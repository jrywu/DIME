/********************************************************************************
* PresenterLayoutParityTest: CUIPresenter's placement decisions, the C++
* originals vs dime-ui::presenter (phase 4b-1, docs/RUST_TSF_SHELL.md).
*
* Two presenters with the same real windows (a candidate window with a list,
* shown or hidden, or none; each notify slot missing, hidden or shown with its
* own text), one deciding in C++ and one in Rust, start from the same location
* state and get the same random facts: _LayoutChangeApply (compositions of any
* size including empty and inverted, first calls, with and without a parent,
* the caret inside or outside), _NotifyLocationForShow (transient or not,
* empty or real rcCaret) and _RestackForShow. Phase 4b-2a: in the Rust
* presenter the location state and the notify stack are Rust's
* (dime-ui::presenterwin), so the steps also make, retext, show, hide, dispose
* and clear slots, set the colors, show the UI, reset the composition range
* and restack. After each step: the location state, the return value, the
* presenter's queries (IsNotifyShown, hint visible, which slots exist,
* GetCandLocation, GetCompRangeHeight), and every window's rectangle,
* visibility and title size.
********************************************************************************/
#include "pch.h"
#include "CppUnitTest.h"
#include <Windows.h>
#include <random>
#include <string>

#include "../Globals.h"
#include "../DIME.h"
#include "../UIPresenter.h"
#include "../BaseStructure.h"
#include "../UIConstants.h"
#include "WindowClassTestHelper.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

