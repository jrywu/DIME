/********************************************************************************
* SettingsProcParityTest: the settings window procedures, C++
* (SettingsProc::Cpp::Main / Content, formerly SettingsWindow::WndProc /
* ContentWndProc) vs Rust (dime-settings wndproc.rs through
* SettingsProc::Rust). A recording Host answers with seeded random values
* (window data present, content area, screen dpi, owner-draw and colour
* results); random messages, including unhandled ones, with random
* wParam / lParam and real structures for the pointer messages, must produce
* the same Host calls, return values, and MINMAXINFO.
********************************************************************************/
#include "pch.h"
#include "CppUnitTest.h"
#include <Windows.h>
#include <random>
#include <string>
#include <vector>

#include "../DIMESettings/SettingsProc.h"
#include "../DIMESettings/SettingsEditor.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace SP = SettingsProc;

namespace
{
    class Recorder : public SP::Host {
    public:
        std::wstring log;
        std::mt19937 rng;
        bool hasData = true, hasContent = true;
        explicit Recorder(unsigned seed) : rng(seed) {}

        void L(const std::wstring& s) { log += s; log += L";"; }
        static std::wstring N(long long v) { return std::to_wstring(v); }

        bool HasData() override { L(L"hasData"); return hasData; }
        bool HasContent() override { L(L"hasContent"); return hasContent; }
        void Create(HINSTANCE inst) override { L(L"create " + N((long long)(intptr_t)inst)); }
        void Nav(int k, int x, int y, int d, int t) override
        {
            L(L"nav " + N(k) + L"," + N(x) + L"," + N(y) + L"," + N(d) + L"," + N(t));
        }
        void ThemeChanged() override { L(L"theme"); }
        void SettingChanged() override { L(L"setting"); }
        void DpiChanged(UINT dpi, const RECT* rc) override
        {
            L(L"dpi " + N(dpi) + (rc ? L" " + N(rc->left) + L"," + N(rc->top) + L"," + N(rc->right) + L"," + N(rc->bottom) : L" null"));
        }
        void PaintMain() override { L(L"paintMain"); }
        void PaintContent() override { L(L"paintContent"); }
        UINT ScreenDpi() override { UINT d = 72 + rng() % 200; L(L"screenDpi " + N(d)); return d; }
        void SwitchMode(int m) override { L(L"switch " + N(m)); }
        void SetForeground() override { L(L"foreground"); }
        void Destroy() override { L(L"destroy"); }
        void Quit() override { L(L"quit"); }
        void Command(WPARAM w, LPARAM l) override { L(L"command " + N((long long)w) + L"," + N((long long)l)); }
        void EditorRevalidate() override { L(L"revalidate"); }
        void EditorViewport() override { L(L"viewport"); }
        bool DrawItem(const DRAWITEMSTRUCT* dis) override
        {
            bool r = (rng() & 1) != 0;
            L(L"draw " + N((long long)(intptr_t)dis) + L"=" + N(r));
            return r;
        }
        bool CtlColor(HDC hdc, LRESULT& brush) override
        {
            bool r = (rng() % 3) != 0;
            brush = r ? (LRESULT)(rng() % 3 == 0 ? 0 : 0x1000 + rng() % 100) : 0;
            L(L"ctl " + N((long long)(intptr_t)hdc) + L"=" + N(r) + L"," + N((long long)brush));
            return r;
        }
        LRESULT Default(UINT msg, WPARAM w, LPARAM l) override
        {
            LRESULT r = (LRESULT)(msg * 7 + 1);
            L(L"def " + N(msg) + L"," + N((long long)w) + L"," + N((long long)l));
            return r;
        }
    };

    const UINT kMessages[] = {
        WM_CREATE, WM_DESTROY, WM_SIZE, WM_PAINT, WM_ERASEBKGND, WM_SETTINGCHANGE, WM_GETMINMAXINFO,
        WM_DRAWITEM, WM_COPYDATA, WM_COMMAND, WM_VSCROLL, WM_CTLCOLOREDIT, WM_CTLCOLORLISTBOX,
        WM_CTLCOLORSTATIC, WM_MOUSEMOVE, WM_LBUTTONDOWN, WM_MOUSEWHEEL, WM_MOUSELEAVE, WM_DPICHANGED,
        WM_THEMECHANGED, SettingsEditor::kMsgRevalidate, SettingsEditor::kMsgViewport, WM_USER + 2,
        WM_NULL, WM_KEYDOWN, WM_TIMER, WM_RBUTTONDOWN,
    };

