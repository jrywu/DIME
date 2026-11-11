/********************************************************************************
* CandidateWindowParityTest: the candidate window owned by Rust (dime-ui::candwin,
* phase 3c-5d, through CRustCandidateWindow as UIPresenter holds it) vs the C++
* CCandidateWindow (with a C++ scroll bar), side by side with the same contents.
*  - PaintMatchesCpp: pixels, layout members, window size, scroll bar highlight.
*  - MessagesMatchCpp: random messages (mouse on rows / the scroll bar / outside,
*    right / middle clicks, wheel, WM_SETCURSOR, WM_VSCROLL, activation,
*    WINDOWPOS, show, WM_PAINT, ...):
*    results, selection, page, callbacks, cursor, capture, mouse tracking,
*    scroll bar state / visibility, shadow visibility, layout members.
*  - OperationsMatchCpp: _Show / _Move / _ResizeWindow / _SetCandStringLength /
*    colors / fill / _SetScrollInfo / scroll bar shifts / _GetWidth /
*    _GetWindowExtent: window and child rectangles, visibility, state.
********************************************************************************/
#include "pch.h"
#include "CppUnitTest.h"
#include <Windows.h>
#include <functional>
#include <random>
#include <string>
#include <vector>

#include "../Globals.h"
#include "../Config.h"
#include "../CandidateWindow.h"
#include "RustCandidateWindow.h"
#include "../BaseStructure.h"
#include "../Define.h"
#include "WindowClassTestHelper.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

#ifdef DIME_RUST_AVAILABLE
namespace
{
    struct Recorder { std::vector<int> actions; };

    HRESULT RecordAction(void* pv, enum CANDWND_ACTION action)
    {
        static_cast<Recorder*>(pv)->actions.push_back(action == CANDWND_ACTION::CAND_ITEM_SELECT ? 0 : 1);
        return S_OK;
    }

    // The Rust window through CRustCandidateWindow, as UIPresenter holds it (the model, the
    // DimeCandModel callbacks and the scroll bar sync are the production ones).
    struct RustSide
    {
        CCandidateRange range;
        std::vector<CCandidateListItem> items;
        Recorder rec;
        std::unique_ptr<CRustCandidateWindow> w;
        DimeCandidate* c = nullptr;
        CCandidateListModel* model = nullptr;

        bool Create(UINT wndWidth, const DimeCandColors& colors, COLORREF fill)
        {
            w = std::make_unique<CRustCandidateWindow>(RecordAction, &rec, &range, FALSE);
            w->_SetClassesForTest(RustCandidateTestClasses());
            if (!w->_Create(wndWidth, 12, nullptr)) return false;
            c = w->_GetRustWindow();
            model = w->_GetModel();
            w->_SetNumberColor(colors.number, colors.number_bk);
            w->_SetTextColor(colors.text, colors.bk);
            w->_SetSelectedTextColor(colors.selected_text, colors.selected_bk);
            w->_SetFillColor(fill);
            return true;
        }
        HWND Wnd() const { return w->_GetWnd(); }
        ~RustSide() { if (w) w->_Destroy(); }
    };

    struct Pair
    {
        CCandidateRange cppRange;
        std::vector<CCandidateListItem> cppItems;
        Recorder cppRec;
        CCandidateWindow* cpp = nullptr;
        RustSide rust;

