/********************************************************************************
* ScrollBarProcParityTest: the candidate scroll bar's events, C++ vs Rust
* (dime-ui::sbproc). Two real candidate windows with the same contents; one
* scroll bar handles its events in C++, the other in Rust (the state is the
* C++ object's either way). Both receive the same random sequences: scroll
* info, _SetCurPos with every notification code, _ShiftLine / _ShiftPage /
* _ShiftPosition, clicks on the buttons / track / thumb, drags, button-up,
* hover, _SetCandidateMouseIn, the repeat and collapse timers,
* _OnScrollActivity, _Show / _Enable, owner moves, WM_MOUSELEAVE /
* WM_ERASEBKGND / WM_PAINT / WM_TIMER. After each step: the whole scroll bar
* state, enabled / visible / window rectangle / layered alpha, the mouse
* capture, the timers started and stopped, and the candidate window's
* selection and page (the scroll bar notifies it with WM_VSCROLL).
********************************************************************************/
#include "pch.h"
#include "CppUnitTest.h"
#include <Windows.h>
#include <random>
#include <string>
#include <vector>

#include "../Globals.h"
#include "../CandidateWindow.h"
#include "../BaseStructure.h"
#include "../UIConstants.h"
#include "WindowClassTestHelper.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

#ifdef DIME_RUST_AVAILABLE
namespace
{
    HRESULT NoCallback(void*, enum CANDWND_ACTION) { return S_OK; }

    struct TimerEvent
    {
        BOOL start;
        UINT elapse;
        UINT_PTR id;
        bool operator==(const TimerEvent& o) const { return start == o.start && (!start || elapse == o.elapse) && id == o.id; }
    };
    CBaseWindow* g_watched[2] = { nullptr, nullptr };
    std::vector<TimerEvent> g_timers[2];

    void TimerHook(CBaseWindow* w, BOOL start, UINT elapse, UINT_PTR id)
    {
        for (int k = 0; k < 2; k++)
            if (w == g_watched[k]) g_timers[k].push_back({ start, elapse, id });
    }

    struct Side
    {
        CCandidateRange range;
        std::vector<CCandidateListItem> items;
        CCandidateWindow* w = nullptr;
        CScrollBarWindow* sb = nullptr;
    };

    std::wstring Snapshot(Side& s, int k)
    {
        CScrollBarWindow::StateForTest st = s.sb->_GetStateForTest();
        HWND h = s.sb->_GetWnd();
        RECT rc = { 0, 0, 0, 0 };
        GetWindowRect(h, &rc);
        RECT pr = { 0, 0, 0, 0 };
        GetWindowRect(s.w->_GetWnd(), &pr);
        BYTE alpha = 0;
        COLORREF key = 0;
        DWORD flags = 0;
        GetLayeredWindowAttributes(h, &key, &alpha, &flags);
        int page = -1;
        s.w->_GetCurrentPage(&page);
        std::wstring t;
        for (const TimerEvent& e : g_timers[k])
            t += (e.start ? L"+" : L"-") + std::to_wstring(e.id) + (e.start ? L"@" + std::to_wstring(e.elapse) : L"") + L" ";
        return L"si=" + std::to_wstring(st.si.nMax) + L"/" + std::to_wstring(st.si.nPage) + L"/" + std::to_wstring(st.si.nPos) +
            L" drag=" + std::to_wstring(st.dragging) + L"," + std::to_wstring(st.dragStartY) + L"," + std::to_wstring(st.dragStartPos) +
            L" alpha=" + std::to_wstring(st.alpha) + L" fading=" + std::to_wstring(st.fading) + L" hovered=" + std::to_wstring(st.hovered) +
            L" tracking=" + std::to_wstring(st.tracking) + L" held=" + std::to_wstring(st.held) + L" repeat=" + std::to_wstring(st.repeatStarted) +
            L" enabled=" + std::to_wstring(IsWindowEnabled(h)) + L" visible=" + std::to_wstring(IsWindowVisible(h)) +
            L" rect=" + std::to_wstring(rc.left - pr.left) + L"," + std::to_wstring(rc.top - pr.top) + L"," + std::to_wstring(rc.right - rc.left) +
            L"x" + std::to_wstring(rc.bottom - rc.top) + L" layeredAlpha=" + std::to_wstring(alpha) +
            L" capture=" + std::to_wstring(GetCapture() == s.w->_GetWnd()) +
            L" sel=" + std::to_wstring(s.w->_GetSelection()) + L" page=" + std::to_wstring(page) + L" timers=" + t;
    }
}