    WPARAM RandomParam(std::mt19937& rng)
    {
        switch (rng() % 4) {
        case 0: return 0;
        case 1: return (WPARAM)(rng() & 0xFFFF);
        case 2: return (WPARAM)rng();   // high word set: signed coordinates / wheel deltas
        default: return (WPARAM)(((rng() & 0xFFFF) << 16) | (rng() & 0xFFFF));
        }
    }

    void Run(bool content, int sequences, int steps)
    {
        std::mt19937 rng(content ? 4242u : 1717u);
        static const wchar_t* lines[] = { L"--mode array", L"x --mode Phonetic y", L"--mode nope", L"", L"--mode", L"--MODE dayi" };
        int calls = 0;
        for (int n = 0; n < sequences; n++) {
            unsigned seed = rng();
            Recorder a(seed), b(seed);
            for (int step = 0; step < steps; step++) {
                bool hasData = (rng() % 5) != 0, hasContent = (rng() % 4) != 0;
                a.hasData = b.hasData = hasData;
                a.hasContent = b.hasContent = hasContent;
                UINT msg = kMessages[rng() % _countof(kMessages)];
                WPARAM w = RandomParam(rng);
                LPARAM l = (LPARAM)RandomParam(rng);
                CREATESTRUCTW cs = {};
                MINMAXINFO ma = {}, mb = {};
                COPYDATASTRUCT cds = {};
                RECT rc = { (LONG)(rng() % 2000) - 500, (LONG)(rng() % 2000) - 500, (LONG)(rng() % 3000), (LONG)(rng() % 3000) };
                LPARAM la = l, lb = l;
                switch (msg) {
                case WM_CREATE:
                    cs.hInstance = (HINSTANCE)(intptr_t)(0x400000 + (rng() % 16) * 0x10000);
                    la = lb = (LPARAM)&cs;
                    break;
                case WM_GETMINMAXINFO:
                    // The content area passes it to Default: same address on both sides.
                    la = (LPARAM)&ma;
                    lb = content ? (LPARAM)&ma : (LPARAM)&mb;
                    break;
                case WM_COPYDATA:
                    if (rng() % 4 == 0) { la = lb = 0; break; }
                    cds.dwData = 1;
                    cds.lpData = (rng() % 4 == 0) ? nullptr : (void*)lines[rng() % _countof(lines)];
                    la = lb = (LPARAM)&cds;
                    break;
                case WM_DPICHANGED:
                    la = lb = (rng() % 3 == 0) ? 0 : (LPARAM)&rc;
                    break;
                case WM_DRAWITEM:
                    if (rng() % 3 == 0) la = lb = 0;
                    break;
                }
                LRESULT ra = content ? SP::Cpp::Content(a, msg, w, la) : SP::Cpp::Main(a, msg, w, la);
                LRESULT rb = content ? SP::Rust::Content(b, msg, w, lb) : SP::Rust::Main(b, msg, w, lb);
                std::wstring what = std::wstring(content ? L"content" : L"main") + L" seq " + std::to_wstring(n)
                    + L" step " + std::to_wstring(step) + L" msg " + std::to_wstring(msg);
                if (a.log != b.log)
                    Assert::Fail((what + L"\nC++:  " + a.log + L"\nRust: " + b.log).c_str());
                Assert::AreEqual((long long)ra, (long long)rb, (what + L" result").c_str());
                Assert::IsTrue(memcmp(&ma, &mb, sizeof(ma)) == 0, (what + L" MINMAXINFO").c_str());
                calls++;
            }
        }
        Assert::IsTrue(calls >= sequences * steps, L"messages");
    }
}

namespace DIMETests
{
    TEST_CLASS(SettingsProcParityTest)
    {
    public:
        TEST_METHOD(MainWindow) { Run(false, 400, 60); }
        TEST_METHOD(ContentArea) { Run(true, 400, 60); }
    };
}