        void Setup(std::mt19937& rng, int rowsInPage, UINT wndWidth, const std::vector<std::wstring>& texts)
        {
            const wchar_t keys[] = L"1234567890";
            for (int r = 0; r < rowsInPage; r++)
            {
                cppRange.Append()->CandIndex = keys[r % 10];
                rust.range.Append()->CandIndex = keys[r % 10];
            }
            cpp = new CCandidateWindow(RecordAction, &cppRec, &cppRange, FALSE);
            cpp->_SetScrollBarForTest(FALSE, CppScrollBarTestAtom());
            DimeCandColors colors = { (uint32_t)(rng() & 0xFFFFFF), (uint32_t)(rng() & 0xFFFFFF), (uint32_t)(rng() & 0xFFFFFF),
                                      (uint32_t)(rng() & 0xFFFFFF), (uint32_t)(rng() & 0xFFFFFF), (uint32_t)(rng() & 0xFFFFFF) };
            COLORREF fill = (COLORREF)(rng() & 0xFFFFFF);
            Assert::IsTrue(cpp->_Create(wndWidth, 12, nullptr) != FALSE);
            Assert::IsNotNull(cpp->_GetWnd(), L"a real C++ window");
            cpp->_SetNumberColor(colors.number, colors.number_bk);
            cpp->_SetTextColor(colors.text, colors.bk);
            cpp->_SetSelectedTextColor(colors.selected_text, colors.selected_bk);
            cpp->_SetFillColor(fill);
            Assert::IsTrue(rust.Create(wndWidth, colors, fill), L"the Rust window");

            UINT count = (UINT)texts.size();
            cppItems.resize(count);
            rust.items.resize(count);
            for (UINT i = 0; i < count; i++)
            {
                cppItems[i]._ItemString.Set(texts[i].c_str(), texts[i].size());
                rust.items[i]._ItemString.Set(texts[i].c_str(), texts[i].size());
                cpp->_AddString(&cppItems[i], FALSE);
                rust.w->_AddString(&rust.items[i], FALSE);
            }
            UINT pages = count ? (count + rowsInPage - 1) / rowsInPage : 0;
            std::vector<UINT> pageIndex(pages);
            for (UINT p = 0; p < pages; p++) pageIndex[p] = p * rowsInPage;
            if (pages)
            {
                cpp->_SetPageIndex(pageIndex.data(), pages);
                rust.w->_SetPageIndex(pageIndex.data(), pages);
            }
            cpp->_SetScrollInfo(count, rowsInPage);
            rust.w->_SetScrollInfo((int)count, rowsInPage);
            cpp->_Move(300, 300);
            rust.w->_Move(300, 300);
            cpp->_ResizeWindow();
            rust.w->_ResizeWindow();
        }
        ~Pair() { DestroyAndDelete(cpp); }
    };

    std::wstring R(const RECT& r)
    {
        return L"(" + std::to_wstring(r.left) + L"," + std::to_wstring(r.top) + L"," + std::to_wstring(r.right) + L"," + std::to_wstring(r.bottom) + L")";
    }
    RECT Rel(HWND h, const RECT& owner)
    {
        RECT r = { 0, 0, 0, 0 };
        if (h) GetWindowRect(h, &r);
        return { r.left - owner.left, r.top - owner.top, r.right - owner.left, r.bottom - owner.top };
    }