#ifdef DIME_RUST_AVAILABLE
// UIPresenter.h's friend: the private parts the parity drives.
class PresenterLayoutParityAccess
{
public:
    static void UseRust(CUIPresenter* p, BOOL rust)
    {
        p->_useRustPresenter = rust;
#ifndef DIME_USE_RUST_ENGINE
        // The C++ build registers DIME.NotifyWindow with CBaseWindow's procedure: Rust classes here.
        if (rust)
        {
            DimeNotifyClasses c = RustNotifyTestClasses();
            p->_SetNotifyClassesForTest(c.notify, c.notify_shadow);
            p->_SetCandidateClassesForTest(RustCandidateTestClasses());
        }
#endif
    }
    static BOOL UsesRust(const CUIPresenter* p) { return p->_RustPresenter(); }
    static void SetState(CUIPresenter* p, POINT cand, POINT notify, RECT comp) { p->_SetPlacement(cand, notify, comp); }
    // The selection keys: the window keeps the pointer (as the engine's range), so they outlive the test.
    static CCandidateRange* Range(int kind)
    {
        static CCandidateRange ranges[3];
        static bool made = false;
        if (!made)
        {
            const int sizes[3] = { 10, 5, 3 };
            for (int k = 0; k < 3; k++)
                for (int r = 0; r < sizes[k]; r++) ranges[k].Append()->CandIndex = L"1234567890"[r];
            made = true;
        }
        return &ranges[kind % 3];
    }
    static void SetCandidates(CUIPresenter* p, int count, int rangeKind, BOOL withKeyCode, UINT width, bool phrase)
    {
        static const wchar_t* texts[] = { L"一", L"二三", L"四", L"五六七", L"八", L"九", L"十", L"百", L"千萬", L"億", L"兆", L"京", L"" };
        static const wchar_t* keys[] = { L"a", L"bc", L"", L"def" };
        CDIMEArray<CCandidateListItem> list;
        for (int i = 0; i < count; i++)
        {
            CCandidateListItem* it = list.Append();
            it->_ItemString.Set(texts[i % 13], wcslen(texts[i % 13]));
            it->_FindKeyCode.Set(keys[i % 4], wcslen(keys[i % 4]));
        }
        p->_SetCandidateText(&list, Range(rangeKind), withKeyCode, width, phrase);
    }
    static void MakeCandidate(CUIPresenter* p, int count, UINT width)
    {
        Assert::IsTrue(SUCCEEDED(p->MakeCandidateWindow(nullptr, width)));
        SetCandidates(p, count, 0, FALSE, 3, false);
    }
    // The candidate-list operations (phase 4b-2b: the window and the list are Rust's).
    static void CandOp(CUIPresenter* p, int op, int a, int b)
    {
        switch (op)
        {
        case 0: p->_MoveCandidateSelection(a % 3 - 1); break;
        case 1: p->_SetCandidateSelection(a % 15 - 2, b % 2); break;
        case 2: p->_MoveCandidatePage(a % 3 - 1); break;
        case 3: p->_SetCandidateSelectionInPage(a % 12 - 1); break;
        case 4: p->SetSelection((UINT)(a % 14)); break;
        case 5: p->AdviseUIChangedByArrowKey((KEYSTROKE_FUNCTION)((int)KEYSTROKE_FUNCTION::FUNCTION_MOVE_UP + a % 8)); break;
        case 6: SetCandidates(p, a % 40, b, b % 2, 3 + a % 5, (b % 3) == 0); break;
        case 7: p->_ClearCandidateList(); break;
        case 8:
            p->_SetCandidateTextColor(RGB(a % 200, 20, 30), RGB(250, 240, 230 - b % 9));
            p->_SetCandidateNumberColor(RGB(5, a % 250, 9), RGB(240, 240, 240));
            p->_SetCandidateSelectedTextColor(RGB(255, 255, 255), RGB(0, 80 + b % 100, 200));
            p->_SetCandidateFillColor(RGB(250 - a % 30, 250, 250));
            break;
        case 9:
            if (a % 3 == 0) p->_EndCandidateList();
            else if (!p->_HasCand()) MakeCandidate(p, 1 + a % 30, 3 + b % 5);
            else { UINT n = 1; p->SetPageIndex(&n, 1); }
            break;
        default: p->Show(a % 2); break;
        }
    }
    static std::wstring CandQueries(CUIPresenter* p)
    {
        auto str = [](const WCHAR* t, DWORD_PTR n) { return t ? std::wstring(t, (size_t)n) : (n ? L"?" : L"-"); };
        UINT count = 0, sel = 0, page = 0, pages[8] = {}, pageCount = 0;
        BOOL shown = FALSE;
        p->GetCount(&count);
        p->GetSelection(&sel);
        HRESULT hrPage = p->GetCurrentPage(&page);
        HRESULT hrIndex = p->GetPageIndex(pages, 8, &pageCount);
        p->IsShown(&shown);
        std::wstring s = L"has=" + std::to_wstring(p->_HasCand()) + L" count=" + std::to_wstring(count) + L" sel=" + std::to_wstring(sel) +
                         L"/" + std::to_wstring(p->_GetCandidateSelection()) + L" page=" + std::to_wstring(hrPage) + L":" + std::to_wstring(page) +
                         L" index=" + std::to_wstring(hrIndex) + L":" + std::to_wstring(pageCount) + L":";
        for (UINT i = 0; i < min(pageCount, 8u); i++) s += std::to_wstring(pages[i]) + L",";
        s += L" shown=" + std::to_wstring(shown) + L"/" + std::to_wstring(p->IsCandShown());
        const WCHAR* t = nullptr;
        DWORD_PTR n = p->_GetSelectedCandidateString(&t);
        s += L" selStr=" + str(t, n);
        t = nullptr;
        n = p->_GetSelectedCandidateKeyCode(&t);
        s += L" selKey=" + str(t, n) + L" items=";
        for (UINT i = 0; i <= count && i < 50; i++)
        {
            BSTR b = nullptr;
            HRESULT hr = p->GetString(i, &b);
            s += std::to_wstring(hr) + (b ? std::wstring(b, SysStringLen(b)) : L"-") + L"|";
            if (b) SysFreeString(b);
        }
        return s;
    }
    static void MakeSlot(CUIPresenter* p, NOTIFY_TYPE type, const std::wstring& text, BOOL show)
    {
        CStringRange sr;
        sr.Set(text.c_str(), text.size());
        Assert::IsTrue(SUCCEEDED(p->MakeNotifyWindow(nullptr, &sr, type)));
        p->ShowNotify(show, 0, 0, type);
    }
    // The notify-stack operations (phase 4b-2a: the stack is Rust's in the Rust presenter).
    static void Make(CUIPresenter* p, NOTIFY_TYPE type, const std::wstring& text)
    {
        CStringRange sr;
        sr.Set(text.c_str(), text.size());
        Assert::AreEqual(S_OK, p->MakeNotifyWindow(nullptr, &sr, type));
    }
    static void SetText(CUIPresenter* p, NOTIFY_TYPE type, const std::wstring& text)
    {
        CStringRange sr;
        sr.Set(text.c_str(), text.size());
        p->SetNotifyText(&sr, type);
    }
    static void ShowSlot(CUIPresenter* p, NOTIFY_TYPE type, BOOL show) { p->ShowNotify(show, 0, 0, type); }
    static void Dispose(CUIPresenter* p, NOTIFY_TYPE type) { p->DisposeNotifyWindow(type); }
    static void Clear(CUIPresenter* p) { p->ClearNotify(); }
    static void Colors(CUIPresenter* p, COLORREF text, COLORREF bk) { p->_SetNotifyTextColor(text, bk); }
    static void ShowUI(CUIPresenter* p) { p->ToShowUIWindows(); }
    static void Reset(CUIPresenter* p) { p->ResetCompRange(); }
    static void Restack(CUIPresenter* p, BOOL growUp) { p->_RestackVisibleSlots(growUp); }
    static std::wstring Queries(CUIPresenter* p)
    {
        std::wstring s = L"shown=" + std::to_wstring(p->IsNotifyShown()) + L" hint=" + std::to_wstring(p->_AnyHintSlotVisible()) +
                         L" height=" + std::to_wstring(p->GetCompRangeHeight());
        POINT c = {};
        p->GetCandLocation(&c);
        s += L" candLoc=" + std::to_wstring(c.x) + L"," + std::to_wstring(c.y) + L" exists=";
        for (size_t i = 0; i < (size_t)NOTIFY_TYPE::NOTIFY_COUNT; i++) s += std::to_wstring(p->_NotifySlotExists((NOTIFY_TYPE)i));
        return s;
    }
    static void Show(CUIPresenter* p, BOOL show) { p->Show(show); }
    static void LayoutChange(CUIPresenter* p, const PresenterLayoutFacts& f) { p->_LayoutChangeApply(f); }
    static BOOL LocationForShow(CUIPresenter* p, const PresenterShowFacts& f) { return p->_NotifyLocationForShow(f); }
    static void RestackForShow(CUIPresenter* p, BOOL caretUpdated) { p->_RestackForShow(caretUpdated); }

