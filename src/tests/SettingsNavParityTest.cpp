/********************************************************************************
* SettingsNavParityTest: the settings window's navigation reducer, C++
* (SettingsNav::Cpp::Handle, the WM_SIZE / WM_MOUSEWHEEL / WM_VSCROLL /
* WM_MOUSEMOVE / WM_MOUSELEAVE / WM_LBUTTONDOWN logic lifted out of
* SettingsWindow.cpp) vs Rust (dime-settings nav.rs via
* dime_settings_nav_handle). Random states are driven by random event
* sequences; after every event the state and the ordered effect list must
* match. Clicks and moves are aimed at sidebar items, cards, rows, the
* breadcrumb, and the hamburger at several dpis.
********************************************************************************/
#include "pch.h"
#include "CppUnitTest.h"
#include <Windows.h>
#include <random>
#include <string>
#include <vector>

#include "../DIMESettings/SettingsNav.h"
#include "dime_ffi.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace SN = SettingsNav;

namespace
{
    bool SameState(const SN::NavState& a, const SN::NavState& b)
    {
        return memcmp(&a, &b, sizeof(a)) == 0;
    }

    std::wstring Describe(const SN::NavState& s)
    {
        return L"sel=" + std::to_wstring(s.selectedModeIndex) + L" nav=" + std::to_wstring(s.navLevel)
            + L" card=" + std::to_wstring(s.currentCardIndex) + L" scroll=" + std::to_wstring(s.scrollPos)
            + L" hoverSb=" + std::to_wstring(s.hoverSidebarIndex) + L" hoverRow=" + std::to_wstring(s.hoverRowIndex)
            + L" collapsed=" + std::to_wstring(s.sidebarCollapsed) + L" narrow=" + std::to_wstring(s.sidebarNarrowMode);
    }

    SN::NavState RandomState(std::mt19937& rng)
    {
        auto r = [&](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };
        SN::NavState s = {};
        int sel[] = { 0, 1, 2, 3, 5 };
        s.selectedModeIndex = sel[r(0, 4)];
        s.currentMode = s.selectedModeIndex < 4 ? s.selectedModeIndex : r(0, 3);
        s.navLevel = s.selectedModeIndex == 5 ? 0 : r(0, 1);
        s.currentCardIndex = r(-1, 4);
        s.hoverSidebarIndex = r(-1, 6);
        s.hoverRowIndex = r(-1, 5);
        s.totalContentHeight = r(0, 3000);
        s.scrollMax = r(0, 1500);
        s.scrollPos = r(0, s.scrollMax);
        s.sidebarNarrowMode = r(0, 1);
        s.sidebarCollapsed = s.sidebarNarrowMode ? r(0, 1) : 0;
        s.colorGridExpanded = r(0, 1);
        for (bool& e : s.cardExpanded) e = r(0, 1) != 0;
        return s;
    }

    SN::NavEvent RandomEvent(std::mt19937& rng, UINT dpi)
    {
        auto r = [&](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };
        SN::NavEvent ev = {};
        ev.kind = r(SN::EV_MAIN_SIZE, SN::EV_BACK);
        ev.dpi = dpi;
        ev.previewH = r(150, 320);
        ev.colorMode = r(0, 3);
        ev.customTableDirty = r(0, 1);
        int scale = (int)dpi;
        switch (ev.kind) {
        case SN::EV_MAIN_SIZE:
            ev.x = MulDiv(r(400, 1400), scale, 96);
            ev.y = MulDiv(r(300, 900), scale, 96);
            break;
        case SN::EV_MAIN_WHEEL:
        case SN::EV_CONTENT_WHEEL:
            ev.delta = 120 * r(-3, 3) + (r(0, 3) == 0 ? r(-60, 60) : 0);
            break;
        case SN::EV_CONTENT_VSCROLL:
            ev.delta = r(0, 8);
            ev.thumb = r(0, 2000);
            break;
        default:
            // Aim at the sidebar, the breadcrumb, the hamburger, or the cards.
            switch (r(0, 3)) {
            case 0: ev.x = MulDiv(r(0, 300), scale, 96); ev.y = MulDiv(r(0, 320), scale, 96); break;
            case 1: ev.x = MulDiv(r(250, 600), scale, 96); ev.y = MulDiv(r(0, 50), scale, 96); break;
            case 2: ev.x = MulDiv(r(0, 40), scale, 96); ev.y = MulDiv(r(0, 40), scale, 96); break;
            default: ev.x = MulDiv(r(0, 900), scale, 96); ev.y = MulDiv(r(-20, 1400), scale, 96); break;
            }
            break;
        }
        return ev;
    }
}

