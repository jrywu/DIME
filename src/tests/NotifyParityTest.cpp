/********************************************************************************
* NotifyParityTest: the notify window, C++ vs Rust (dime-ui::notify).
*  - PaintMatchesCpp: two real notify windows set up alike, one painted by
*    _OnPaintCpp and one by _OnPaintRust into 32-bpp DIBs: the pixels, the new
*    _cxTitle / _cyTitle and the window size (_ResizeWindow) are identical over
*    fonts, sizes, texts and colors.
*  - BehaviorMatchesCpp: two real notify windows, one per implementation
*    (WM_CREATE included), through random _Show with delay-show / auto-hide,
*    both timers, clicks inside / outside, right clicks, mouse moves,
*    WM_SETCURSOR, WM_MOUSEACTIVATE, WM_POINTERACTIVATE, WM_WINDOWPOSCHANGING /
*    CHANGED, WM_SHOWWINDOW, WM_PAINT and moves, for the 中/英 (clickable) and
*    reverse-lookup types: the state, visibility, position and size, shadow
*    visibility, callbacks, cursor, timers started / stopped and the results.
********************************************************************************/
#include "pch.h"
#include "CppUnitTest.h"
#include <Windows.h>
#include <random>
#include <string>
#include <vector>

#include "../Globals.h"
#include "../NotifyWindow.h"
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

    CBaseWindow* g_watchedN[2] = { nullptr, nullptr };
    std::vector<std::wstring> g_timersN[2];

    void TimerHookN(CBaseWindow* w, BOOL start, UINT elapse, UINT_PTR id)
    {
        for (int k = 0; k < 2; k++)
            if (w == g_watchedN[k])
                g_timersN[k].push_back((start ? L"+" : L"-") + std::to_wstring(id) + (start ? L"@" + std::to_wstring(elapse) : L""));
    }

    std::vector<DWORD> PaintInto(CNotifyWindow* w, bool rust, bool withPs)
    {
        RECT rc = { 0, 0, 0, 0 };
        GetClientRect(w->_GetWnd(), &rc);
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
        PAINTSTRUCT* pps = withPs ? &ps : nullptr;
        if (rust) w->_OnPaintRust(mem, pps);
        else w->_OnPaintCpp(mem, pps);
        GdiFlush();
        std::vector<DWORD> px((DWORD*)bits, (DWORD*)bits + (size_t)cx * cy);
        SelectObject(mem, oldBmp);
        DeleteObject(dib);
        DeleteDC(mem);
        ReleaseDC(nullptr, screen);
        return px;
    }

    std::wstring State(CNotifyWindow* w, Calls& calls, int k)
    {
        CNotifyWindow::StateForTest st = w->_GetStateForTest();
        RECT rc = { 0, 0, 0, 0 };
        GetWindowRect(w->_GetWnd(), &rc);
        BOOL shadowVisible = w->_GetShadowForTest() ? IsWindowVisible(w->_GetShadowForTest()->_GetWnd()) : -1;
        std::wstring s = L"title=" + std::to_wstring(st.cxTitle) + L"x" + std::to_wstring(st.cyTitle) +
            L" pos=" + std::to_wstring(st.x) + L"," + std::to_wstring(st.y) +
            L" delay=" + std::to_wstring(st.delayShow) + L" hide=" + std::to_wstring(st.timeToHide) +
            L" visible=" + std::to_wstring(IsWindowVisible(w->_GetWnd())) +
            L" rect=" + std::to_wstring(rc.left) + L"," + std::to_wstring(rc.top) + L"," + std::to_wstring(rc.right - rc.left) + L"x" + std::to_wstring(rc.bottom - rc.top) +
            L" shadow=" + std::to_wstring(shadowVisible) + L" calls=";
        for (auto& c : calls.log) s += c + L";";
        s += L" timers=";
        for (auto& t : g_timersN[k]) s += t + L" ";
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
                    Calls calls[2];
                    CNotifyWindow* w[2] = {};
                    for (int k = 0; k < 2; k++)
                    {
                        w[k] = new CNotifyWindow(RecordNotify, &calls[k], NOTIFY_TYPE::NOTIFY_CHN_ENG);
                        Assert::IsTrue(w[k]->_Create(12, nullptr, &sr) != FALSE);
                        Assert::IsNotNull(w[k]->_GetWnd(), L"a real window (not a virtual one)");
                        w[k]->_SetTextColor(fg, bg);
                        if (fillBrush) w[k]->_SetFillColor(fill);
                    }
                    std::vector<DWORD> c = PaintInto(w[0], false, withPs);
                    std::vector<DWORD> r = PaintInto(w[1], true, withPs);
                    std::wstring where = std::wstring(face) + L" " + std::to_wstring(height) + L" \"" + text + L"\"";
                    CNotifyWindow::StateForTest a = w[0]->_GetStateForTest(), b = w[1]->_GetStateForTest();
                    Assert::IsTrue(a.cxTitle == b.cxTitle && a.cyTitle == b.cyTitle, (where + L": title size").c_str());
                    RECT ra = {}, rb = {};
                    GetWindowRect(w[0]->_GetWnd(), &ra);
                    GetWindowRect(w[1]->_GetWnd(), &rb);
                    Assert::IsTrue(ra.right - ra.left == rb.right - rb.left && ra.bottom - ra.top == rb.bottom - rb.top, (where + L": window size").c_str());
                    Assert::AreEqual(c.size(), r.size(), (where + L": bitmap size").c_str());
                    for (size_t i = 0; i < c.size(); i++)
                        if (c[i] != r[i]) Assert::Fail((where + L": pixel " + std::to_wstring(i) + L" differs").c_str());
                    if (ra.right - ra.left > 0) sized++;
                    compared++;
                    for (int k = 0; k < 2; k++) DestroyAndDelete(w[k]);
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
            g_baseWindowTimerTestHook = TimerHookN;
            std::mt19937 rng(72);
            int compared = 0, callbacks = 0;
            const NOTIFY_TYPE types[] = { NOTIFY_TYPE::NOTIFY_CHN_ENG, NOTIFY_TYPE::NOTIFY_REVERSE_LOOKUP };
            for (int scenario = 0; scenario < 12; scenario++)
            {
                std::wstring text = (scenario % 3 == 0) ? L"英" : L"反查：中文字";
                CStringRange sr;
                sr.Set(text.c_str(), text.size());
                Calls calls[2];
                CNotifyWindow* w[2] = {};
                for (int k = 0; k < 2; k++)
                {
                    w[k] = new CNotifyWindow(RecordNotify, &calls[k], types[scenario % 2]);
                    w[k]->_SetUseRustForTest(k == 1);   // before _Create: WM_CREATE too
                    Assert::IsTrue(w[k]->_Create(12, nullptr, (scenario % 4 == 3) ? nullptr : &sr) != FALSE);
                    Assert::IsNotNull(w[k]->_GetWnd(), L"a real window (not a virtual one)");
                    if (scenario % 4 == 3) w[k]->_SetString(&sr);
                    w[k]->_SetTextColor(RGB(0, 0, 0), RGB(255, 255, 255));
                    w[k]->_SetFillColor(RGB(255, 255, 255));
                    g_watchedN[k] = w[k];
                    g_timersN[k].clear();
                }
                {
                    std::wstring a = State(w[0], calls[0], 0), b = State(w[1], calls[1], 1);
                    Assert::IsTrue(a == b, (L"after create\nC++  " + a + L"\nRust " + b).c_str());
                }
                for (int step = 0; step < 80; step++)
                {
                    int op = (int)(rng() % 14);
                    UINT times[] = { 0, 0, 100, 500 };
                    UINT delay = times[rng() % 4], tth = times[rng() % 4];
                    BOOL flag = rng() % 2;
                    int x = (int)(rng() % 200) - 50, y = (int)(rng() % 80) - 20;
                    UINT_PTR timers[] = { UI::DELAY_SHOW_TIMER_ID, UI::TIME_TO_HIDE_TIMER_ID, 4242 };
                    UINT_PTR timer = timers[rng() % 3];
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
                        CNotifyWindow* n = w[k];
                        HWND h = n->_GetWnd();
                        SetCursor(nullptr);
                        WINDOWPOS pos = {};
                        pos.hwnd = h;
                        pos.flags = wpFlags;
                        if (flag && n->_GetShadowForTest()) pos.hwndInsertAfter = n->_GetShadowForTest()->_GetWnd();
                        switch (op)
                        {
                        case 0: case 1: n->_Show(flag || op == 0, delay, tth); break;
                        case 2: n->_OnTimerID(timer); break;
                        case 3: res[k] = SendMessage(h, WM_LBUTTONDOWN, 0, MAKELPARAM(x, y)); break;
                        case 4: res[k] = SendMessage(h, WM_RBUTTONDOWN, 0, MAKELPARAM(x, y)); break;
                        case 5: res[k] = SendMessage(h, WM_MOUSEMOVE, 0, MAKELPARAM(x, y)); break;
                        case 6: res[k] = SendMessage(h, WM_SETCURSOR, (WPARAM)h, MAKELPARAM(HTCLIENT, flag ? WM_MOUSEMOVE : WM_LBUTTONUP)); break;
                        case 7: res[k] = SendMessage(h, WM_MOUSEACTIVATE, 0, MAKELPARAM(HTCLIENT, mev)); break;
                        case 8: res[k] = SendMessage(h, 0x024B /* WM_POINTERACTIVATE */, 0, 0); break;
                        case 9: res[k] = SendMessage(h, flag ? WM_WINDOWPOSCHANGING : WM_WINDOWPOSCHANGED, 0, (LPARAM)&pos); break;
                        case 10: res[k] = SendMessage(h, WM_SHOWWINDOW, flag, 0); break;
                        case 11: res[k] = SendMessage(h, WM_PAINT, 0, 0); break;
                        case 12: n->_Move(mx, my); break;
                        case 13: res[k] = SendMessage(h, WM_LBUTTONUP, 0, MAKELPARAM(x, y)); break;
                        }
                        st[k] = State(n, calls[k], k) + L" res=" + std::to_wstring(res[k]) + L" cursor=" + std::to_wstring((INT_PTR)GetCursor()) +
                            L" wp=" + std::to_wstring(pos.flags);
                    }
                    if (st[0] != st[1])
                        Assert::Fail((L"scenario " + std::to_wstring(scenario) + L" step " + std::to_wstring(step) + L" op " + std::to_wstring(op) +
                            L"\nC++  " + st[0] + L"\nRust " + st[1]).c_str());
                    compared++;
                }
                callbacks += (int)calls[0].log.size();
                for (int k = 0; k < 2; k++) { g_watchedN[k] = nullptr; DestroyAndDelete(w[k]); }
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