    // The slot painted as WM_PAINT would into a DIB of its client size, hashed: the text and
    // the colors reached the window (the rectangles and title sizes do not show them).
    static std::wstring PaintHash(CUIPresenter* p, NOTIFY_TYPE type, HWND h)
    {
        RECT rc = {};
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
        HGDIOBJ old = SelectObject(mem, dib);
        PAINTSTRUCT ps = {};
        ps.hdc = mem;
        ps.rcPaint = rc;
        p->_PaintNotifySlotForTest(type, mem, &ps);
        GdiFlush();
        unsigned long long hash = 1469598103934665603ULL;
        for (size_t i = 0; i < (size_t)cx * cy; i++)
            hash = (hash ^ ((const DWORD*)bits)[i]) * 1099511628211ULL;
        SelectObject(mem, old);
        DeleteObject(dib);
        DeleteDC(mem);
        ReleaseDC(nullptr, screen);
        return std::to_wstring(hash);
    }

    // The candidate window painted as WM_PAINT would, hashed (the list, the selection, the colors).
    static std::wstring CandPaintHash(CUIPresenter* p, HWND h)
    {
        if (!h) return L"-";
        RECT rc = {};
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
        HGDIOBJ old = SelectObject(mem, dib);
        PAINTSTRUCT ps = {};
        ps.hdc = mem;
        ps.rcPaint = rc;
        p->_PaintCandidateForTest(mem, &ps);
        GdiFlush();
        unsigned long long hash = 1469598103934665603ULL;
        for (size_t i = 0; i < (size_t)cx * cy; i++)
            hash = (hash ^ ((const DWORD*)bits)[i]) * 1099511628211ULL;
        SelectObject(mem, old);
        DeleteObject(dib);
        DeleteDC(mem);
        ReleaseDC(nullptr, screen);
        return std::to_wstring(cx) + L"x" + std::to_wstring(cy) + L"#" + std::to_wstring(hash);
    }