namespace DIMEUnitTests
{
    TEST_CLASS(ScrollBarProcParityTest)
    {
    public:
        TEST_METHOD(EventsMatchCpp)
        {
            EnsureWindowClassesRegistered();
            HFONT savedFont = Global::defaultlFontHandle;
            HFONT font = CreateFont(-24, 0, 0, 0, FW_NORMAL, FALSE, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Microsoft JhengHei");
            Global::defaultlFontHandle = font;
            g_baseWindowTimerTestHook = TimerHook;

            std::mt19937 rng(53);
            int compared = 0, notified = 0;
            for (int scenario = 0; scenario < 20; scenario++)
            {
                int rowsInPage = 1 + (int)(rng() % 9);
                UINT count = rng() % 60;
                bool shown = scenario % 2 == 0;
                Side sides[2];
                for (int k = 0; k < 2; k++)
                {
                    Side& s = sides[k];
                    for (int r = 0; r < rowsInPage; r++) s.range.Append()->CandIndex = L'1' + (wchar_t)r;
                    s.w = new CCandidateWindow(NoCallback, nullptr, &s.range, FALSE);
                    Assert::IsTrue(s.w->_Create(2, 12, nullptr) != FALSE);
                    Assert::IsNotNull(s.w->_GetWnd(), L"a real window (not a virtual one)");
                    s.sb = s.w->_GetScrollBarForTest();
                    Assert::IsNotNull(s.sb);
                    s.sb->_SetUseRustForTest(k == 1);
                    s.items.resize(count);
                    for (UINT i = 0; i < count; i++)
                    {
                        s.items[i]._ItemString.Set(L"中", 1);
                        s.w->_AddString(&s.items[i], FALSE);
                    }
                    UINT pages = count ? (count + rowsInPage - 1) / rowsInPage : 0;
                    std::vector<UINT> pageIndex(pages);
                    for (UINT p = 0; p < pages; p++) pageIndex[p] = p * rowsInPage;
                    if (pages) s.w->_SetPageIndex(pageIndex.data(), pages);
                    s.w->_Move(400, 300);
                    s.w->_ResizeWindow();
                    g_watched[k] = s.sb;
                    g_timers[k].clear();
                }
                // Same starting point: _SetScrollInfo as the presenter does.
                for (int k = 0; k < 2; k++)
                {
                    sides[k].w->_SetScrollInfo(count, rowsInPage);
                    if (shown) sides[k].w->_Show(TRUE);
                    ReleaseCapture();   // _Show(TRUE) captures the mouse; start both sides without it
                }

                HWND capture[2] = { nullptr, nullptr };
                RECT sbClient = { 0, 0, 0, 0 };
                GetClientRect(sides[0].sb->_GetWnd(), &sbClient);
                int sw = max(1, (int)sbClient.right), sh = max(1, (int)sbClient.bottom);

                for (int step = 0; step < 150; step++)
                {
                    int op = (int)(rng() % 20);
                    int a = (int)(rng() % (count + 10)) - 5, b = (int)(rng() % 8) - 1;
                    POINT pt = { (LONG)(rng() % (sw + 6)) - 3, (LONG)(rng() % (sh + 10)) - 5 };
                    CScrollInfo si;
                    si.nMax = (int)(rng() % 60); si.nPage = 1 + (int)(rng() % 10); si.nPos = (int)(rng() % 70) - 5;
                    UINT_PTR timers[] = { BTNREPEAT_TIMER_ID, UI::SCROLLBAR_COLLAPSE_TIMER_ID, FADE_TIMER_ID, 12345 };
                    UINT_PTR timer = timers[rng() % 4];
                    BOOL flag = rng() % 2;
                    bool setInfo = rng() % 4 == 0;
                    int radius = (int)(rng() % 3) - 1;
                    UINT paintMsg = (rng() % 2) ? WM_ERASEBKGND : WM_PAINT;

                    std::wstring snap[2];
                    for (int k = 0; k < 2; k++)
                    {
                        Side& s = sides[k];
                        CScrollBarWindow* sb = s.sb;
                        // Mouse capture is global: give each side the capture it left.
                        if (capture[k]) SetCapture(capture[k]); else ReleaseCapture();
                        switch (op)
                        {
                        case 0: if (setInfo) sb->_SetScrollInfo(&si); break;
                        case 1: sb->_SetCurPos(a, b); break;
                        case 2: sb->_ShiftLine(b - 2, flag); break;
                        case 3: sb->_ShiftPage(b - 2, flag); break;
                        case 4: sb->_ShiftPosition(a, flag); break;
                        case 5: case 6: sb->_OnLButtonDown(pt); break;
                        case 7: sb->_OnLButtonUp(pt); break;
                        case 8: case 9: sb->_OnMouseMove(pt); break;
                        case 10: sb->_SetCandidateMouseIn(flag); break;
                        case 11: sb->_OnTimerID(timer); break;
                        case 12: sb->_OnScrollActivity(); break;
                        case 13: sb->_Show(flag); break;
                        case 14: sb->_Enable(flag); break;
                        case 15: sb->_OnOwnerWndMoved(flag); break;
                        case 16: SendMessage(sb->_GetWnd(), 0x02A3 /* WM_MOUSELEAVE */, 0, 0); break;
                        case 17: SendMessage(sb->_GetWnd(), paintMsg, 0, 0); break;
                        case 18: SendMessage(sb->_GetWnd(), WM_TIMER, timer, 0); break;
                        case 19: sb->_SetSelectionHighlight({ 0, 0, 5, 5 }, radius, 0); break;
                        }
                        snap[k] = Snapshot(s, k);
                        capture[k] = GetCapture();
                    }
                    ReleaseCapture();
                    if (snap[0] != snap[1])
                        Assert::Fail((L"scenario " + std::to_wstring(scenario) + L" step " + std::to_wstring(step) + L" op " + std::to_wstring(op) +
                            L"\nC++  " + snap[0] + L"\nRust " + snap[1]).c_str());
                    if (sides[0].w->_GetSelection() != 0) notified++;
                    compared++;
                }
                for (int k = 0; k < 2; k++)
                {
                    g_watched[k] = nullptr;
                    ReleaseCapture();
                    delete sides[k].w;
                }
            }
            g_baseWindowTimerTestHook = nullptr;
            Global::defaultlFontHandle = savedFont;
            DeleteObject(font);
            Logger::WriteMessage((std::to_wstring(compared) + L" scroll bar steps compared, " + std::to_wstring(notified) +
                L" with the candidate selection moved by the scroll bar").c_str());
            Assert::IsTrue(notified > 0, L"coverage");
        }
    };
}
#endif
