// CandidateListModelTest.cpp - unit tests for CCandidateListModel (Rust migration phase 2a-2)
//
// Pins down the candidate list, selection and paging rules (moved unchanged from
// CCandidateWindow), so that a later port can be checked against them.

#include "pch.h"
#include "CppUnitTest.h"
#include "../CandidateListModel.h"
#include "../RustCandidateListModel.h"

#include <random>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace DIMEUnitTests
{
    namespace
    {
        // A select-key range of n keys (= page size).
        struct Range
        {
            CCandidateRange range;
            explicit Range(int n)
            {
                for (int i = 0; i < n; i++)
                {
                    _KEYSTROKE* k = range.Append();
                    k->Printable = (WCHAR)(L'1' + i);
                    k->VirtualKey = '1' + i;
                    k->Modifiers = 0;
                    k->Index = i;
                    k->Function = KEYSTROKE_FUNCTION::FUNCTION_NONE;
                }
            }
        };

        void Fill(CCandidateListModel& m, int count, bool withKeys = false)
        {
            for (int i = 0; i < count; i++)
            {
                std::wstring item = L"c" + std::to_wstring(i), key = L"k" + std::to_wstring(i);
                CCandidateListItem li;
                li._ItemString.Set(item.c_str(), item.size());
                li._FindKeyCode.Set(key.c_str(), key.size());
                m.AddItem(&li, withKeys);
            }
        }

        std::vector<UINT> Pages(CCandidateListModel& m)
        {
            std::vector<UINT> p;
            for (UINT i = 0; i < m.PageCount(); i++) p.push_back(m.PageStart(i));
            return p;
        }

        UINT CurrentPage(CCandidateListModel& m)
        {
            UINT page = 99;
            Assert::IsTrue(SUCCEEDED(m.GetCurrentPage(&page)));
            return page;
        }

        std::wstring Selected(CCandidateListModel& m)
        {
            const WCHAR* s = nullptr;
            DWORD n = m.GetSelectedString(&s);
            return n ? std::wstring(s, n) : L"<none>";
        }

        void AssertPages(CCandidateListModel& m, std::vector<UINT> expected, const wchar_t* what)
        {
            std::vector<UINT> actual = Pages(m);
            std::wstring a, e;
            for (UINT v : actual) a += std::to_wstring(v) + L" ";
            for (UINT v : expected) e += std::to_wstring(v) + L" ";
            Assert::AreEqual(e, a, what);
        }
    }

    TEST_CLASS(CandidateListModelTest)
    {
    public:
        TEST_METHOD(Empty)
        {
            CCandidateListModel m;
            Assert::AreEqual(0u, m.Count());
            Assert::AreEqual(0, (int)m.Selection());
            UINT page = 0;
            Assert::AreEqual(E_UNEXPECTED, m.GetCurrentPage(&page));
            Assert::AreEqual(L"<none>", Selected(m).c_str());
            Assert::IsFalse(m.MoveSelection(1));
        }

        TEST_METHOD(AddItemCopiesStringsAndOptionalKeyCode)
        {
            CCandidateListModel m;
            Fill(m, 1, false);
            Fill(m, 1, true);
            const WCHAR* k = nullptr;
            m.SetSelectionRaw(0);
            Assert::AreEqual(0ul, m.GetSelectedKeyCode(&k));
            m.SetSelectionRaw(1);
            Assert::AreEqual(2ul, m.GetSelectedKeyCode(&k));
            Assert::AreEqual(std::wstring(L"k0"), std::wstring(k, 2));
            const WCHAR* s = nullptr;
            Assert::AreEqual(2ul, m.GetString(0, &s));
            Assert::AreEqual(0ul, m.GetString(5, &s));
            Assert::IsNull(s);
            Assert::AreEqual(0ul, m.GetString(-1, &s));
        }

        TEST_METHOD(SinglePageNeedsNoPaging)
        {
            Range r(9);
            CCandidateListModel m;
            m.SetIndexRange(&r.range);
            Fill(m, 5);
            Assert::IsTrue(m.SetSelection(0, nullptr));
            Assert::AreEqual(0u, m.PageCount()); // fewer items than a page: page index untouched
        }

        TEST_METHOD(TwelveItemsNinePerPage)
        {
            Range r(9);
            CCandidateListModel m;
            m.SetIndexRange(&r.range);
            Fill(m, 12);
            BOOL applied = FALSE;
            Assert::IsTrue(m.SetSelection(0, &applied));
            Assert::IsTrue(applied);
            AssertPages(m, { 0, 9 }, L"pages from 0");
            Assert::AreEqual(0u, CurrentPage(m));

            BOOL wrapped = TRUE;
            Assert::IsTrue(m.MovePage(1, &wrapped));
            Assert::IsFalse(wrapped);
            Assert::AreEqual(9, (int)m.Selection());
            Assert::AreEqual(1u, CurrentPage(m));

            Assert::IsTrue(m.MovePage(1, &wrapped)); // past the last page: back to the first
            Assert::IsTrue(wrapped);
            Assert::AreEqual(0, (int)m.Selection());

            Assert::IsTrue(m.MovePage(-1, &wrapped)); // before the first page: to the last
            Assert::IsTrue(wrapped);
            Assert::AreEqual(9, (int)m.Selection());

            Assert::IsTrue(m.SetSelectionInPage(2));
            Assert::AreEqual(11, (int)m.Selection());
            Assert::AreEqual(L"c11", Selected(m).c_str());
            Assert::IsFalse(m.SetSelectionInPage(12));
            Assert::IsFalse(m.SetSelectionInPage(-1));
        }

        TEST_METHOD(MovePageKeepsOffsetAndClampsOnShortLastPage)
        {
            Range r(9);
            CCandidateListModel m;
            m.SetIndexRange(&r.range);
            Fill(m, 12);
            m.SetSelection(0, nullptr);
            Assert::IsTrue(m.MoveSelection(5));
            Assert::IsTrue(m.MovePage(1, nullptr));
            Assert::AreEqual(11, (int)m.Selection()); // 9 + 5 clamped to the last item
        }

        TEST_METHOD(SelectLastRebuildsPagesAroundSelection)
        {
            Range r(9);
            CCandidateListModel m;
            m.SetIndexRange(&r.range);
            Fill(m, 12);
            BOOL applied = FALSE;
            m.SetSelection(-2, &applied); // -2 = last item
            Assert::IsTrue(applied);
            Assert::AreEqual(11, (int)m.Selection());
            AssertPages(m, { 0, 2, 11 }, L"pages around 11");
            Assert::AreEqual(2u, CurrentPage(m));
        }

        TEST_METHOD(InvalidSelectionIsRejected)
        {
            Range r(9);
            CCandidateListModel m;
            m.SetIndexRange(&r.range);
            Fill(m, 12);
            BOOL applied = TRUE;
            Assert::IsFalse(m.SetSelection(12, &applied));
            Assert::IsFalse(applied);
            Assert::IsFalse(m.SetSelection(-3, &applied));
            Assert::IsFalse(applied);
            m.SetSelection(11, nullptr);
            Assert::IsFalse(m.MoveSelection(1));
            Assert::AreEqual(11, (int)m.Selection());
            Assert::IsFalse(m.SetSelectionOffset(1));
        }

        TEST_METHOD(PhraseSelectionMinusOne)
        {
            Range r(9);
            CCandidateListModel m;
            m.SetIndexRange(&r.range);
            Fill(m, 12);
            BOOL applied = FALSE;
            m.SetSelection(-1, &applied); // associated phrases: nothing selected
            Assert::IsTrue(applied);
            Assert::AreEqual(-1, (int)m.Selection());
            AssertPages(m, { 0, 9 }, L"phrase pages");
            Assert::AreEqual(0u, CurrentPage(m));
            Assert::AreEqual(L"<none>", Selected(m).c_str());

            Assert::IsTrue(m.MovePage(1, nullptr)); // -1 is reset to 0 before moving
            Assert::AreEqual(9, (int)m.Selection());
        }

        TEST_METHOD(ClearResets)
        {
            Range r(9);
            CCandidateListModel m;
            m.SetIndexRange(&r.range);
            Fill(m, 12);
            m.SetSelection(10, nullptr);
            m.Clear();
            Assert::AreEqual(0u, m.Count());
            Assert::AreEqual(0, (int)m.Selection());
            Assert::AreEqual(0u, m.PageCount());
        }

        TEST_METHOD(ScrollOffsetOnShortLastPage)
        {
            Range r(9);
            CCandidateListModel m;
            m.SetIndexRange(&r.range);
            Fill(m, 12);
            m.SetSelection(0, nullptr);
            for (int i = 0; i < 11; i++)
                Assert::IsTrue(m.SetSelectionOffset(1));
            Assert::AreEqual(11, (int)m.Selection());
            std::vector<UINT> p = Pages(m);
            Assert::IsTrue(p.size() >= 2);
            Assert::IsTrue(m.SetSelectionOffset(-1));
            Assert::AreEqual(10, (int)m.Selection());
        }

        TEST_METHOD(PaintAdjustFillsShortLastPage)
        {
            Range r(9);
            CCandidateListModel m;
            m.SetIndexRange(&r.range);
            Fill(m, 12);
            m.SetSelection(-2, nullptr);           // pages 0 2 11, on page 2 (one item)
            UINT page = CurrentPage(m), start = 0;
            Assert::AreEqual(S_OK, m.AdjustPageIndex(page, start));
            Assert::AreEqual(3u, start);           // the last page now shows items 3..11
            Assert::AreEqual(11, (int)m.Selection());
        }
    };

#ifdef DIME_RUST_AVAILABLE
    // C++ CCandidateListModel vs CRustCandidateListModel on random operation sequences.
    TEST_CLASS(RustCandidateListModelParityTest)
    {
        struct Pair
        {
            CCandidateListModel cpp;
            CRustCandidateListModel rust;
        };

        static std::wstring State(CCandidateListModel& m)
        {
            std::wstring s = L"sel=" + std::to_wstring(m.Selection()) + L" pages=";
            for (UINT i = 0; i < m.PageCount(); i++) s += std::to_wstring(m.PageStart(i)) + L",";
            UINT page = 77;
            HRESULT hr = m.GetCurrentPage(&page);
            s += L" cur=" + std::to_wstring(hr) + L"/" + std::to_wstring(page);
            BOOL empty = 7;
            hr = m.CurrentPageHasEmptyItems(&empty);
            s += L" empty=" + std::to_wstring(hr) + L"/" + std::to_wstring(empty);
            const WCHAR* str = nullptr;
            DWORD n = m.GetSelectedString(&str);
            s += L" str=" + (n ? std::wstring(str, n) : std::wstring(L"-"));
            return s;
        }

    public:
        TEST_METHOD(RandomOperations)
        {
            int checked = 0;
            for (unsigned seed = 1; seed <= 400; seed++)
            {
                std::mt19937 rng(seed);
                auto rnd = [&](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };

                int pageSize = rnd(0, 10);   // 0 = no select-key range
                Range r(pageSize > 0 ? pageSize : 1);
                Pair p;
                Assert::IsTrue(p.rust.IsValid());
                p.cpp.SetIndexRange(pageSize ? &r.range : nullptr);
                p.rust.SetIndexRange(pageSize ? &r.range : nullptr);
                Fill(p.cpp, rnd(0, 30));
                for (UINT i = 0; i < p.cpp.Count(); i++)
                {
                    CCandidateListItem* it = p.cpp.ItemAt(i);
                    p.rust.AddItem(it, FALSE);
                }

                std::wstring log;
                for (int step = 0; step < 60; step++)
                {
                    int count = (int)p.cpp.Count();
                    int op = rnd(0, 10);
                    int arg = rnd(-3, count + 2);
                    std::wstring what;
                    long long a = 0, b = 0, a2 = 0, b2 = 0;
                    switch (op)
                    {
                    case 0: { BOOL x = 0, y = 0; a = p.cpp.SetSelection(arg, &x); b = p.rust.SetSelection(arg, &y); a2 = x; b2 = y; what = L"SetSelection"; break; }
                    case 1: { int off = rnd(-12, 12); a = p.cpp.MoveSelection(off); b = p.rust.MoveSelection(off); what = L"MoveSelection " + std::to_wstring(off); break; }
                    case 2: { int off = rnd(-2, 2); BOOL x = 0, y = 0; a = p.cpp.MovePage(off, &x); b = p.rust.MovePage(off, &y); a2 = x; b2 = y; what = L"MovePage " + std::to_wstring(off); break; }
                    case 3: { int off = rnd(-1, 1) * (rnd(0, 3) == 0 ? 3 : 1); a = p.cpp.SetSelectionOffset(off); b = p.rust.SetSelectionOffset(off); what = L"SetSelectionOffset " + std::to_wstring(off); break; }
                    case 4: { int pos = rnd(-1, 12); a = p.cpp.SetSelectionInPage(pos); b = p.rust.SetSelectionInPage(pos); what = L"SetSelectionInPage " + std::to_wstring(pos); break; }
                    case 5: { a = p.cpp.AdjustPageIndexForSelection(); b = p.rust.AdjustPageIndexForSelection(); what = L"AdjustPageIndexForSelection"; break; }
                    case 6: { p.cpp.SetSelectionRaw(arg); p.rust.SetSelectionRaw(arg); what = L"SetSelectionRaw"; break; }
                    case 7:
                        if (p.cpp.PageCount() > 0)
                        {
                            UINT page = (UINT)rnd(0, (int)p.cpp.PageCount() - 1), page2 = page, idx = 5, idx2 = 5;
                            a = p.cpp.AdjustPageIndex(page, idx); b = p.rust.AdjustPageIndex(page2, idx2);
                            a2 = ((long long)page << 32) | idx; b2 = ((long long)page2 << 32) | idx2;
                            what = L"AdjustPageIndex";
                        }
                        break;
                    case 8:
                    {
                        UINT pages[4]; UINT n = (UINT)rnd(0, 4);
                        for (UINT i = 0; i < n; i++) pages[i] = (UINT)rnd(0, count + 1);
                        p.cpp.SetPageIndex(pages, n); p.rust.SetPageIndex(pages, n);
                        what = L"SetPageIndex";
                        break;
                    }
                    case 9: { Fill(p.cpp, 1); p.rust.AddItem(p.cpp.ItemAt(p.cpp.Count() - 1), FALSE); what = L"AddItem"; break; }
                    default: if (rnd(0, 5) == 0) { p.cpp.Clear(); p.rust.Clear(); Fill(p.cpp, rnd(0, 15)); for (UINT i = 0; i < p.cpp.Count(); i++) p.rust.AddItem(p.cpp.ItemAt(i), FALSE); what = L"Clear+Fill"; } break;
                    }
                    log += what + L" arg=" + std::to_wstring(arg) + L"; ";
                    std::wstring sc = State(p.cpp), sr = State(p.rust);
                    if (a != b || a2 != b2 || sc != sr)
                        Assert::Fail((L"seed " + std::to_wstring(seed) + L" pageSize " + std::to_wstring(pageSize) + L" after: " + log
                            + L"\n ret C++=" + std::to_wstring(a) + L"/" + std::to_wstring(a2) + L" Rust=" + std::to_wstring(b) + L"/" + std::to_wstring(b2)
                            + L"\n C++:  " + sc + L"\n Rust: " + sr).c_str());
                    checked++;
                }
            }
            Logger::WriteMessage((L"candidate model operations compared: " + std::to_wstring(checked) + L"\n").c_str());
        }
    };
#endif
}