    static std::wstring Observe(CUIPresenter* p)
    {
        auto pt = [](POINT q) { return std::to_wstring(q.x) + L"," + std::to_wstring(q.y); };
        auto rc = [](RECT r) { return std::to_wstring(r.left) + L"," + std::to_wstring(r.top) + L"," + std::to_wstring(r.right) + L"," + std::to_wstring(r.bottom); };
        POINT cand = {}, notify = {};
        RECT comp = {};
        p->_GetPlacement(&cand, &notify, &comp);
        std::wstring s = L"cand=" + pt(cand) + L" notify=" + pt(notify) + L" comp=" + rc(comp);
        if (p->_HasCand())
        {
            RECT r = {};
            HWND h = p->_CandWnd();
            GetWindowRect(h, &r);
            s += L" candWnd=" + rc(r) + L"/" + std::to_wstring(p->_CandVisible()) + L"/" + CandPaintHash(p, h);
            int sb[3] = { -1, -1, -1 };
            if (p->_CandScrollInfoForTest(sb))
                s += L" sb=" + std::to_wstring(sb[0]) + L"," + std::to_wstring(sb[1]) + L"," + std::to_wstring(sb[2]);
        }
        for (size_t i = 0; i < (size_t)NOTIFY_TYPE::NOTIFY_COUNT; i++)
        {
            if (!p->_NotifySlotExists((NOTIFY_TYPE)i)) continue;
            UINT cx = 0, cy = 0;
            HWND h = p->_NotifySlotWnd((NOTIFY_TYPE)i, &cx, &cy);
            RECT r = {};
            GetWindowRect(h, &r);
            s += L" slot" + std::to_wstring(i) + L"=" + rc(r) + L"/" + std::to_wstring(IsWindowVisible(h) ? 1 : 0) +
                 L"/" + std::to_wstring(cx) + L"x" + std::to_wstring(cy);
            if (h) s += L"/" + PaintHash(p, (NOTIFY_TYPE)i, h);
        }
        return s;
    }
};

