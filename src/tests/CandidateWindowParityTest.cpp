/********************************************************************************
* CandidateWindowParityTest: the candidate window owned by Rust (dime-ui::candwin,
* phase 3c-5d) vs the C++ CCandidateWindow (C++ message handling and drawing, a
* C++ scroll bar), side by side with the same contents. The Rust window gets the
* candidate list through the same C++ CCandidateListModel (the DimeCandModel
* callbacks below, as UIPresenter provides them).
*  - PaintMatchesCpp: pixels, layout members, window size, scroll bar highlight.
*  - MessagesMatchCpp: random messages (mouse on rows / the scroll bar / outside,
*    wheel, WM_SETCURSOR, WM_VSCROLL, activation, WINDOWPOS, show, paint, ...):
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

    // The Rust window's candidate list: a CCandidateListModel, as UIPresenter will hold it.
    struct RustSide
    {
        CCandidateRange range;
        std::unique_ptr<CCandidateListModel> model{ CCandidateListModel::Create() };
        std::vector<CCandidateListItem> items;
        Recorder rec;
        DimeCandidate* c = nullptr;

        static RustSide* S(void* ctx) { return static_cast<RustSide*>(ctx); }
        static int32_t Selection(void* ctx) { return S(ctx)->model->Selection(); }
        static void SetSelectionRaw(void* ctx, uint32_t i) { S(ctx)->model->SetSelectionRaw(i); }
        static void SetSelectionOffset(void* ctx, int32_t o) { S(ctx)->model->SetSelectionOffset(o); }
        static void SetSelection(void* ctx, int32_t i) { BOOL applied = FALSE; S(ctx)->model->SetSelection(i, &applied); }
        static void MovePage(void* ctx, int32_t o) { BOOL wrapped = FALSE; S(ctx)->model->MovePage(o, &wrapped); }
        static int32_t CurrentPage(void* ctx, int32_t* page)
        {
            int p = 0;
            if (FAILED(S(ctx)->model->GetCurrentPage(&p))) return 0;
            *page = p;
            return 1;
        }
        static int32_t PaintPage(void* ctx, uint32_t* page, uint32_t* start)
        {
            UINT p = 0, st = 0;
            if (FAILED(S(ctx)->model->GetCurrentPage(&p))) return 0;
            S(ctx)->model->AdjustPageIndex(p, st);
            *page = p;
            *start = st;
            return 1;
        }
        static uint32_t PageCount(void* ctx) { return S(ctx)->model->PageCount(); }
        static uint32_t PageStart(void* ctx, int32_t page) { return S(ctx)->model->PageStart(page); }
        static uint32_t Count(void* ctx) { return S(ctx)->model->Count(); }
        static int32_t RowsInPage(void* ctx) { return S(ctx)->range.Count(); }
        static uint16_t IndexChar(void* ctx, int32_t row) { return (uint16_t)S(ctx)->range.GetAt(row)->CandIndex; }
        static const uint16_t* Item(void* ctx, uint32_t index, uint32_t* len)
        {
            CCandidateListItem* it = S(ctx)->model->ItemAt(index);
            if (!it) return nullptr;
            *len = (uint32_t)it->_ItemString.GetLength();
            return (const uint16_t*)it->_ItemString.Get();
        }
        static int32_t HasCallback(void*) { return 1; }
        static void Invoke(void* ctx, int32_t action) { S(ctx)->rec.actions.push_back(action); }

        static uint32_t Dpi(void* hwnd) { return CConfig::GetDpiForHwnd((HWND)hwnd); }
        static int32_t Dark() { return CConfig::GetEffectiveDarkMode() ? 1 : 0; }
        static int32_t AlwaysShow() { return CConfig::GetAlwaysShowScrollbars() ? 1 : 0; }
        static void* Font() { return Global::defaultlFontHandle; }
        static void SetFont(void* hwnd) { CConfig::SetDefaultTextFont((HWND)hwnd); }

        bool Create(UINT wndWidth, const DimeCandColors& colors, COLORREF fill)
        {
            static const DimeCandEnv env = { Dpi, Dark, AlwaysShow, Font, SetFont };
            DimeCandModel m = { this, Selection, SetSelectionRaw, SetSelectionOffset, SetSelection, MovePage, CurrentPage,
                                PaintPage, PageCount, PageStart, Count, RowsInPage, IndexChar, Item, HasCallback, Invoke };
            DimeCandClasses classes = {
                TestClassAtomKind(L"DIME.Test.RustCandidateWindow", true, true),
                TestClassAtom(L"DIME.Test.RustShadowWindow", true),
                RustScrollBarTestAtom(),
                Global::AtomNotifyWindow, Global::AtomNotifyShadowWindow };
            model->SetIndexRange(&range);
            c = dime_ui_candidate_create(Global::dllInstanceHandle, &classes, nullptr, wndWidth, &env, &m, &colors, fill);
            return c != nullptr;
        }
        HWND Wnd() const { return (HWND)dime_ui_candidate_hwnd(c); }
        ~RustSide() { dime_ui_candidate_free(c); }
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
            cpp->_SetUseRustProcForTest(FALSE);
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
            for (int which = 0; which < 3; which++)   // the same color adjustment as the C++ setters
            {
                uint32_t cc = which == 0 ? colors.number : which == 1 ? colors.text : colors.selected_text;
                uint32_t bk = which == 0 ? colors.number_bk : which == 1 ? colors.bk : colors.selected_bk;
                dime_ui_candidate_set_color(rust.c, which, cc, bk);
            }

            UINT count = (UINT)texts.size();
            cppItems.resize(count);
            rust.items.resize(count);
            for (UINT i = 0; i < count; i++)
            {
                cppItems[i]._ItemString.Set(texts[i].c_str(), texts[i].size());
                rust.items[i]._ItemString.Set(texts[i].c_str(), texts[i].size());
                cpp->_AddString(&cppItems[i], FALSE);
                rust.model->AddItem(&rust.items[i], FALSE);
            }
            UINT pages = count ? (count + rowsInPage - 1) / rowsInPage : 0;
            std::vector<UINT> pageIndex(pages);
            for (UINT p = 0; p < pages; p++) pageIndex[p] = p * rowsInPage;
            if (pages)
            {
                cpp->_SetPageIndex(pageIndex.data(), pages);
                rust.model->SetPageIndex(pageIndex.data(), pages);
            }
            cpp->_SetScrollInfo(count, rowsInPage);
            dime_ui_candidate_op(rust.c, 9, (int)count, rowsInPage);
            cpp->_Move(300, 300);
            dime_ui_candidate_op(rust.c, 1, 300, 300);
            cpp->_ResizeWindow();
            dime_ui_candidate_op(rust.c, 2, 0, 0);
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
                        BOOL applied = FALSE;
                        p.cpp->_SetSelection(sel, FALSE);
                        p.rust.model->SetSelection(sel, &applied);
                    }
                    RECT rc = { 0, 0, 0, 0 };
                    GetClientRect(p.cpp->_GetWnd(), &rc);
                    int cx = max(1, (int)rc.right), cy = max(1, (int)rc.bottom);
                    std::vector<DWORD> c = PaintInto(cx, cy, [&](HDC dc) {
                        PAINTSTRUCT ps = {};
                        ps.hdc = dc;
                        ps.rcPaint = rc;
                        p.cpp->_OnPaintCpp(dc, &ps);
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
                    switch (rng() % 14)
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
                    int op = (int)(rng() % 11);
                    BOOL flag = rng() % 2;
                    int x = (int)(rng() % 1200) - 100, y = (int)(rng() % 800) - 100;
                    UINT w = 1 + rng() % 8;
                    COLORREF c1 = rng() & 0xFFFFFF, c2 = rng() & 0xFFFFFF;
                    int n = (int)(rng() % 7) - 3;
                    RECT text = { x, y, x + (int)(rng() % 300), y + 20 + (int)(rng() % 20) };
                    std::wstring extent[2], obs[2];
                    for (int k = 0; k < 2; k++)
                    {
                        ReleaseCapture();   // capture is global; _Show(TRUE) takes it: observe each side right after its op
                        CCandidateWindow* cw = p.cpp;
                        DimeCandidate* rc = p.rust.c;
                        switch (op)
                        {
                        case 0: k == 0 ? cw->_Show(flag) : dime_ui_candidate_op(rc, 0, flag, 0); break;
                        case 1: k == 0 ? cw->_Move(x, y) : dime_ui_candidate_op(rc, 1, x, y); break;
                        case 2: k == 0 ? cw->_ResizeWindow() : dime_ui_candidate_op(rc, 2, 0, 0); break;
                        case 3: k == 0 ? cw->_SetCandStringLength(w) : dime_ui_candidate_op(rc, 4, (int)w, 0); break;
                        case 4:
                            if (k == 0) { cw->_SetTextColor(c1, c2); cw->_SetFillColor(c2); }
                            else { dime_ui_candidate_set_color(rc, 1, c1, c2); dime_ui_candidate_op(rc, 5, (int)c2, 0); }
                            break;
                        case 5:
                            k == 0 ? cw->_SetScrollInfo((int)texts.size() + n, 3 + (n & 3)) : dime_ui_candidate_op(rc, 9, (int)texts.size() + n, 3 + (n & 3));
                            break;
                        case 6: if (k == 0) { if (cw->_GetScrollBarForTest()) cw->_GetScrollBarForTest()->SbShiftLine(n, flag); } else dime_ui_candidate_op(rc, 6, n, flag); break;
                        case 7: if (k == 0) { if (cw->_GetScrollBarForTest()) cw->_GetScrollBarForTest()->SbShiftPage(n, flag); } else dime_ui_candidate_op(rc, 7, n, flag); break;
                        case 8: if (k == 0) { if (cw->_GetScrollBarForTest()) cw->_GetScrollBarForTest()->SbShiftPosition(x & 63, flag); } else dime_ui_candidate_op(rc, 8, x & 63, flag); break;
                        case 9: k == 0 ? cw->_InvalidateRect() : dime_ui_candidate_op(rc, 3, 0, 0); break;
                        case 10:
                        {
                            RECT cand = { 0, 0, 0, 0 };
                            POINT pt = { 0, 0 };
                            if (k == 0)
                            {
                                cw->_GetClientRect(&cand);
                                cw->_GetWindowExtent(&text, &cand, &pt);
                            }
                            else
                            {
                                DimeRect cr = {}, tr = { text.left, text.top, text.right, text.bottom };
                                DimePoint dp = {};
                                dime_ui_candidate_rect(rc, 1, &cr);
                                dime_ui_candidate_window_extent(rc, &tr, &cr, &dp);
                                pt = { dp.x, dp.y };
                            }
                            extent[k] = L" extent=" + std::to_wstring(pt.x) + L"," + std::to_wstring(pt.y);
                            break;
                        }
                        }
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
