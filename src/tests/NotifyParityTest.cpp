/********************************************************************************
* NotifyParityTest: the notify window, the C++ CNotifyWindow (C++ drawing and
* behavior) vs the window owned by Rust (dime-ui::notifywin / notify, phase
* 3c-5e, through CRustNotifyWindow as UIPresenter holds it).
*  - PaintMatchesCpp: two real notify windows set up alike paint into 32-bpp
*    DIBs: the pixels, the new _cxTitle / _cyTitle and the window size
*    (_ResizeWindow) are identical over fonts, sizes, texts and colors.
*  - BehaviorMatchesCpp: two real notify windows (WM_CREATE included) through
*    random _Show with delay-show / auto-hide, both timers, clicks inside /
*    outside, right clicks, mouse moves, WM_SETCURSOR, WM_MOUSEACTIVATE,
*    WM_POINTERACTIVATE, WM_WINDOWPOSCHANGING / CHANGED, WM_SHOWWINDOW,
*    WM_PAINT, real WM_TIMER messages and moves, for the 中/英 (clickable) and
*    reverse-lookup types: the state, visibility, position and size, shadow
*    visibility, callbacks, cursor, timers started / stopped and the results.
********************************************************************************/
#include "pch.h"
#include "CppUnitTest.h"
#include <Windows.h>
#include <functional>
#include <random>
#include <string>
#include <vector>

#include "../Globals.h"
#include "../NotifyWindow.h"
#include "RustNotifyWindow.h"
#include "../BaseStructure.h"
#include "../UIConstants.h"
#include "WindowClassTestHelper.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

#ifdef DIME_RUST_AVAILABLE
namespace
{
    struct Calls
    {
        std::vector<std::wstring> log;
    };

    HRESULT RecordNotify(void* pv, enum NOTIFY_WND action, _In_ WPARAM wParam, _In_ LPARAM lParam)
    {
        static_cast<Calls*>(pv)->log.push_back(std::to_wstring((int)action) + L"," + std::to_wstring(wParam) + L"," + std::to_wstring(lParam));
        return S_OK;
    }

    // Timers started / stopped: CBaseWindow's hook for the C++ window, the Rust window's own hook.
    CBaseWindow* g_watchedCpp = nullptr;
    std::vector<std::wstring> g_timers[2];

    std::wstring TimerText(BOOL start, UINT elapse, UINT_PTR id)
    {
        return (start ? L"+" : L"-") + std::to_wstring(id) + (start ? L"@" + std::to_wstring(elapse) : L"");
    }
    void TimerHookCpp(CBaseWindow* w, BOOL start, UINT elapse, UINT_PTR id)
    {
        if (w == g_watchedCpp) g_timers[0].push_back(TimerText(start, elapse, id));
    }
    void TimerHookRust(void*, int32_t start, uint32_t elapse, uintptr_t id)
    {
        g_timers[1].push_back(TimerText(start, elapse, id));
    }

    std::vector<DWORD> PaintInto(HWND h, const std::function<void(HDC, PAINTSTRUCT*)>& paint, bool withPs)
    {
        RECT rc = { 0, 0, 0, 0 };
        GetClientRect(h, &rc);
        int cx = max(1, (int)rc.right), cy = max(1, (int)rc.bottom);
        HDC screen = GetDC(nullptr);
        HDC mem = CreateCompatibleDC(screen);
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = cx;
        bi.bmiHeader.biHeight = -cy;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP dib = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        HGDIOBJ oldBmp = SelectObject(mem, dib);
        RECT all = { 0, 0, cx, cy };
        HBRUSH sentinel = CreateSolidBrush(RGB(0x12, 0x34, 0x56));
        FillRect(mem, &all, sentinel);
        DeleteObject(sentinel);
        PAINTSTRUCT ps = {};
        ps.hdc = mem;
        ps.rcPaint = rc;
        paint(mem, withPs ? &ps : nullptr);
        GdiFlush();
        std::vector<DWORD> px((DWORD*)bits, (DWORD*)bits + (size_t)cx * cy);
        SelectObject(mem, oldBmp);
        DeleteObject(dib);
        DeleteDC(mem);
        ReleaseDC(nullptr, screen);
        return px;
    }

    // One pair: the C++ window and the Rust one, set up alike.
    struct Pair
    {
        Calls calls[2];
        CNotifyWindow* cpp = nullptr;
        CRustNotifyWindow* rust = nullptr;

