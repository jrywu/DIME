/********************************************************************************
* SettingsDriverParityTest: what the settings window does with navigation,
* frame, mode-switch, and rebuild events, C++ (SettingsDriver::Cpp, formerly
* SettingsWindow::RunNav / RunFrame / SwitchMode / RebuildContentArea /
* UpdateScrollInfo / RepositionControlsForScroll / ShowAllControls) vs Rust
* (dime-settings driver.rs through SettingsDriver::Rust). A recording Host
* answers from a seeded script (content area, client sizes, colour mode,
* [儲存] state, preview height, the child controls and their check state, the
* unsaved-table prompt); random states and random operations must produce the
* same Host calls and the same state after every operation.
********************************************************************************/
#include "pch.h"
#include "CppUnitTest.h"
#include <Windows.h>
#include <algorithm>
#include <random>
#include <string>
#include <vector>

#include "../DIMESettings/SettingsDriver.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace SD = SettingsDriver;
namespace SN = SettingsNav;
namespace SF = SettingsFrame;

namespace
{
    int R(std::mt19937& rng, int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); }

    // Toggle ids of the shipped pages, plus ids no row uses.
    const int kIds[] = { 11, 14, 15, 18, 20, 21, 22, 23, 24, 26, 27, 28, 31, 35, 37, 38, 39, 300, 999 };

    SD::ControlInfo RandomControl(std::mt19937& rng)
    {
        SD::ControlInfo c = {};
        c.id = kIds[R(rng, 0, _countof(kIds) - 1)];
        c.exists = R(rng, 0, 9) != 0;
        c.alive = c.exists && R(rng, 0, 9) != 0;
        c.visible = c.alive && R(rng, 0, 2) != 0;
        c.x = R(rng, 0, 600);
        c.y = R(rng, -100, 3000);
        c.w = R(rng, 0, 5) == 0 ? 0 : R(rng, 1, 400);
        c.h = R(rng, 0, 5) == 0 ? 0 : R(rng, 1, 300);
        return c;
    }

    class Recorder : public SD::Host {
    public:
        std::wstring log;
        std::mt19937 rng;
        UINT dpi;
        bool hasContent;
        std::vector<SD::ControlInfo> controls;
        std::vector<bool> checked;

        Recorder(unsigned seed, UINT d, bool content) : rng(seed), dpi(d), hasContent(content)
        {
            int n = R(rng, 0, 10);
            for (int i = 0; i < n; i++) {
                controls.push_back(RandomControl(rng));
                checked.push_back(R(rng, 0, 1) != 0);
            }
        }

        void L(const std::wstring& s) { log += s; log += L";"; }
        static std::wstring N(long long v) { return std::to_wstring(v); }
        int Rnd(int lo, int hi) { return R(rng, lo, hi); }

        UINT MainDpi() override { L(L"mainDpi"); return dpi; }
        UINT ContentDpi() override { L(L"contentDpi"); return dpi; }
        bool HasContent() override { L(L"hasContent"); return hasContent; }
        void MainClient(int& w, int& h) override
        {
            w = Rnd(300, 2200);
            h = Rnd(200, 1500);
            L(L"mainClient " + N(w) + L"," + N(h));
        }
        int ContentClientHeight() override { int h = Rnd(0, 1400); L(L"contentH " + N(h)); return h; }
        int VScrollWidth() override { int w = Rnd(12, 24); L(L"vscroll " + N(w)); return w; }
        bool SystemDark() override { bool d = Rnd(0, 1) != 0; L(L"systemDark " + N(d)); return d; }
        int ColorMode() override { int m = Rnd(0, 3); L(L"colorMode " + N(m)); return m; }
        bool SaveEnabled() override { bool e = Rnd(0, 2) == 0; L(L"saveEnabled " + N(e)); return e; }
        int PreviewHeight(UINT d) override { int h = Rnd(150, 320); L(L"preview " + N(d) + L"=" + N(h)); return h; }

        int ControlCount() override { L(L"count " + N(controls.size())); return (int)controls.size(); }
        SD::ControlInfo Control(int i) override
        {
            L(L"control " + N(i));
            if (i < 0 || i >= (int)controls.size()) return {};
            return controls[i];
        }
        void MoveControl(int i, int x, int y, int w, int h) override
        {
            L(L"move " + N(i) + L" " + N(x) + L"," + N(y) + L"," + N(w) + L"," + N(h));
        }
        void ShowControl(int i, bool show) override { L(L"show " + N(i) + L"=" + N(show)); }
        void ResetControlRegion(int i) override { L(L"rgn " + N(i)); }
        bool ControlChecked(int i) override
        {
            bool c = i >= 0 && i < (int)checked.size() && checked[i];
            L(L"checked " + N(i) + L"=" + N(c));
            return c;
        }
        void SetControlChecked(int i, bool on) override
        {
            if (i >= 0 && i < (int)checked.size()) checked[i] = on;
            L(L"setChecked " + N(i) + L"=" + N(on));
        }
        void DestroyControls() override { controls.clear(); checked.clear(); L(L"destroyControls"); }
        void CreateControls(const SettingsWindowLayout::PlanInput& in, int cardW) override
        {
            std::wstring e;
            for (bool x : in.expanded) e += x ? L"1" : L"0";
            L(L"create dpi=" + N(in.dpi) + L" sel=" + N(in.selectedModeIndex) + L" level=" + N(in.navLevel)
                + L" card=" + N(in.currentCardIndex) + L" mask=" + N(in.modeMask) + L" color=" + N(in.colorMode)
                + L" grid=" + N(in.gridExpanded) + L" preview=" + N(in.previewH) + L" exp=" + e + L" w=" + N(cardW));
            int n = Rnd(0, 12);
            for (int i = 0; i < n; i++) {
                controls.push_back(RandomControl(rng));
                checked.push_back(Rnd(0, 1) != 0);
            }
        }
        void Populate() override { L(L"populate"); }

        void InvalidateMain(const RECT* rc, bool erase) override
        {
            L(L"invMain " + (rc ? N(rc->left) + L"," + N(rc->top) + L"," + N(rc->right) + L"," + N(rc->bottom) : std::wstring(L"all"))
                + L" " + N(erase));
        }
        void InvalidateContent(bool erase) override { L(L"invContent " + N(erase)); }
        void TrackLeave(bool content) override { L(L"track " + N(content)); }
        void SetScrollInfo(int total, int page, int pos) override
        {
            L(L"scrollInfo " + N(total) + L"," + N(page) + L"," + N(pos));
        }
        void RedrawContent() override { L(L"redraw"); }
        void UpdateContent() override { L(L"update"); }
        void MoveContent(int x, int y, int w, int h) override
        {
            L(L"moveContent " + N(x) + L"," + N(y) + L"," + N(w) + L"," + N(h));
        }
        void FocusContent() override { L(L"focus"); }
        void CreateContent(int x, int y, int w, int h) override
        {
            hasContent = true;
            L(L"createContent " + N(x) + L"," + N(y) + L"," + N(w) + L"," + N(h));
        }
        void ContentTheme(bool dark) override { L(L"contentTheme " + N(dark)); }
        void SetMainPos(int x, int y, int w, int h) override
        {
            L(L"pos " + N(x) + L"," + N(y) + L"," + N(w) + L"," + N(h));
        }
        void SetDarkTitle(bool dark) override { L(L"darkTitle " + N(dark)); }
        void SetCaptionColor(int a) override { L(L"caption " + N(a)); }
        void ThemeBrushes() override { L(L"brushes"); }
        void CreateFonts(int d) override { L(L"fonts " + N(d)); }
        void DestroyFonts() override { L(L"destroyFonts"); }

        int AskSaveBeforeBack() override
        {
            static const int answers[] = { IDYES, IDNO, IDCANCEL };
            int a = answers[Rnd(0, 2)];
            L(L"ask=" + N(a));
            return a;
        }
        void ClickSave() override { L(L"clickSave"); }

        void ToggleSnapshot(int id, bool on) override { L(L"sync " + N(id) + L"=" + N(on)); }
        void ApplyAndSave() override { L(L"apply"); }
        void EnumerateReverseProviders() override { L(L"enumerate"); }
        void LoadConfig(int m) override { L(L"loadConfig " + N(m)); }
        void LoadColors() override { L(L"loadColors"); }
        void LoadSnapshot() override { L(L"loadSnapshot"); }
        void RecreateEngine(int m) override { L(L"engine " + N(m)); }
    };

    SD::State RandomState(std::mt19937& rng)
    {
        SD::State s = {};
        SN::NavState& n = s.nav;
        int sel[] = { 0, 1, 2, 3, 5 };
        n.selectedModeIndex = sel[R(rng, 0, 4)];
        n.currentMode = n.selectedModeIndex < 4 ? n.selectedModeIndex : R(rng, 0, 3);
        n.navLevel = n.selectedModeIndex == 5 ? 0 : R(rng, 0, 1);
        n.currentCardIndex = R(rng, -1, 4);
        n.hoverSidebarIndex = R(rng, -1, 6);
        n.hoverRowIndex = R(rng, -1, 5);
        n.totalContentHeight = R(rng, 0, 3000);
        n.scrollMax = R(rng, 0, 1500);
        n.scrollPos = R(rng, 0, n.scrollMax);
        n.sidebarNarrowMode = R(rng, 0, 1);
        n.sidebarCollapsed = n.sidebarNarrowMode ? R(rng, 0, 1) : 0;
        n.colorGridExpanded = R(rng, 0, 1);
        for (bool& e : n.cardExpanded) e = R(rng, 0, 1) != 0;
        s.isDark = R(rng, 0, 1);
        return s;
    }

    SN::NavEvent RandomNav(std::mt19937& rng, UINT dpi)
    {
        SN::NavEvent ev = {};
        ev.kind = R(rng, SN::EV_MAIN_SIZE, SN::EV_BACK);
        int scale = (int)dpi;
        switch (ev.kind) {
        case SN::EV_MAIN_SIZE:
            ev.x = MulDiv(R(rng, 400, 1400), scale, 96);
            ev.y = MulDiv(R(rng, 300, 900), scale, 96);
            break;
        case SN::EV_MAIN_WHEEL:
        case SN::EV_CONTENT_WHEEL:
            ev.delta = 120 * R(rng, -3, 3) + (R(rng, 0, 3) == 0 ? R(rng, -60, 60) : 0);
            break;
        case SN::EV_CONTENT_VSCROLL:
            ev.delta = R(rng, 0, 8);
            ev.thumb = R(rng, 0, 2000);
            break;
        default:
            // Aim at the sidebar, the breadcrumb, the hamburger, or the cards.
            switch (R(rng, 0, 3)) {
            case 0: ev.x = MulDiv(R(rng, 0, 300), scale, 96); ev.y = MulDiv(R(rng, 0, 320), scale, 96); break;
            case 1: ev.x = MulDiv(R(rng, 250, 600), scale, 96); ev.y = MulDiv(R(rng, 0, 50), scale, 96); break;
            case 2: ev.x = MulDiv(R(rng, 0, 40), scale, 96); ev.y = MulDiv(R(rng, 0, 40), scale, 96); break;
            default: ev.x = MulDiv(R(rng, 0, 900), scale, 96); ev.y = MulDiv(R(rng, -20, 1400), scale, 96); break;
            }
            break;
        }
        return ev;
    }

    SF::FrameEvent RandomFrame(std::mt19937& rng, UINT dpi)
    {
        SF::FrameEvent ev = {};
        ev.kind = R(rng, SF::EV_CREATE, SF::EV_DPI_CHANGED);
        ev.dpi = ev.kind == SF::EV_DPI_CHANGED ? (UINT)(96 + 24 * R(rng, 0, 6)) : dpi;
        ev.clientW = R(rng, 300, 2000);
        ev.clientH = R(rng, 200, 1400);
        if (R(rng, 0, 2) != 0) {
            ev.hasRect = 1;
            ev.rc = { R(rng, -500, 1500), R(rng, -500, 1000), R(rng, 0, 3000), R(rng, 0, 2000) };
        }
        return ev;
    }

    std::wstring Describe(const SD::State& s)
    {
        const SN::NavState& n = s.nav;
        std::wstring e;
        for (bool x : n.cardExpanded) e += x ? L"1" : L"0";
        return L"sel " + std::to_wstring(n.selectedModeIndex) + L" hoverSb " + std::to_wstring(n.hoverSidebarIndex)
            + L" level " + std::to_wstring(n.navLevel) + L" card " + std::to_wstring(n.currentCardIndex)
            + L" hoverRow " + std::to_wstring(n.hoverRowIndex) + L" mode " + std::to_wstring(n.currentMode)
            + L" scroll " + std::to_wstring(n.scrollPos) + L"/" + std::to_wstring(n.scrollMax)
            + L" total " + std::to_wstring(n.totalContentHeight) + L" collapsed " + std::to_wstring(n.sidebarCollapsed)
            + L" narrow " + std::to_wstring(n.sidebarNarrowMode) + L" grid " + std::to_wstring(n.colorGridExpanded)
            + L" exp " + e + L" dark " + std::to_wstring(s.isDark);
    }

    // bool[16] may hold values other than 0 / 1 only if written as bytes; compare as booleans.
    bool SameState(const SD::State& a, const SD::State& b)
    {
        SD::State x = a, y = b;
        for (bool& e : x.nav.cardExpanded) e = e ? true : false;
        for (bool& e : y.nav.cardExpanded) e = e ? true : false;
        return memcmp(&x, &y, sizeof(x)) == 0;
    }
}