    // Everything observable of one side.
    std::wstring ObserveCpp(Pair& p)
    {
        CCandidateWindow* w = p.cpp;
        RECT wr = { 0, 0, 0, 0 };
        GetWindowRect(w->_GetWnd(), &wr);
        CCandidateWindow::LayoutStateForTest st = w->_GetLayoutStateForTest();
        IDimeScrollBar* sb = w->_GetScrollBarForTest();
        SbStateForTest ss = sb ? sb->SbGetStateForTest() : SbStateForTest{};
        int page = -1;
        w->_GetCurrentPage(&page);
        std::wstring a;
        for (int x : p.cppRec.actions) a += std::to_wstring(x);
        HWND shadow = w->_GetShadowForTest() ? w->_GetShadowForTest()->_GetWnd() : nullptr;
        return L"rect=" + R(wr) + L" visible=" + std::to_wstring(IsWindowVisible(w->_GetWnd())) +
            L" layout=" + std::to_wstring(st.cxTitle) + L"," + std::to_wstring(st.cyRow) + L"," + std::to_wstring(st.padding) + L"," + std::to_wstring(st.wndWidth) +
            L" width=" + std::to_wstring(w->_GetWidth()) +
            L" sel=" + std::to_wstring(w->_GetSelection()) + L" page=" + std::to_wstring(page) + L" actions=" + a +
            L" capture=" + std::to_wstring(GetCapture() == w->_GetWnd()) + L" tracking=" + std::to_wstring(w->_IsTrackingMouseForTest()) +
            L" sb=" + R(Rel(sb ? sb->SbWnd() : nullptr, wr)) + L"/" + std::to_wstring(ss.si.nMax) + L"/" + std::to_wstring(ss.si.nPage) + L"/" + std::to_wstring(ss.si.nPos) +
            L" sbEnabled=" + std::to_wstring(sb ? sb->SbIsEnabled() : -1) + L" sbVisible=" + std::to_wstring(sb ? IsWindowVisible(sb->SbWnd()) : -1) +
            L" sbHover=" + std::to_wstring(ss.hovered) + L" sbDrag=" + std::to_wstring(ss.dragging) + L" sbAlpha=" + std::to_wstring(ss.alpha) +
            L" shadow=" + R(Rel(shadow, wr)) + L"/" + std::to_wstring(shadow ? IsWindowVisible(shadow) : -1);
    }
    std::wstring ObserveRust(Pair& p)
    {
        DimeCandidate* c = p.rust.c;
        HWND h = p.rust.Wnd();
        RECT wr = { 0, 0, 0, 0 };
        GetWindowRect(h, &wr);
        int32_t st[4] = {};
        dime_ui_candidate_layout_state(c, 0, st);
        DimeSbSnapshot ss = {};
        DimeRect hl = {};
        int32_t radius = 0;
        uint32_t color = 0;
        bool hasSb = dime_ui_candidate_sb_state(c, &ss, &hl, &radius, &color) != 0;
        HWND sb = (HWND)dime_ui_candidate_child(c, 0);
        HWND shadow = (HWND)dime_ui_candidate_child(c, 1);
        int page = -1;
        p.rust.model->GetCurrentPage(&page);
        std::wstring a;
        for (int x : p.rust.rec.actions) a += std::to_wstring(x);
        return L"rect=" + R(wr) + L" visible=" + std::to_wstring(IsWindowVisible(h)) +
            L" layout=" + std::to_wstring(st[0]) + L"," + std::to_wstring(st[1]) + L"," + std::to_wstring(st[2]) + L"," + std::to_wstring(st[3]) +
            L" width=" + std::to_wstring(dime_ui_candidate_width(c)) +
            L" sel=" + std::to_wstring(p.rust.model->Selection()) + L" page=" + std::to_wstring(page) + L" actions=" + a +
            L" capture=" + std::to_wstring(GetCapture() == h) + L" tracking=" + std::to_wstring(dime_ui_candidate_child(c, 2) != nullptr) +
            L" sb=" + R(Rel(sb, wr)) + L"/" + std::to_wstring(ss.si.max) + L"/" + std::to_wstring(ss.si.page) + L"/" + std::to_wstring(ss.si.pos) +
            L" sbEnabled=" + std::to_wstring(hasSb ? (IsWindowEnabled(sb) ? 1 : 0) : -1) + L" sbVisible=" + std::to_wstring(hasSb ? IsWindowVisible(sb) : -1) +
            L" sbHover=" + std::to_wstring(ss.hovered) + L" sbDrag=" + std::to_wstring(ss.dragging) + L" sbAlpha=" + std::to_wstring(ss.alpha) +
            L" shadow=" + R(Rel(shadow, wr)) + L"/" + std::to_wstring(shadow ? IsWindowVisible(shadow) : -1);
    }

    std::vector<DWORD> PaintInto(int cx, int cy, const std::function<void(HDC)>& paint)
    {
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
        paint(mem);
        GdiFlush();
        std::vector<DWORD> px((DWORD*)bits, (DWORD*)bits + (size_t)cx * cy);
        SelectObject(mem, oldBmp);
        DeleteObject(dib);
        DeleteDC(mem);
        ReleaseDC(nullptr, screen);
        return px;
    }

    const std::wstring kPool[] = { L"中", L"國", L"美麗", L"台灣大學", L"Wi-Fi", L"一二三四五六七八九十甲乙丙丁戊",
                                   L"abcdefghijklmnopqrstuvwxyz", L"ㄅㄆㄇ" };
}