        void Create(NOTIFY_TYPE type, CStringRange* text)
        {
            cpp = new CNotifyWindow(RecordNotify, &calls[0], type);   // DIME.Test.CppNotifyWindow in the Rust build
            Assert::IsTrue(cpp->_Create(12, nullptr, text) != FALSE);
            Assert::IsNotNull(cpp->_GetWnd(), L"a real C++ window (not a virtual one)");
            rust = new CRustNotifyWindow(RecordNotify, &calls[1], type);
            rust->_SetClassesForTest(RustNotifyTestClasses());
            Assert::IsTrue(rust->_Create(12, nullptr, text) != FALSE);
            Assert::IsNotNull(rust->_GetWnd(), L"a real Rust window");
        }
        HWND Wnd(int k) const { return k == 0 ? cpp->_GetWnd() : rust->_GetWnd(); }
        HWND Shadow(int k) const
        {
            if (k == 0) return cpp->_GetShadowForTest() ? cpp->_GetShadowForTest()->_GetWnd() : nullptr;
            return (HWND)dime_ui_notifywin_shadow_hwnd(rust->_GetRustWindow());
        }
        void State(int k, int st[6]) const
        {
            if (k == 0)
            {
                CNotifyWindow::StateForTest s = cpp->_GetStateForTest();
                int v[6] = { s.cxTitle, s.cyTitle, s.x, s.y, (int)s.delayShow, (int)s.timeToHide };
                memcpy(st, v, sizeof(v));
            }
            else
                dime_ui_notifywin_state(rust->_GetRustWindow(), (int32_t*)st);
        }
        ~Pair()
        {
            DestroyAndDelete(cpp);
            if (rust) { rust->_Destroy(); delete rust; }
        }
    };

    std::wstring Observe(Pair& p, int k)
    {
        int st[6] = {};
        p.State(k, st);
        HWND h = p.Wnd(k), shadow = p.Shadow(k);
        RECT rc = { 0, 0, 0, 0 };
        GetWindowRect(h, &rc);
        std::wstring s = L"title=" + std::to_wstring(st[0]) + L"x" + std::to_wstring(st[1]) +
            L" pos=" + std::to_wstring(st[2]) + L"," + std::to_wstring(st[3]) +
            L" delay=" + std::to_wstring(st[4]) + L" hide=" + std::to_wstring(st[5]) +
            L" visible=" + std::to_wstring(IsWindowVisible(h)) +
            L" rect=" + std::to_wstring(rc.left) + L"," + std::to_wstring(rc.top) + L"," + std::to_wstring(rc.right - rc.left) + L"x" + std::to_wstring(rc.bottom - rc.top) +
            L" shadow=" + std::to_wstring(shadow ? IsWindowVisible(shadow) : -1) +
            L" width=" + std::to_wstring(k == 0 ? p.cpp->_GetWidth() : p.rust->_GetWidth()) +
            L" height=" + std::to_wstring(k == 0 ? p.cpp->_GetHeight() : p.rust->_GetHeight()) + L" calls=";
        for (auto& c : p.calls[k].log) s += c + L";";
        s += L" timers=";
        for (auto& t : g_timers[k]) s += t + L" ";
        return s;
    }
}

