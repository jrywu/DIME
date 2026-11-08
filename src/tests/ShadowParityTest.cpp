/********************************************************************************
* ShadowParityTest: the drop shadow, C++ (ShadowCpp / CShadowWindow) vs Rust
* (dime-ui::shadow).
*  - ColorMatchesCpp: the shadow gray and peak alpha for light / dark themes
*    and random system window colors.
*  - PixelsMatchCpp: the premultiplied shadow image (rounded-rectangle SDF,
*    double precision) byte for byte over window sizes, corner radii, grays and
*    peak alphas.
*  - BehaviorMatchesCpp: a C++ CShadowWindow and a Rust-owned shadow
*    (CRustShadowWindow, phase 3c-5b) around two real candidate windows,
*    through random owner moves / resizes / show / hide, shadow show, owner
*    moved notifications and settings changes: the shadow's rectangle relative
*    to its owner, visibility, layered style, color, offset and gradient flag.
*  - ShadowMessagesAreNotDispatched: both answer S_FALSE to every message (the
*    C++ shadow never sets its UI window, so CBaseWindow never dispatches).
********************************************************************************/
#include "pch.h"
#include "CppUnitTest.h"
#include <Windows.h>
#include <random>
#include <string>
#include <vector>

#include "../Globals.h"
#include "../CandidateWindow.h"
#include "../ShadowWindow.h"
#include "../BaseStructure.h"
#include "../Config.h"
#include "../Define.h"
#include "../RustShadowWindow.h"
#include "WindowClassTestHelper.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

#ifdef DIME_RUST_AVAILABLE
#include "dime_ffi.h"

namespace
{
    HRESULT NoCallback(void*, enum CANDWND_ACTION) { return S_OK; }

    template <class Shadow>
    std::wstring ShadowState(Shadow* sh, CCandidateWindow* owner)
    {
        RECT rc = { 0, 0, 0, 0 }, orc = { 0, 0, 0, 0 };
        GetWindowRect(sh->_GetWnd(), &rc);
        GetWindowRect(owner->_GetWnd(), &orc);
        SIZE shift = sh->_GetSizeShiftForTest();
        return L"rect=" + std::to_wstring(rc.left - orc.left) + L"," + std::to_wstring(rc.top - orc.top) + L"," +
            std::to_wstring(rc.right - rc.left) + L"x" + std::to_wstring(rc.bottom - rc.top) +
            L" visible=" + std::to_wstring(IsWindowVisible(sh->_GetWnd())) +
            L" layered=" + std::to_wstring((GetWindowLong(sh->_GetWnd(), GWL_EXSTYLE) & WS_EX_LAYERED) != 0) +
            L" color=" + std::to_wstring(sh->_GetColorForTest()) +
            L" shift=" + std::to_wstring(shift.cx) + L"," + std::to_wstring(shift.cy) +
            L" gradient=" + std::to_wstring(sh->_IsGradientForTest());
    }

    // A class for C++ CShadowWindow (CBaseWindow's window procedure) and one for the
    // Rust-owned shadow (dime-ui's), whatever the build registered for the real ones.
    ATOM ClassAtom(const wchar_t* name, bool rust)
    {
        EnsureWindowClassesRegistered();
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        ATOM atom = (ATOM)GetClassInfoExW(Global::dllInstanceHandle, name, &wc);
        if (atom) return atom;
        if (rust) return dime_ui_register_window_class(Global::dllInstanceHandle, (const uint16_t*)name, 0);
        CBaseWindow::_InitWindowClass(name, &atom);
        return atom;
    }
    ATOM CppShadowAtom() { return ClassAtom(L"DIME.Test.CppShadowWindow", false); }
    ATOM RustShadowAtom() { return ClassAtom(L"DIME.Test.RustShadowWindow", true); }
    const DWORD kShadowExStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED;
    const DWORD kShadowStyle = WS_DISABLED | WS_POPUP;
}