namespace DIMEUnitTests
{
    TEST_CLASS(CandidateWindowParityTest)
    {
    public:
        TEST_METHOD_INITIALIZE(Init)
        {
            EnsureWindowClassesRegistered();
            savedFont = Global::defaultlFontHandle;
            font = CreateFont(-24, 0, 0, 0, FW_NORMAL, FALSE, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Microsoft JhengHei");
            Global::defaultlFontHandle = font;
        }
        TEST_METHOD_CLEANUP(Cleanup)
        {
            ReleaseCapture();
            Global::defaultlFontHandle = savedFont;
            DeleteObject(font);
        }

        TEST_METHOD(PaintMatchesCpp)
        {
            std::mt19937 rng(81);
            int compared = 0;
            for (int s = 0; s < 40; s++)
            {
                int rows = 1 + (int)(rng() % 9);
                UINT width = 1 + rng() % 5;
                std::vector<std::wstring> texts(rng() % 30);
                for (auto& t : texts) t = kPool[rng() % ARRAYSIZE(kPool)];
                Pair p;
                p.Setup(rng, rows, width, texts);
                for (int v = 0; v < 3; v++)
                {
                    if (!texts.empty())
                    {
                        int sel = (int)(rng() % texts.size());
                        p.cpp->_SetSelection(sel, FALSE);
                        p.rust.w->_SetSelection(sel, FALSE);
                    }
                    RECT rc = { 0, 0, 0, 0 };
                    GetClientRect(p.cpp->_GetWnd(), &rc);
                    int cx = max(1, (int)rc.right), cy = max(1, (int)rc.bottom);
                    std::vector<DWORD> c = PaintInto(cx, cy, [&](HDC dc) {
                        PAINTSTRUCT ps = {};
                        ps.hdc = dc;
                        ps.rcPaint = rc;
                        p.cpp->_OnPaint(dc, &ps);
                    });
                    std::vector<DWORD> r = PaintInto(cx, cy, [&](HDC dc) {
                        DimeRect pr = { rc.left, rc.top, rc.right, rc.bottom };
                        dime_ui_candidate_paint(p.rust.c, dc, &pr);
                    });
                    std::wstring oc = ObserveCpp(p), orr = ObserveRust(p);
                    if (oc != orr)
                        Assert::Fail((L"paint " + std::to_wstring(s) + L"/" + std::to_wstring(v) + L"\nC++  " + oc + L"\nRust " + orr).c_str());
                    for (size_t i = 0; i < c.size(); i++)
                        if (c[i] != r[i]) Assert::Fail((L"paint " + std::to_wstring(s) + L"/" + std::to_wstring(v) + L": pixel " + std::to_wstring(i)).c_str());
                    compared++;
                }
            }
            Logger::WriteMessage((std::to_wstring(compared) + L" candidate paints compared").c_str());
        }

        TEST_METHOD(MessagesMatchCpp)
        {
            std::mt19937 rng(82);
            int compared = 0, selects = 0, cancels = 0;
            for (int scenario = 0; scenario < 20; scenario++)
            {
                std::vector<std::wstring> texts(rng() % 40);
                for (auto& t : texts) t = kPool[rng() % ARRAYSIZE(kPool)];
                Pair p;
                p.Setup(rng, 1 + (int)(rng() % 9), 1 + rng() % 4, texts);
                {
                    std::wstring a = ObserveCpp(p), b = ObserveRust(p);
                    Assert::IsTrue(a == b, (L"after create\nC++  " + a + L"\nRust " + b).c_str());
                }
                RECT client = { 0, 0, 0, 0 };
                GetClientRect(p.cpp->_GetWnd(), &client);
                int cw = max(1, (int)client.right), ch = max(1, (int)client.bottom);
                HWND capture[2] = { nullptr, nullptr };
                for (int step = 0; step < 100; step++)
                {
                    UINT msg = 0;
                    WPARAM wp = 0;
                    LPARAM lp = 0;
                    bool useWindowPos = false;
                    WINDOWPOS pos = {};
                    int x = (int)(rng() % (cw + 20)) - 10, y = (int)(rng() % (ch + 20)) - 10;
                    switch (rng() % 16)
                    {
                    case 0: case 1: msg = WM_MOUSEMOVE; lp = MAKELPARAM(x, y); break;
                    case 2: msg = WM_LBUTTONDOWN; lp = MAKELPARAM(x, y); break;
                    case 3: msg = WM_LBUTTONUP; lp = MAKELPARAM(x, y); break;
                    case 4: msg = WM_MOUSEWHEEL; wp = MAKEWPARAM(0, (rng() % 2) ? WHEEL_DELTA : (WORD)-WHEEL_DELTA); break;
                    case 5: msg = WM_VSCROLL; wp = MAKEWPARAM(rng() % 9, rng() % (texts.size() + 3)); break;
                    case 6: msg = WM_SETCURSOR; lp = MAKELPARAM(HTCLIENT, (rng() % 2) ? WM_MOUSEMOVE : WM_LBUTTONUP); break;
                    case 7:
                    {
                        static const WORD ev[] = { WM_LBUTTONDOWN, WM_RBUTTONDOWN, WM_MBUTTONDOWN, WM_MOUSEMOVE };
                        msg = WM_MOUSEACTIVATE; lp = MAKELPARAM(HTCLIENT, ev[rng() % 4]); break;
                    }
                    case 8:
                    {
                        msg = (rng() % 2) ? WM_WINDOWPOSCHANGING : WM_WINDOWPOSCHANGED;
                        useWindowPos = true;
                        static const UINT fl[] = { SWP_NOSIZE, SWP_NOMOVE, SWP_NOZORDER, SWP_HIDEWINDOW, SWP_SHOWWINDOW, SWP_NOREDRAW };
                        for (UINT b : fl) if (rng() % 3 == 0) pos.flags |= b;
                        break;
                    }
                    case 9: msg = WM_SHOWWINDOW; wp = rng() % 2; break;
                    case 10: msg = WM_ERASEBKGND; break;
                    case 11: msg = 0x02A3 /* WM_MOUSELEAVE */; break;
                    case 12: msg = WM_CTLCOLORSCROLLBAR; break;
                    case 13: msg = 0x024B /* WM_POINTERACTIVATE */; break;
                    case 14: msg = (rng() % 2) ? WM_RBUTTONDOWN : WM_MBUTTONUP; lp = MAKELPARAM(x, y); break;
                    case 15: msg = WM_PAINT; break;
                    }
                    std::wstring obs[2];
                    for (int k = 0; k < 2; k++)
                    {
                        HWND h = k == 0 ? p.cpp->_GetWnd() : p.rust.Wnd();
                        if (capture[k]) SetCapture(capture[k]); else ReleaseCapture();
                        SetCursor(nullptr);
                        WINDOWPOS wpos = pos;
                        wpos.hwnd = h;
                        LRESULT r = SendMessage(h, msg, wp, useWindowPos ? (LPARAM)&wpos : lp);
                        if (msg == WM_CTLCOLORSCROLLBAR) r = (r != 0);   // each window answers with its own brush
                        obs[k] = (k == 0 ? ObserveCpp(p) : ObserveRust(p)) + L" result=" + std::to_wstring(r) +
                            L" cursor=" + std::to_wstring((INT_PTR)GetCursor()) + L" wp=" + std::to_wstring(useWindowPos ? wpos.flags : 0);
                        capture[k] = GetCapture();
                    }
                    ReleaseCapture();
                    if (obs[0] != obs[1])
                        Assert::Fail((L"scenario " + std::to_wstring(scenario) + L" step " + std::to_wstring(step) + L" msg 0x" + std::to_wstring(msg) +
                            L"\nC++  " + obs[0] + L"\nRust " + obs[1]).c_str());
                    compared++;
                }
                for (int a : p.cppRec.actions) (a == 0 ? selects : cancels)++;
            }
            Logger::WriteMessage((std::to_wstring(compared) + L" candidate messages compared, " + std::to_wstring(selects) + L" selections, " +
                std::to_wstring(cancels) + L" cancels").c_str());
            Assert::IsTrue(selects > 0 && cancels > 0, L"coverage");
        }

        TEST_METHOD(OperationsMatchCpp)
        {
            std::mt19937 rng(83);
            int compared = 0;
            for (int scenario = 0; scenario < 20; scenario++)
            {
                std::vector<std::wstring> texts(rng() % 40);
                for (auto& t : texts) t = kPool[rng() % ARRAYSIZE(kPool)];
                Pair p;
                p.Setup(rng, 1 + (int)(rng() % 9), 1 + rng() % 4, texts);
                for (int step = 0; step < 60; step++)
                {
                    int op = (int)(rng() % 14);
                    BOOL flag = rng() % 2;
                    int x = (int)(rng() % 1200) - 100, y = (int)(rng() % 800) - 100;
                    UINT w = 1 + rng() % 8;
                    COLORREF c1 = rng() & 0xFFFFFF, c2 = rng() & 0xFFFFFF;
                    int n = (int)(rng() % 7) - 3;
                    int index = (int)(rng() % (texts.size() + 3)) - 1;
                    RECT text = { x, y, x + (int)(rng() % 300), y + 20 + (int)(rng() % 20) };
                    std::wstring extent[2], obs[2];
                    for (int k = 0; k < 2; k++)
                    {
                        ReleaseCapture();   // capture is global; _Show(TRUE) takes it: observe each side right after its op
                        CCandidateWindow* cw = p.cpp;
                        CRustCandidateWindow* rw = p.rust.w.get();
                        DimeCandidate* rc = p.rust.c;
                        int ret = -1;
                        switch (op)
                        {
                        case 0: k == 0 ? cw->_Show(flag) : rw->_Show(flag); break;
                        case 1: k == 0 ? cw->_Move(x, y) : rw->_Move(x, y); break;
                        case 2: k == 0 ? cw->_ResizeWindow() : rw->_ResizeWindow(); break;
                        case 3: k == 0 ? cw->_SetCandStringLength(w) : rw->_SetCandStringLength(w); break;
                        case 4:
                            if (k == 0) { cw->_SetTextColor(c1, c2); cw->_SetFillColor(c2); }
                            else { rw->_SetTextColor(c1, c2); rw->_SetFillColor(c2); }
                            break;
                        case 5:
                            k == 0 ? cw->_SetScrollInfo((int)texts.size() + n, 3 + (n & 3)) : rw->_SetScrollInfo((int)texts.size() + n, 3 + (n & 3));
                            break;
                        case 6: if (k == 0) { if (cw->_GetScrollBarForTest()) cw->_GetScrollBarForTest()->SbShiftLine(n, flag); } else dime_ui_candidate_op(rc, 6, n, flag); break;
                        case 7: if (k == 0) { if (cw->_GetScrollBarForTest()) cw->_GetScrollBarForTest()->SbShiftPage(n, flag); } else dime_ui_candidate_op(rc, 7, n, flag); break;
                        case 8: if (k == 0) { if (cw->_GetScrollBarForTest()) cw->_GetScrollBarForTest()->SbShiftPosition(x & 63, flag); } else dime_ui_candidate_op(rc, 8, x & 63, flag); break;
                        case 9: k == 0 ? cw->_InvalidateRect() : rw->_InvalidateRect(); break;
                        case 10:
                        {
                            RECT cand = { 0, 0, 0, 0 };
                            POINT pt = { 0, 0 };
                            HRESULT hr = S_OK;
                            if (k == 0) { cw->_GetClientRect(&cand); hr = cw->_GetWindowExtent(&text, &cand, &pt); }
                            else { rw->_GetClientRect(&cand); hr = rw->_GetWindowExtent(&text, &cand, &pt); }
                            extent[k] = L" extent=" + std::to_wstring(pt.x) + L"," + std::to_wstring(pt.y) + L"/" + std::to_wstring(hr) +
                                L" client=" + R(cand);
                            break;
                        }
                        // UIPresenter's selection and paging: the model, then the scroll bar.
                        case 11: ret = k == 0 ? cw->_MoveSelection(n, flag) : rw->_MoveSelection(n, flag); break;
                        case 12: ret = k == 0 ? cw->_SetSelection(index, flag) : rw->_SetSelection(index, flag); break;
                        case 13: ret = k == 0 ? cw->_MovePage(n, flag) : rw->_MovePage(n, flag); break;
                        }
                        if (ret != -1) extent[k] += L" ret=" + std::to_wstring(ret);
                        obs[k] = (k == 0 ? ObserveCpp(p) : ObserveRust(p)) + extent[k];
                    }
                    ReleaseCapture();
                    const std::wstring& a = obs[0];
                    const std::wstring& b = obs[1];
                    if (a != b)
                        Assert::Fail((L"scenario " + std::to_wstring(scenario) + L" step " + std::to_wstring(step) + L" op " + std::to_wstring(op) +
                            L"\nC++  " + a + L"\nRust " + b).c_str());
                    compared++;
                }
            }
            Logger::WriteMessage((std::to_wstring(compared) + L" candidate operations compared").c_str());
        }

    private:
        HFONT savedFont = nullptr;
        HFONT font = nullptr;
    };
}
#endif