namespace DIMEUnitTests
{
    TEST_CLASS(SettingsNavParityTest)
    {
    public:
        TEST_METHOD(RandomEventSequences)
        {
            std::mt19937 rng(77);
            const UINT dpis[] = { 96, 120, 144, 192 };
            int effects = 0, switches = 0, toggles = 0, rebuilds = 0;
            for (int run = 0; run < 1500; run++) {
                UINT dpi = dpis[run % 4];
                SN::NavState a = RandomState(rng), b = a;
                for (int step = 0; step < 40; step++) {
                    SN::NavEvent ev = RandomEvent(rng, dpi);
                    std::vector<SN::NavEffect> fa;
                    SN::Cpp::Handle(a, ev, fa);
                    SN::NavEffect fb[32];
                    size_t count = 0;
                    dime_settings_nav_handle(&b, &ev, fb, 32, &count);
                    std::wstring ctx = L"run " + std::to_wstring(run) + L" step " + std::to_wstring(step)
                        + L" event " + std::to_wstring(ev.kind) + L" (" + std::to_wstring(ev.x) + L"," + std::to_wstring(ev.y)
                        + L") " + Describe(a) + L" | " + Describe(b);
                    Assert::IsTrue(SameState(a, b), (L"state: " + ctx).c_str());
                    Assert::AreEqual(fa.size(), count, (L"effect count: " + ctx).c_str());
                    for (size_t i = 0; i < count; i++) {
                        const SN::NavEffect& x = fa[i];
                        const SN::NavEffect& y = fb[i];
                        bool same = x.kind == y.kind && x.arg == y.arg && x.erase == y.erase && x.hasRect == y.hasRect
                            && (!x.hasRect || EqualRect(&x.rc, &y.rc));
                        Assert::IsTrue(same, (L"effect " + std::to_wstring(i) + L": " + ctx).c_str());
                        effects++;
                        if (x.kind == SN::FX_SWITCH_MODE) switches++;
                        if (x.kind == SN::FX_TOGGLE_CONTROL) toggles++;
                        if (x.kind == SN::FX_REBUILD) rebuilds++;
                    }
                    // The host would apply a mode switch: mirror the state it sets.
                    for (const auto& x : fa) {
                        if (x.kind == SN::FX_SWITCH_MODE) {
                            a.currentMode = b.currentMode = x.arg;
                            a.selectedModeIndex = b.selectedModeIndex = x.arg;
                            a.navLevel = b.navLevel = 0;
                        }
                    }
                }
            }
            Assert::IsTrue(effects > 20000 && switches > 100 && toggles > 50 && rebuilds > 500,
                (L"coverage: effects " + std::to_wstring(effects) + L" switches " + std::to_wstring(switches)
                    + L" toggles " + std::to_wstring(toggles) + L" rebuilds " + std::to_wstring(rebuilds)).c_str());
        }

        TEST_METHOD(MinTrackSize)
        {
            for (UINT dpi : { 72u, 96u, 120u, 144u, 192u, 288u }) {
                POINT a = SN::Cpp::MinTrackSize(dpi), b = {};
                dime_settings_nav_min_track_size(dpi, &b);
                Assert::AreEqual(a.x, b.x);
                Assert::AreEqual(a.y, b.y);
            }
        }
    };
}
