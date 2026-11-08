/********************************************************************************
* CandidateProcParityTest: the candidate window's message handling, C++
* (CCandidateWindow::_WindowProcCpp) vs Rust (_WindowProcRust,
* dime-ui::candproc). Two real candidate windows with the same contents, one on
* each implementation, stacked at the same screen position, receive the same
* random message sequences (mouse moves / clicks / wheel on rows, the scroll bar
* and outside, WM_SETCURSOR, WM_VSCROLL codes, WM_MOUSEACTIVATE,
* WM_POINTERACTIVATE, WM_WINDOWPOSCHANGING with z-order, show / hide, size
* flags, WM_SHOWWINDOW, WM_ERASEBKGND, WM_CTLCOLORSCROLLBAR, WM_PAINT,
* WM_MOUSELEAVE). After every message: the return value, the selection, the
* current page, the selection callbacks, the cursor, the mouse capture, the
* TrackMouseEvent flag, the scroll bar position / enabled / visible, the
* shadow visibility, the layout members, and the WINDOWPOS flags written back.
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
#include "WindowClassTestHelper.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

#ifdef DIME_RUST_AVAILABLE

namespace
{
    struct Recorder
    {
        std::vector<int> actions;
    };

    HRESULT RecordCallback(void* pv, enum CANDWND_ACTION action)
    {
        static_cast<Recorder*>(pv)->actions.push_back(action == CANDWND_ACTION::CAND_ITEM_SELECT ? 0 : 1);
        return S_OK;
    }


    struct Side
    {
        Recorder rec;
        CCandidateRange range;
        std::vector<CCandidateListItem> items;
        CCandidateWindow* w = nullptr;
    };

    struct Observed
    {
        LRESULT result = 0;
        int selection = 0;
        int page = -1;
        size_t actions = 0;
        int lastAction = -1;
        HCURSOR cursor = nullptr;
        bool captured = false;
        BOOL tracking = FALSE;
        int sbPos = 0, sbMax = 0, sbPage = 0;
        BOOL sbEnabled = FALSE, sbVisible = FALSE, shadowVisible = FALSE;
        CCandidateWindow::LayoutStateForTest state = {};
        UINT wpFlags = 0;

        std::wstring Diff(const Observed& o) const
        {
            std::wstring d;
            auto f = [&](const wchar_t* n, long long a, long long b) {
                if (a != b) d += std::wstring(n) + L" C++=" + std::to_wstring(a) + L" Rust=" + std::to_wstring(b) + L"; ";
            };
            f(L"result", result, o.result);
            f(L"selection", selection, o.selection);
            f(L"page", page, o.page);
            f(L"actions", (long long)actions, (long long)o.actions);
            f(L"lastAction", lastAction, o.lastAction);
            f(L"cursor", (long long)(INT_PTR)cursor, (long long)(INT_PTR)o.cursor);
            f(L"captured", captured, o.captured);
            f(L"tracking", tracking, o.tracking);
            f(L"sbPos", sbPos, o.sbPos);
            f(L"sbMax", sbMax, o.sbMax);
            f(L"sbPage", sbPage, o.sbPage);
            f(L"sbEnabled", sbEnabled, o.sbEnabled);
            f(L"sbVisible", sbVisible, o.sbVisible);
            f(L"shadowVisible", shadowVisible, o.shadowVisible);
            f(L"cxTitle", state.cxTitle, o.state.cxTitle);
            f(L"cyRow", state.cyRow, o.state.cyRow);
            f(L"padding", state.padding, o.state.padding);
            f(L"wndWidth", state.wndWidth, o.state.wndWidth);
            f(L"wpFlags", wpFlags, o.wpFlags);
            return d;
        }
    };

    Observed Observe(Side& s, LRESULT result, UINT wpFlags)
    {
        Observed o;
        o.result = result;
        o.selection = s.w->_GetSelection();
        int page = -1;
        if (SUCCEEDED(s.w->_GetCurrentPage(&page))) o.page = page;
        o.actions = s.rec.actions.size();
        o.lastAction = s.rec.actions.empty() ? -1 : s.rec.actions.back();
        o.cursor = GetCursor();
        o.captured = GetCapture() == s.w->_GetWnd();
        o.tracking = s.w->_IsTrackingMouseForTest();
        if (CScrollBarWindow* sb = s.w->_GetScrollBarForTest())
        {
            CScrollInfo si;
            sb->_GetScrollInfo(&si);
            o.sbPos = si.nPos; o.sbMax = si.nMax; o.sbPage = si.nPage;
            o.sbEnabled = sb->_IsEnabled();
            o.sbVisible = IsWindowVisible(sb->_GetWnd());
        }
        if (CShadowWindow* sh = s.w->_GetShadowForTest())
            o.shadowVisible = IsWindowVisible(sh->_GetWnd());
        o.state = s.w->_GetLayoutStateForTest();
        o.wpFlags = wpFlags;
        return o;
    }
}

namespace DIMEUnitTests
{
    TEST_CLASS(CandidateProcParityTest)
    {
    public:
        TEST_METHOD(MessagesMatchCpp)
        {
            EnsureWindowClassesRegistered();
            HFONT savedFont = Global::defaultlFontHandle;
            HFONT font = CreateFont(-24, 0, 0, 0, FW_NORMAL, FALSE, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Microsoft JhengHei");
            Global::defaultlFontHandle = font;
            const std::wstring pool[] = { L"中", L"國", L"美麗", L"台灣大學", L"Wi-Fi", L"一二三四五六七八九十甲乙丙丁戊" };

            std::mt19937 rng(41);
            int compared = 0, selects = 0, cancels = 0;
            for (int scenario = 0; scenario < 24; scenario++)
            {
                int rowsInPage = 1 + (int)(rng() % 9);
                UINT wndWidth = 1 + rng() % 4;
                UINT count = rng() % 40;
                std::vector<std::wstring> texts(count);
                for (auto& t : texts) t = pool[rng() % ARRAYSIZE(pool)];

                Side sides[2];
                bool ok = true;
                for (int k = 0; k < 2; k++)
                {
                    Side& s = sides[k];
                    const wchar_t keys[] = L"1234567890";
                    for (int r = 0; r < rowsInPage; r++) s.range.Append()->CandIndex = keys[r % 10];
                    s.w = new CCandidateWindow(RecordCallback, &s.rec, &s.range, FALSE);
                    s.w->_SetUseRustProcForTest(k == 1);
                    if (!s.w->_Create(wndWidth, 12, nullptr)) { ok = false; break; }
                    Assert::IsNotNull(s.w->_GetWnd(), L"a real window (not a virtual one)");
                    s.w->_SetCandStringLength(wndWidth);
                    s.items.resize(count);
                    for (UINT i = 0; i < count; i++)
                    {
                        s.items[i]._ItemString.Set(texts[i].c_str(), texts[i].size());
                        s.w->_AddString(&s.items[i], FALSE);
                    }
                    UINT pages = count ? (count + rowsInPage - 1) / rowsInPage : 0;
                    std::vector<UINT> pageIndex(pages);
                    for (UINT p = 0; p < pages; p++) pageIndex[p] = p * rowsInPage;
                    if (pages) s.w->_SetPageIndex(pageIndex.data(), pages);
                    s.w->_SetScrollInfo(count, rowsInPage);
                    s.w->_SetFillColor(RGB(250, 250, 250));
                    s.w->_Move(300, 300);
                }
                if (!ok)
                {
                    for (Side& s : sides) delete s.w;
                    Logger::WriteMessage("SKIP: window creation failed (no GUI context)");
                    break;
                }
                // WM_CREATE ran on each implementation.
                {
                    Observed c = Observe(sides[0], 0, 0), r = Observe(sides[1], 0, 0);
                    std::wstring d = c.Diff(r);
                    Assert::IsTrue(d.empty(), (L"after create: " + d).c_str());
                }
                // Paint once so the layout members reflect the contents, then size both windows alike.
                for (Side& s : sides) { s.w->_ResizeWindow(); SendMessage(s.w->_GetWnd(), WM_PAINT, 0, 0); }

                RECT client = { 0, 0, 0, 0 };
                GetClientRect(sides[0].w->_GetWnd(), &client);
                int cw = max(1, (int)client.right), ch = max(1, (int)client.bottom);

                for (int step = 0; step < 120; step++)
                {
                    UINT msg = 0;
                    WPARAM wp = 0;
                    LPARAM lp = 0;
                    bool useWindowPos = false;
                    WINDOWPOS pos = {};
                    int x = (int)(rng() % (cw + 20)) - 10, y = (int)(rng() % (ch + 20)) - 10;
                    switch (rng() % 16)
                    {
                    case 0: case 1: case 2: msg = WM_MOUSEMOVE; lp = MAKELPARAM(x, y); break;
                    case 3: msg = WM_LBUTTONDOWN; lp = MAKELPARAM(x, y); break;
                    case 4: msg = WM_LBUTTONUP; lp = MAKELPARAM(x, y); break;
                    case 5: msg = (rng() % 2) ? WM_RBUTTONDOWN : WM_MBUTTONUP; lp = MAKELPARAM(x, y); break;
                    case 6: msg = WM_MOUSEWHEEL; wp = MAKEWPARAM(0, (rng() % 2) ? WHEEL_DELTA : (WORD)-WHEEL_DELTA); break;
                    case 7: msg = WM_VSCROLL; wp = MAKEWPARAM(rng() % 9, rng() % (count + 3)); break;
                    case 8: msg = WM_SETCURSOR; lp = MAKELPARAM(HTCLIENT, (rng() % 2) ? WM_MOUSEMOVE : WM_LBUTTONUP); break;
                    case 9:
                    {
                        static const WORD ev[] = { WM_LBUTTONDOWN, WM_RBUTTONDOWN, WM_MBUTTONDOWN, WM_MOUSEMOVE, 0 };
                        msg = WM_MOUSEACTIVATE; lp = MAKELPARAM(HTCLIENT, ev[rng() % 5]); break;
                    }
                    case 10: msg = 0x024B /* WM_POINTERACTIVATE */; break;
                    case 11:
                    {
                        msg = (rng() % 2) ? WM_WINDOWPOSCHANGING : WM_WINDOWPOSCHANGED;
                        useWindowPos = true;
                        static const UINT fl[] = { SWP_NOSIZE, SWP_NOMOVE, SWP_NOZORDER, SWP_HIDEWINDOW, SWP_SHOWWINDOW, SWP_NOREDRAW, SWP_NOACTIVATE };
                        for (UINT b : fl) if (rng() % 3 == 0) pos.flags |= b;
                        break;
                    }
                    case 12: msg = WM_SHOWWINDOW; wp = rng() % 2; break;
                    case 13: msg = (rng() % 2) ? WM_ERASEBKGND : WM_PAINT; break;
                    case 14: msg = WM_CTLCOLORSCROLLBAR; break;
                    case 15: msg = 0x02A3 /* WM_MOUSELEAVE */; break;
                    }

                    Observed obs[2];
                    for (int k = 0; k < 2; k++)
                    {
                        Side& s = sides[k];
                        ReleaseCapture();
                        SetCursor(nullptr);
                        WINDOWPOS p = pos;
                        p.hwnd = s.w->_GetWnd();
                        // Half of the z-order changes target the shadow ("don't go behind the shadow").
                        if (useWindowPos && (step % 2) && s.w->_GetShadowForTest())
                            p.hwndInsertAfter = s.w->_GetShadowForTest()->_GetWnd();
                        LRESULT r = SendMessage(s.w->_GetWnd(), msg, wp, useWindowPos ? (LPARAM)&p : lp);
                        if (msg == WM_CTLCOLORSCROLLBAR)   // each window answers with its own brush
                            r = (r != 0);
                        obs[k] = Observe(s, r, useWindowPos ? p.flags : 0);
                    }
                    ReleaseCapture();
                    std::wstring d = obs[0].Diff(obs[1]);
                    if (!d.empty())
                        Assert::Fail((L"scenario " + std::to_wstring(scenario) + L" step " + std::to_wstring(step) + L" msg 0x" +
                            std::to_wstring(msg) + L" wp=" + std::to_wstring(wp) + L" lp=" + std::to_wstring(lp) + L": " + d).c_str());
                    compared++;
                }
                for (int a : sides[0].rec.actions) (a == 0 ? selects : cancels)++;
                for (Side& s : sides) delete s.w;
            }
            Global::defaultlFontHandle = savedFont;
            DeleteObject(font);
            Logger::WriteMessage((std::to_wstring(compared) + L" messages compared, " + std::to_wstring(selects) +
                L" selections, " + std::to_wstring(cancels) + L" cancels").c_str());
            Assert::IsTrue(selects > 0 && cancels > 0, L"coverage");
        }
    };
}
#endif
