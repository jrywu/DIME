/********************************************************************************
* ShadowParityTest: the drop shadow, C++ (ShadowCpp / CShadowWindow) vs Rust
* (dime-ui::shadow).
*  - ColorMatchesCpp: the shadow gray and peak alpha for light / dark themes
*    and random system window colors.
*  - PixelsMatchCpp: the premultiplied shadow image (rounded-rectangle SDF,
*    double precision) byte for byte over window sizes, corner radii, grays and
*    peak alphas.
*  - BehaviorMatchesCpp: two real candidate windows, one shadow per
*    implementation, through random owner moves / resizes, show / hide, owner
*    moved notifications, settings changes and WM_PAINT / WM_APP+1 /
*    WM_SETTINGCHANGE: the shadow's rectangle relative to its owner, visibility,
*    layered style, color, offset and gradient flag.
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
#include "WindowClassTestHelper.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

#ifdef DIME_RUST_AVAILABLE
#include "dime_ffi.h"

namespace
{
    HRESULT NoCallback(void*, enum CANDWND_ACTION) { return S_OK; }

    std::wstring ShadowState(CCandidateWindow* w)
    {
        CShadowWindow* sh = w->_GetShadowForTest();
        RECT rc = { 0, 0, 0, 0 }, owner = { 0, 0, 0, 0 };
        GetWindowRect(sh->_GetWnd(), &rc);
        GetWindowRect(w->_GetWnd(), &owner);
        SIZE shift = sh->_GetSizeShiftForTest();
        return L"rect=" + std::to_wstring(rc.left - owner.left) + L"," + std::to_wstring(rc.top - owner.top) + L"," +
            std::to_wstring(rc.right - rc.left) + L"x" + std::to_wstring(rc.bottom - rc.top) +
            L" visible=" + std::to_wstring(IsWindowVisible(sh->_GetWnd())) +
            L" layered=" + std::to_wstring((GetWindowLong(sh->_GetWnd(), GWL_EXSTYLE) & WS_EX_LAYERED) != 0) +
            L" color=" + std::to_wstring(sh->_GetColorForTest()) +
            L" shift=" + std::to_wstring(shift.cx) + L"," + std::to_wstring(shift.cy) +
            L" gradient=" + std::to_wstring(sh->_IsGradientForTest());
    }
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
                    Assert::IsNotNull(w[k]->_GetShadowForTest());
                    w[k]->_GetShadowForTest()->_SetUseRustForTest(k == 1);
                    w[k]->_Move(300, 200);
                    w[k]->_ResizeWindow();
                }
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
                        CShadowWindow* sh = w[k]->_GetShadowForTest();
                        switch (op)
                        {
                        case 0: w[k]->_Move(x, y); break;
                        case 1: SetWindowPos(w[k]->_GetWnd(), nullptr, x, y, cx, cy, SWP_NOZORDER | SWP_NOACTIVATE); break;
                        case 2: sh->_Show(flag); break;
                        case 3: sh->_OnOwnerWndMoved(flag); break;
                        case 4: sh->_OnSettingChange(); break;
                        case 5: SendMessage(sh->_GetWnd(), WM_PAINT, 0, 0); break;
                        case 6: SendMessage(sh->_GetWnd(), WM_APP + 1, 0, 0); break;
                        case 7: SendMessage(sh->_GetWnd(), WM_SETTINGCHANGE, 0, 0); break;
                        case 8: SetWindowPos(w[k]->_GetWnd(), nullptr, 0, 0, 0, 0,
                                    SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | (flag ? SWP_SHOWWINDOW : SWP_HIDEWINDOW)); break;
                        }
                        st[k] = ShadowState(w[k]);
                    }
                    if (st[0] != st[1])
                        Assert::Fail((L"scenario " + std::to_wstring(scenario) + L" step " + std::to_wstring(step) + L" op " + std::to_wstring(op) +
                            L"\nC++  " + st[0] + L"\nRust " + st[1]).c_str());
                    compared++;
                }
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
