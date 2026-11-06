// CandidateListModelTest.cpp - unit tests for CCandidateListModel (Rust migration phase 2a-2)
//
// Pins down the candidate list, selection and paging rules (moved unchanged from
// CCandidateWindow), so that a later port can be checked against them.

#include "pch.h"
#include "CppUnitTest.h"
#include "../CandidateListModel.h"

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
}