namespace DIMEUnitTests
{
    TEST_CLASS(NotifyParityTest)
    {
    public:
        TEST_METHOD(PaintMatchesCpp)
        {
            EnsureWindowClassesRegistered();
            HFONT savedFont = Global::defaultlFontHandle;
            const wchar_t* faces[] = { L"Microsoft JhengHei", L"PMingLiU", L"Segoe UI" };
            const int heights[] = { -16, -24, -37 };
            const std::wstring pool[] = { L"中", L"英", L"全形", L"半形", L"ㄅㄆㄇ", L"a-b-c", L"反查：一二三四", L"特別碼 ABCD" };
            std::mt19937 rng(71);
            int compared = 0, sized = 0;
            for (const wchar_t* face : faces)
            for (int height : heights)
            {
                HFONT font = CreateFont(height, 0, 0, 0, FW_NORMAL, FALSE, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, face);
                Global::defaultlFontHandle = font;
                for (int s = 0; s < 8; s++)
                {
                    std::wstring text = pool[rng() % ARRAYSIZE(pool)];
                    COLORREF fg = rng() & 0xFFFFFF, bg = rng() & 0xFFFFFF, fill = rng() & 0xFFFFFF;
                    bool fillBrush = rng() % 4 != 0;
                    bool withPs = rng() % 6 != 0;
                    CStringRange sr;
                    sr.Set(text.c_str(), text.size());
                    Pair p;
                    p.Create(NOTIFY_TYPE::NOTIFY_CHN_ENG, &sr);
                    p.cpp->_SetTextColor(fg, bg);
                    p.rust->_SetTextColor(fg, bg);
                    if (fillBrush) { p.cpp->_SetFillColor(fill); p.rust->_SetFillColor(fill); }
                    std::vector<DWORD> c = PaintInto(p.Wnd(0), [&](HDC dc, PAINTSTRUCT* ps) { p.cpp->_OnPaint(dc, ps); }, withPs);
                    std::vector<DWORD> r = PaintInto(p.Wnd(1), [&](HDC dc, PAINTSTRUCT* ps) {
                        DimeRect pr = {};
                        if (ps) pr = { ps->rcPaint.left, ps->rcPaint.top, ps->rcPaint.right, ps->rcPaint.bottom };
                        dime_ui_notifywin_paint(p.rust->_GetRustWindow(), dc, ps ? &pr : nullptr);
                    }, withPs);
                    std::wstring where = std::wstring(face) + L" " + std::to_wstring(height) + L" \"" + text + L"\"";
                    int a[6] = {}, b[6] = {};
                    p.State(0, a);
                    p.State(1, b);
                    Assert::IsTrue(a[0] == b[0] && a[1] == b[1], (where + L": title size").c_str());
                    RECT ra = {}, rb = {};
                    GetWindowRect(p.Wnd(0), &ra);
                    GetWindowRect(p.Wnd(1), &rb);
                    Assert::IsTrue(ra.right - ra.left == rb.right - rb.left && ra.bottom - ra.top == rb.bottom - rb.top, (where + L": window size").c_str());
                    Assert::AreEqual(c.size(), r.size(), (where + L": bitmap size").c_str());
                    for (size_t i = 0; i < c.size(); i++)
                        if (c[i] != r[i]) Assert::Fail((where + L": pixel " + std::to_wstring(i) + L" differs").c_str());
                    if (ra.right - ra.left > 0) sized++;
                    compared++;
                }
                Global::defaultlFontHandle = savedFont;
                DeleteObject(font);
            }
            Logger::WriteMessage((std::to_wstring(compared) + L" notify paints compared, " + std::to_wstring(sized) + L" with a non-empty window").c_str());
        }

        TEST_METHOD(BehaviorMatchesCpp)
        {
            EnsureWindowClassesRegistered();
            HFONT savedFont = Global::defaultlFontHandle;
            HFONT font = CreateFont(-24, 0, 0, 0, FW_NORMAL, FALSE, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Microsoft JhengHei");
            Global::defaultlFontHandle = font;
            g_baseWindowTimerTestHook = TimerHookCpp;
            std::mt19937 rng(72);
            int compared = 0, callbacks = 0;
            const NOTIFY_TYPE types[] = { NOTIFY_TYPE::NOTIFY_CHN_ENG, NOTIFY_TYPE::NOTIFY_REVERSE_LOOKUP };
            for (int scenario = 0; scenario < 12; scenario++)
            {
                std::wstring text = (scenario % 3 == 0) ? L"英" : L"反查：中文字";
                CStringRange sr;
                sr.Set(text.c_str(), text.size());
                Pair p;
                p.Create(types[scenario % 2], (scenario % 4 == 3) ? nullptr : &sr);
                if (scenario % 4 == 3) { p.cpp->_SetString(&sr); p.rust->_SetString(&sr); }
                p.cpp->_SetTextColor(RGB(0, 0, 0), RGB(255, 255, 255));
                p.cpp->_SetFillColor(RGB(255, 255, 255));
                p.rust->_SetTextColor(RGB(0, 0, 0), RGB(255, 255, 255));
                p.rust->_SetFillColor(RGB(255, 255, 255));
                g_watchedCpp = p.cpp;
                dime_ui_notifywin_set_timer_hook(p.rust->_GetRustWindow(), TimerHookRust, nullptr);
                g_timers[0].clear();
                g_timers[1].clear();
                {
                    std::wstring a = Observe(p, 0), b = Observe(p, 1);
                    Assert::IsTrue(a == b, (L"after create\nC++  " + a + L"\nRust " + b).c_str());
                }
                for (int step = 0; step < 80; step++)
                {
                    int op = (int)(rng() % 15);
                    UINT times[] = { 0, 0, 100, 500 };
                    UINT delay = times[rng() % 4], tth = times[rng() % 4];
                    BOOL flag = rng() % 2;
                    int x = (int)(rng() % 200) - 50, y = (int)(rng() % 80) - 20;
                    UINT_PTR timerIds[] = { UI::DELAY_SHOW_TIMER_ID, UI::TIME_TO_HIDE_TIMER_ID, 4242 };
                    UINT_PTR timer = timerIds[rng() % 3];
                    int mx = 200 + (int)(rng() % 400), my = 200 + (int)(rng() % 300);
                    static const WORD ev[] = { WM_LBUTTONDOWN, WM_RBUTTONDOWN, WM_MBUTTONDOWN, WM_MOUSEMOVE };
                    WORD mev = ev[rng() % 4];
                    UINT wpFlags = 0;
                    static const UINT fl[] = { SWP_NOSIZE, SWP_NOMOVE, SWP_NOZORDER, SWP_HIDEWINDOW, SWP_SHOWWINDOW, SWP_NOREDRAW };
                    for (UINT b : fl) if (rng() % 3 == 0) wpFlags |= b;
                    std::wstring st[2];
                    LRESULT res[2] = {};
                    for (int k = 0; k < 2; k++)
                    {
                        HWND h = p.Wnd(k);
                        SetCursor(nullptr);
                        WINDOWPOS pos = {};
                        pos.hwnd = h;
                        pos.flags = wpFlags;
                        if (flag && p.Shadow(k)) pos.hwndInsertAfter = p.Shadow(k);
                        switch (op)
                        {
                        case 0: case 1:
                            if (k == 0) p.cpp->_Show(flag || op == 0, delay, tth); else p.rust->_Show(flag || op == 0, delay, tth);
                            break;
                        case 2:
                            if (k == 0) p.cpp->_OnTimerID(timer); else dime_ui_notifywin_op(p.rust->_GetRustWindow(), 4, (int32_t)timer, 0, 0);
                            break;
                        case 3: res[k] = SendMessage(h, WM_LBUTTONDOWN, 0, MAKELPARAM(x, y)); break;
                        case 4: res[k] = SendMessage(h, WM_RBUTTONDOWN, 0, MAKELPARAM(x, y)); break;
                        case 5: res[k] = SendMessage(h, WM_MOUSEMOVE, 0, MAKELPARAM(x, y)); break;
                        case 6: res[k] = SendMessage(h, WM_SETCURSOR, (WPARAM)h, MAKELPARAM(HTCLIENT, flag ? WM_MOUSEMOVE : WM_LBUTTONUP)); break;
                        case 7: res[k] = SendMessage(h, WM_MOUSEACTIVATE, 0, MAKELPARAM(HTCLIENT, mev)); break;
                        case 8: res[k] = SendMessage(h, 0x024B /* WM_POINTERACTIVATE */, 0, 0); break;
                        case 9: res[k] = SendMessage(h, flag ? WM_WINDOWPOSCHANGING : WM_WINDOWPOSCHANGED, 0, (LPARAM)&pos); break;
                        case 10: res[k] = SendMessage(h, WM_SHOWWINDOW, flag, 0); break;
                        case 11: res[k] = SendMessage(h, WM_PAINT, 0, 0); break;
                        case 12: if (k == 0) p.cpp->_Move(mx, my); else p.rust->_Move(mx, my); break;
                        case 13: res[k] = SendMessage(h, WM_LBUTTONUP, 0, MAKELPARAM(x, y)); break;
                        case 14: res[k] = SendMessage(h, WM_TIMER, timer, 0); break;   // routed to the timer object
                        }
                        st[k] = Observe(p, k) + L" res=" + std::to_wstring(res[k]) + L" cursor=" + std::to_wstring((INT_PTR)GetCursor()) +
                            L" wp=" + std::to_wstring(pos.flags);
                    }
                    if (st[0] != st[1])
                        Assert::Fail((L"scenario " + std::to_wstring(scenario) + L" step " + std::to_wstring(step) + L" op " + std::to_wstring(op) +
                            L"\nC++  " + st[0] + L"\nRust " + st[1]).c_str());
                    compared++;
                }
                callbacks += (int)p.calls[0].log.size();
                g_watchedCpp = nullptr;
                dime_ui_notifywin_set_timer_hook(p.rust->_GetRustWindow(), nullptr, nullptr);
            }
            g_baseWindowTimerTestHook = nullptr;
            Global::defaultlFontHandle = savedFont;
            DeleteObject(font);
            Logger::WriteMessage((std::to_wstring(compared) + L" notify steps compared, " + std::to_wstring(callbacks) + L" callbacks").c_str());
            Assert::IsTrue(callbacks > 0, L"coverage");
        }
    };
}
#endif