namespace DIMETests
{
    TEST_CLASS(SettingsDriverParityTest)
    {
    public:
        TEST_METHOD(RandomOperationSequences)
        {
            std::mt19937 rng(9090);
            const UINT dpis[] = { 96, 120, 144, 192 };
            int ops[4] = {}, calls = 0, toggles = 0, prompts = 0, rebuilds = 0, moves = 0;
            for (int run = 0; run < 1500; run++) {
                UINT dpi = dpis[R(rng, 0, 3)];
                bool content = R(rng, 0, 7) != 0;
                unsigned seed = rng();
                Recorder a(seed, dpi, content), b(seed, dpi, content);
                SD::State sa = RandomState(rng), sb = sa;
                for (int step = 0; step < 30; step++) {
                    int op = R(rng, 0, 9);
                    std::wstring what;
                    if (op < 6) {
                        SN::NavEvent ev = RandomNav(rng, dpi);
                        what = L"nav " + std::to_wstring(ev.kind) + L" (" + std::to_wstring(ev.x) + L","
                            + std::to_wstring(ev.y) + L"," + std::to_wstring(ev.delta) + L"," + std::to_wstring(ev.thumb) + L")";
                        SD::Cpp::Nav(a, &sa, ev);
                        SD::Rust::Nav(b, &sb, ev);
                        ops[0]++;
                    } else if (op < 8) {
                        SF::FrameEvent ev = RandomFrame(rng, dpi);
                        what = L"frame " + std::to_wstring(ev.kind);
                        SD::Cpp::Frame(a, &sa, ev);
                        SD::Rust::Frame(b, &sb, ev);
                        ops[1]++;
                    } else if (op == 8) {
                        int mode = R(rng, 0, 3);
                        what = L"switch " + std::to_wstring(mode);
                        SD::Cpp::SwitchMode(a, &sa, mode);
                        SD::Rust::SwitchMode(b, &sb, mode);
                        ops[2]++;
                    } else {
                        what = L"rebuild";
                        SD::Cpp::Rebuild(a, &sa);
                        SD::Rust::Rebuild(b, &sb);
                        ops[3]++;
                    }
                    std::wstring ctx = L"run " + std::to_wstring(run) + L" step " + std::to_wstring(step) + L" " + what
                        + L"\nC++:  " + Describe(sa) + L"\nRust: " + Describe(sb);
                    if (a.log != b.log)
                        Assert::Fail((ctx + L"\nC++ calls:  " + a.log + L"\nRust calls: " + b.log).c_str());
                    Assert::IsTrue(SameState(sa, sb), (L"state: " + ctx).c_str());
                    calls += (int)std::count(a.log.begin(), a.log.end(), L';');
                    if (a.log.find(L"sync ") != std::wstring::npos) toggles++;
                    if (a.log.find(L"ask=") != std::wstring::npos) prompts++;
                    if (a.log.find(L"create dpi") != std::wstring::npos) rebuilds++;
                    if (a.log.find(L"move ") != std::wstring::npos) moves++;
                    a.log.clear();
                    b.log.clear();
                }
            }
            std::wstring cov = L"coverage: nav " + std::to_wstring(ops[0]) + L" frame " + std::to_wstring(ops[1])
                + L" switch " + std::to_wstring(ops[2]) + L" rebuild " + std::to_wstring(ops[3])
                + L" calls " + std::to_wstring(calls) + L" toggles " + std::to_wstring(toggles)
                + L" prompts " + std::to_wstring(prompts) + L" rebuilds " + std::to_wstring(rebuilds)
                + L" scroll moves " + std::to_wstring(moves);
            Logger::WriteMessage(cov.c_str());
            Assert::IsTrue(toggles > 20 && prompts > 1000 && rebuilds > 1000 && moves > 100, cov.c_str());
        }
    };
}