namespace DIMEUnitTests
{
    TEST_CLASS(ShadowParityTest)
    {
    public:
        TEST_METHOD(ColorMatchesCpp)
        {
            std::mt19937 rng(61);
            for (int i = 0; i < 20000; i++)
            {
                bool dark = rng() % 2;
                COLORREF sys = (i < 4) ? (i % 2 ? RGB(255, 255, 255) : RGB(0, 0, 0)) : (COLORREF)(rng() & 0xFFFFFF);
                BYTE gc = 0, ac = 0, gr = 0, ar = 0;
                ShadowCpp::Color(dark, sys, &gc, &ac);
                dime_ui_shadow_color(dark ? 1 : 0, sys, &gr, &ar);
                if (gc != gr || ac != ar)
                    Assert::Fail((L"color dark=" + std::to_wstring(dark) + L" sys=" + std::to_wstring(sys)).c_str());
            }
        }

        TEST_METHOD(PixelsMatchCpp)
        {
            std::mt19937 rng(62);
            const BYTE grays[] = { 0, 200 };
            const BYTE alphas[] = { 35, 45 };
            long long pixels = 0;
            for (int i = 0; i < 600; i++)
            {
                // Owners at least 1 px: cx, cy >= 2 * SHADOW_SPREAD + 1.
                int cx = 2 * SHADOW_SPREAD + 1 + (int)(rng() % 420);
                int cy = 2 * SHADOW_SPREAD + 1 + (int)(rng() % 320);
                int radius = (int)(rng() % 41);
                BYTE gray = (i % 5 == 4) ? (BYTE)(rng() % 256) : grays[rng() % 2];
                BYTE maxAlpha = (i % 5 == 4) ? (BYTE)(rng() % 256) : alphas[rng() % 2];
                std::vector<DWORD> c((size_t)cx * cy, 0), r((size_t)cx * cy, 0xDEADBEEF);
                ShadowCpp::FillPixels(c.data(), cx, cy, SHADOW_SPREAD, radius, gray, maxAlpha);
                dime_ui_shadow_pixels(cx, cy, SHADOW_SPREAD, radius, gray, maxAlpha, (uint32_t*)r.data(), r.size());
                for (size_t k = 0; k < c.size(); k++)
                {
                    if (c[k] != r[k])
                        Assert::Fail((L"pixel " + std::to_wstring(k % cx) + L"," + std::to_wstring(k / cx) + L" of " + std::to_wstring(cx) + L"x" +
                            std::to_wstring(cy) + L" r=" + std::to_wstring(radius) + L": C++ " + std::to_wstring(c[k]) + L" Rust " + std::to_wstring(r[k])).c_str());
                }
                pixels += (long long)c.size();
            }
            Logger::WriteMessage((std::to_wstring(pixels) + L" shadow pixels compared").c_str());
        }

        // CBaseWindow::_WindowProc only forwards to _WindowProcCallback when the object's
        // _pUIWnd is set; the shadow never sets it, so after WM_CREATE every shadow message
        // returns S_FALSE without reaching either implementation (or DefWindowProc). Its
        // WM_PAINT / WM_SETTINGCHANGE / WM_APP+1 handling is dead code; this pins that down
        // (and keeps the Rust-owned shadow of phase 3c-5 from "fixing" it).
        TEST_METHOD(ShadowMessagesAreNotDispatched)
        {
            EnsureWindowClassesRegistered();
            CCandidateRange range;
            range.Append()->CandIndex = L'1';
            CCandidateWindow* w = new CCandidateWindow(NoCallback, nullptr, &range, FALSE);
            Assert::IsTrue(w->_Create(2, 12, nullptr) != FALSE);
            CShadowWindow cpp(w);
            CRustShadowWindow rust(w);
            Assert::IsTrue(cpp._Create(CppShadowAtom(), kShadowExStyle, kShadowStyle, w) != FALSE);
            Assert::IsTrue(rust._Create(RustShadowAtom(), kShadowExStyle, kShadowStyle, w) != FALSE);
            for (HWND h : { cpp._GetWnd(), rust._GetWnd() })
            {
                Assert::IsNotNull(h);
                Assert::AreEqual((LRESULT)S_FALSE, SendMessage(h, WM_APP + 1, 0, 0), L"WM_APP+1 (dispatched: 0)");
                Assert::AreEqual((LRESULT)S_FALSE, SendMessage(h, WM_PAINT, 0, 0), L"WM_PAINT (dispatched: 0)");
                Assert::AreEqual((LRESULT)S_FALSE, SendMessage(h, WM_SETTINGCHANGE, 0, 0), L"WM_SETTINGCHANGE");
            }
            // The candidate's own shadow (Rust-owned in the Rust build) too.
            Assert::AreEqual((LRESULT)S_FALSE, SendMessage(w->_GetShadowForTest()->_GetWnd(), WM_APP + 1, 0, 0), L"candidate's shadow");
            cpp._Destroy();
            DestroyAndDelete(w);
        }

        TEST_METHOD(BehaviorMatchesCpp)
        {
            EnsureWindowClassesRegistered();
            HFONT savedFont = Global::defaultlFontHandle;
            IME_COLOR_MODE savedMode = CConfig::GetColorMode();
            HFONT font = CreateFont(-24, 0, 0, 0, FW_NORMAL, FALSE, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Microsoft JhengHei");
            Global::defaultlFontHandle = font;

            std::mt19937 rng(63);
            int compared = 0;
            for (int scenario = 0; scenario < 10; scenario++)
            {
                CCandidateRange range[2];
                CCandidateWindow* w[2] = {};
                for (int k = 0; k < 2; k++)
                {
                    for (int r = 0; r < 5; r++) range[k].Append()->CandIndex = L'1' + (wchar_t)r;
                    w[k] = new CCandidateWindow(NoCallback, nullptr, &range[k], FALSE);
                    Assert::IsTrue(w[k]->_Create(2, 12, nullptr) != FALSE);
                    Assert::IsNotNull(w[k]->_GetWnd(), L"a real window (not a virtual one)");
                    w[k]->_Move(300, 200);
                    w[k]->_ResizeWindow();
                }
                // Shadows of our own around the two owners: C++ and Rust-owned.
                CShadowWindow* cpp = new CShadowWindow(w[0]);
                CRustShadowWindow* rust = new CRustShadowWindow(w[1]);
                Assert::IsTrue(cpp->_Create(CppShadowAtom(), kShadowExStyle, kShadowStyle, w[0]) != FALSE);
                Assert::IsTrue(rust->_Create(RustShadowAtom(), kShadowExStyle, kShadowStyle, w[1]) != FALSE);
                for (int step = 0; step < 60; step++)
                {
                    int op = (int)(rng() % 9);
                    BOOL flag = rng() % 2;
                    int x = 100 + (int)(rng() % 600), y = 100 + (int)(rng() % 400);
                    int cx = 40 + (int)(rng() % 300), cy = 40 + (int)(rng() % 300);
                    bool dark = rng() % 2;
                    CConfig::SetColorMode(dark ? IME_COLOR_MODE::IME_COLOR_MODE_DARK : IME_COLOR_MODE::IME_COLOR_MODE_LIGHT);
                    std::wstring st[2];
                    for (int k = 0; k < 2; k++)
                    {
                        auto act = [&](auto* sh) {
                            switch (op)
                            {
                            case 0: w[k]->_Move(x, y); break;
                            case 1: SetWindowPos(w[k]->_GetWnd(), nullptr, x, y, cx, cy, SWP_NOZORDER | SWP_NOACTIVATE); break;
                            case 2: sh->_Show(flag); break;
                            case 3: case 5: case 6: case 7: sh->_OnOwnerWndMoved(flag); break;
                            case 4: sh->_OnSettingChange(); break;
                            case 8: SetWindowPos(w[k]->_GetWnd(), nullptr, 0, 0, 0, 0,
                                        SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | (flag ? SWP_SHOWWINDOW : SWP_HIDEWINDOW)); break;
                            }
                            return ShadowState(sh, w[k]);
                        };
                        st[k] = (k == 0) ? act(cpp) : act(rust);
                    }
                    if (st[0] != st[1])
                        Assert::Fail((L"scenario " + std::to_wstring(scenario) + L" step " + std::to_wstring(step) + L" op " + std::to_wstring(op) +
                            L"\nC++  " + st[0] + L"\nRust " + st[1]).c_str());
                    compared++;
                }
                cpp->_Destroy();
                delete cpp;
                delete rust;
                for (int k = 0; k < 2; k++) DestroyAndDelete(w[k]);
            }
            CConfig::SetColorMode(savedMode);
            Global::defaultlFontHandle = savedFont;
            DeleteObject(font);
            Logger::WriteMessage((std::to_wstring(compared) + L" shadow steps compared").c_str());
        }
    };
}
#endif
