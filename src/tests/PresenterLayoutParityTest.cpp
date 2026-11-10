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
* empty or real rcCaret) and _RestackForShow. After each step: the location
* state, the return value, and every window's rectangle and visibility.
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
    static void UseRust(CUIPresenter* p, BOOL rust) { p->_useRustLayout = rust; }
    static BOOL UsesRust(const CUIPresenter* p) { return p->_useRustLayout; }
    static void SetState(CUIPresenter* p, POINT cand, POINT notify, RECT comp)
    {
        p->_candLocation = cand;
        p->_notifyLocation = notify;
        p->_rectCompRange = comp;
    }
    static void MakeCandidate(CUIPresenter* p, int count, UINT width)
    {
        Assert::IsTrue(SUCCEEDED(p->MakeCandidateWindow(nullptr, width)));
        static const wchar_t keys[] = L"1234567890";
        CCandidateRange range;
        for (int r = 0; r < 10; r++) range.Append()->CandIndex = keys[r];
        CDIMEArray<CCandidateListItem> list;
        static const wchar_t* texts[] = { L"一", L"二三", L"四", L"五六七", L"八", L"九", L"十", L"百", L"千萬", L"億", L"兆", L"京" };
        for (int i = 0; i < count; i++) list.Append()->_ItemString.Set(texts[i % 12], wcslen(texts[i % 12]));
        p->_SetCandidateText(&list, &range, FALSE, 3);
    }
    static void MakeSlot(CUIPresenter* p, NOTIFY_TYPE type, const std::wstring& text, BOOL show)
    {
        CStringRange sr;
        sr.Set(text.c_str(), text.size());
        Assert::IsTrue(SUCCEEDED(p->MakeNotifyWindow(nullptr, &sr, type)));
        p->ShowNotify(show, 0, 0, type);
    }
    static void Show(CUIPresenter* p, BOOL show) { p->Show(show); }
    static void LayoutChange(CUIPresenter* p, const PresenterLayoutFacts& f) { p->_LayoutChangeApply(f); }
    static BOOL LocationForShow(CUIPresenter* p, const PresenterShowFacts& f) { return p->_NotifyLocationForShow(f); }
    static void RestackForShow(CUIPresenter* p, BOOL caretUpdated) { p->_RestackForShow(caretUpdated); }

    static std::wstring Observe(CUIPresenter* p)
    {
        auto pt = [](POINT q) { return std::to_wstring(q.x) + L"," + std::to_wstring(q.y); };
        auto rc = [](RECT r) { return std::to_wstring(r.left) + L"," + std::to_wstring(r.top) + L"," + std::to_wstring(r.right) + L"," + std::to_wstring(r.bottom); };
        std::wstring s = L"cand=" + pt(p->_candLocation) + L" notify=" + pt(p->_notifyLocation) + L" comp=" + rc(p->_rectCompRange);
        if (p->_pCandidateWnd)
        {
            RECT r = {};
            GetWindowRect(p->_pCandidateWnd->_GetWnd(), &r);
            s += L" candWnd=" + rc(r) + L"/" + std::to_wstring(p->_pCandidateWnd->_IsWindowVisible() ? 1 : 0);
        }
        for (size_t i = 0; i < p->_pNotifyWnds.size(); i++)
        {
            auto& w = p->_pNotifyWnds[i];
            if (!w) continue;
            RECT r = {};
            GetWindowRect(w->_GetWnd(), &r);
            s += L" slot" + std::to_wstring(i) + L"=" + rc(r) + L"/" + std::to_wstring(w->_IsWindowVisible() ? 1 : 0);
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
            int steps = 0, layoutPlaced = 0, restacked = 0, updated = 0;
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
            for (int scenario = 0; scenario < 40; scenario++)
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

                for (int step = 0; step < 25; step++)
                {
                    int op = in(0, 2);
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
                    else
                    {
                        BOOL caretUpdated = in(0, 1);
                        for (int k = 0; k < 2; k++) PresenterLayoutParityAccess::RestackForShow(p[k], caretUpdated);
                        restacked++;
                    }
                    std::wstring a = PresenterLayoutParityAccess::Observe(p[0]);
                    std::wstring b = PresenterLayoutParityAccess::Observe(p[1]);
                    if (a != b)
                        Assert::Fail((where + L"\nC++:  " + a + L"\nRust: " + b).c_str());
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
                                  std::to_wstring(restacked) + L" restacks, " + std::to_wstring(updated) + L" caret updates)").c_str());
        }
    };
}
#endif