namespace DIMEUnitTests
{
    TEST_CLASS(PresenterLayoutParityTest)
    {
    public:
        TEST_METHOD(DecisionsMatchCpp)
        {
            EnsureWindowClassesRegistered();
            HFONT savedFont = Global::defaultlFontHandle;
            HFONT font = CreateFont(-20, 0, 0, 0, FW_NORMAL, FALSE, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Microsoft JhengHei");
            Global::defaultlFontHandle = font;
            std::mt19937 rng(4201);
            auto in = [&](int lo, int hi) { return lo + (int)(rng() % (unsigned)(hi - lo + 1)); };
            int steps = 0, layoutPlaced = 0, restacked = 0, updated = 0, stackOps = 0, candOps = 0;
            const std::wstring stackTexts[] = { L"英數", L"反查：一二", L"特別碼 XY", L"錯誤組字", L"全形", L"簡" };
            const NOTIFY_TYPE types[] = { NOTIFY_TYPE::NOTIFY_CHN_ENG, NOTIFY_TYPE::NOTIFY_REVERSE_LOOKUP,
                                          NOTIFY_TYPE::NOTIFY_SPECIAL_CODE, NOTIFY_TYPE::NOTIFY_BEEP };
            const std::wstring slotTexts[] = { L"中", L"反查：一二三", L"特別碼 ABCD", L"錯誤組字" };

            {
                // The production default: the Rust decisions exactly in the Rust build.
                CDIME* d = new CDIME();
                CUIPresenter* q = new CUIPresenter(d, nullptr);
#ifdef DIME_USE_RUST_ENGINE
                Assert::IsTrue(PresenterLayoutParityAccess::UsesRust(q) != FALSE, L"Rust decisions by default");
#else
                Assert::IsFalse(PresenterLayoutParityAccess::UsesRust(q) != FALSE, L"C++ decisions by default");
#endif
                q->Release();
                d->Release();
            }
            for (int scenario = 0; scenario < 60; scenario++)
            {
                CDIME* dime[2] = { new CDIME(), new CDIME() };
                CUIPresenter* p[2] = { new CUIPresenter(dime[0], nullptr), new CUIPresenter(dime[1], nullptr) };
                PresenterLayoutParityAccess::UseRust(p[0], FALSE);
                PresenterLayoutParityAccess::UseRust(p[1], TRUE);

                int candKind = in(0, 3);   // 0: none, 1: hidden, 2-3: shown
                int candCount = in(1, 12);
                UINT candWidth = (UINT)in(3, 8);   // characters, as GetCandidateWindowWidth()
                int slotKind[4];
                for (int& k : slotKind) k = in(0, 2);   // 0: missing, 1: hidden, 2: shown
                for (int k = 0; k < 2; k++)
                {
                    if (candKind != 0)
                    {
                        PresenterLayoutParityAccess::MakeCandidate(p[k], candCount, candWidth);
                        PresenterLayoutParityAccess::Show(p[k], candKind >= 2);
                    }
                    for (int i = 0; i < 4; i++)
                        if (slotKind[i] != 0)
                            PresenterLayoutParityAccess::MakeSlot(p[k], types[i], slotTexts[i], slotKind[i] == 2);
                }
                POINT cand = { in(-1, 1600), in(-1, 900) };
                POINT notify = { in(-1, 1600), in(-1, 900) };
                RECT comp = { in(0, 1500), in(0, 900), 0, 0 };
                comp.right = comp.left + in(-10, 300);
                comp.bottom = comp.top + in(-10, 40);
                for (int k = 0; k < 2; k++) PresenterLayoutParityAccess::SetState(p[k], cand, notify, comp);
                Assert::AreEqual(PresenterLayoutParityAccess::Observe(p[0]), PresenterLayoutParityAccess::Observe(p[1]), L"set up alike");

                for (int step = 0; step < 40; step++)
                {
                    int r30 = in(0, 29);
                    // 0 layout change, 1 location for show, 2 restack for show, 3-12 the notify stack (4b-2a),
                    // 13 the candidate list and window (4b-2b)
                    int op = r30 < 5 ? 0 : r30 < 8 ? 1 : r30 < 10 ? 2 : r30 < 20 ? r30 - 7 : 13;
                    std::wstring where = L"scenario " + std::to_wstring(scenario) + L" step " + std::to_wstring(step) + L" op " + std::to_wstring(op);
                    BOOL r[2] = {};
                    if (op == 0)
                    {
                        PresenterLayoutFacts f = {};
                        f.comp.left = in(-50, 1700);
                        f.comp.top = in(-50, 1000);
                        int shape = in(0, 5);   // inverted, empty, 1 pixel, normal
                        int w = shape == 0 ? in(-60, -1) : shape == 1 ? 0 : shape == 2 ? 1 : in(2, 500);
                        int h = shape == 0 ? in(-30, -1) : shape == 1 ? 0 : shape == 2 ? 1 : in(2, 40);
                        if (in(0, 3) == 0) h = in(-30, 40);   // mixed signs
                        f.comp.right = f.comp.left + w;
                        f.comp.bottom = f.comp.top + h;
                        f.firstCall = in(0, 1);
                        f.hasParent = in(0, 3) != 0;
                        f.caret = in(0, 1) ? POINT{ f.comp.left + in(0, max(0, w)), f.comp.top + in(0, max(0, h)) }
                                           : POINT{ in(-50, 1800), in(-50, 1000) };
                        for (int k = 0; k < 2; k++) PresenterLayoutParityAccess::LayoutChange(p[k], f);
                        layoutPlaced++;
                    }
                    else if (op == 1)
                    {
                        PresenterShowFacts f = {};
                        f.refresh = in(0, 1);
                        f.hasParent = in(0, 3) != 0;
                        if (in(0, 2) != 0) f.rcCaret = { in(-5, 600), in(-5, 400), in(0, 600), in(0, 400) };
                        f.guiCaret = { in(-50, 1800), in(-50, 1000) };
                        if (in(0, 3) == 0)
                        {
                            // An uninitialised location (ResetCompRange) for the fallbacks.
                            for (int k = 0; k < 2; k++) PresenterLayoutParityAccess::SetState(p[k], cand, { UI::DEFAULT_WINDOW_X, UI::DEFAULT_WINDOW_Y }, comp);
                        }
                        for (int k = 0; k < 2; k++) r[k] = PresenterLayoutParityAccess::LocationForShow(p[k], f);
                        Assert::AreEqual((int)r[0], (int)r[1], (where + L": caretUpdated").c_str());
                        if (r[0]) updated++;
                    }
                    else if (op == 2)
                    {
                        BOOL caretUpdated = in(0, 1);
                        for (int k = 0; k < 2; k++) PresenterLayoutParityAccess::RestackForShow(p[k], caretUpdated);
                        restacked++;
                    }
                    else if (op == 13)
                    {
                        int cop = in(0, 10), a = in(0, 1000), b = in(0, 1000);
                        where += L" cand " + std::to_wstring(cop) + L" " + std::to_wstring(a) + L" " + std::to_wstring(b);
                        for (int k = 0; k < 2; k++) PresenterLayoutParityAccess::CandOp(p[k], cop, a, b);
                        candOps++;
                    }
                    else
                    {
                        NOTIFY_TYPE t = (NOTIFY_TYPE)in(0, (int)NOTIFY_TYPE::NOTIFY_COUNT - 1);
                        std::wstring text = stackTexts[in(0, 5)];
                        for (int k = 0; k < 2; k++)
                        {
                            switch (op)
                            {
                            case 3: case 4: PresenterLayoutParityAccess::Make(p[k], t, text); break;
                            case 5: PresenterLayoutParityAccess::SetText(p[k], t, text); break;
                            case 6: case 7: PresenterLayoutParityAccess::ShowSlot(p[k], t, op == 6); break;
                            case 8: PresenterLayoutParityAccess::Dispose(p[k], t); break;
                            case 9: PresenterLayoutParityAccess::Clear(p[k]); break;
                            case 10: PresenterLayoutParityAccess::Colors(p[k], RGB(10 * (int)t, 20, 30), RGB(250, 240 - (int)t, 230)); break;
                            case 11: if (text.size() % 2) PresenterLayoutParityAccess::ShowUI(p[k]); else PresenterLayoutParityAccess::Reset(p[k]); break;
                            default: PresenterLayoutParityAccess::Restack(p[k], text.size() % 2 ? TRUE : FALSE); break;
                            }
                        }
                        stackOps++;
                    }
                    std::wstring qa = PresenterLayoutParityAccess::Queries(p[0]) + L" " + PresenterLayoutParityAccess::CandQueries(p[0]);
                    std::wstring qb = PresenterLayoutParityAccess::Queries(p[1]) + L" " + PresenterLayoutParityAccess::CandQueries(p[1]);
                    if (qa != qb)
                        Assert::Fail((where + L"\nC++:  " + qa + L"\nRust: " + qb).c_str());
                    std::wstring a = PresenterLayoutParityAccess::Observe(p[0]);
                    std::wstring b = PresenterLayoutParityAccess::Observe(p[1]);
                    if (a != b)
                        Assert::Fail((where + L"\nC++:  " + a + L"\nRust: " + b + L"\nqueries: " + qa).c_str());
                    steps++;
                }
                for (int k = 0; k < 2; k++)
                {
                    p[k]->Release();   // destroys the windows; releases the text service once
                    dime[k]->Release();
                }
            }
            Global::defaultlFontHandle = savedFont;
            DeleteObject(font);
            Logger::WriteMessage((std::to_wstring(steps) + L" steps compared (" + std::to_wstring(layoutPlaced) + L" layout changes, " +
                                  std::to_wstring(restacked) + L" restacks, " + std::to_wstring(updated) + L" caret updates, " + std::to_wstring(stackOps) + L" stack operations, " + std::to_wstring(candOps) + L" candidate operations)").c_str());
        }
    };
}
#endif
