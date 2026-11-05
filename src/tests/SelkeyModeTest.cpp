/********************************************************************************
* DayiSelkeyMode tests: the candidate select keys built by
* CCompositionProcessorEngine::SetupCandidateListRange() for each mode, the
* key -> candidate index lookup, and key routing while the candidate window
* is open.
*
*   FROM_TABLE (0): table %selkey (fallback without a table); Dayi keeps
*                   "space as first select key" so the keys start at 2.
*   NUMERIC    (1): 1 2 3 4 5 6 7 8 9 0          -> candidates 1..10
*   DAYI       (2): ' [ ] - \  then  " { } _     -> candidates 1..9
*                   (the Dayi manual: Shift+' [ ] - for 6..9)
* In NUMERIC / DAYI, Space is not a slot: it converts the highlighted (first)
* candidate through the normal VK_SPACE path.
********************************************************************************/
#include "pch.h"
#include "CppUnitTest.h"
#include <Windows.h>
#include <msctf.h>

#include "../DIME.h"
#include "../Globals.h"
#include "../Config.h"
#include "../CompositionProcessorEngine.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace DIMEUnitTests
{
    TEST_CLASS(SelkeyModeTest)
    {
    private:
        DAYI_SELKEY_MODE _savedSelkeyMode = DAYI_SELKEY_MODE::DAYI_SELKEY_FROM_TABLE;
        BOOL _savedSpaceFirst = FALSE;
        BOOL _savedSpacePageDown = FALSE;
        IME_MODE _savedImeMode = IME_MODE::IME_MODE_NONE;
        USHORT _savedModifiers = 0;

        // Dayi forces "space as first select key" in SetupConfiguration().
        static void SetupDayi(CCompositionProcessorEngine& engine, DAYI_SELKEY_MODE mode)
        {
            Global::imeMode = IME_MODE::IME_MODE_DAYI;
            CConfig::SetSpaceAsFirstCaniSelkey(TRUE);
            CConfig::SetDayiSelkeyMode(mode);
            engine.SetupCandidateListRange(IME_MODE::IME_MODE_DAYI);
        }

        static std::wstring Printables(CCandidateRange* pRange)
        {
            std::wstring s;
            for (int i = 0; i < pRange->Count(); i++)
                s += pRange->GetAt(i)->Printable;
            return s;
        }

        static std::wstring Labels(CCandidateRange* pRange)
        {
            std::wstring s;
            for (int i = 0; i < pRange->Count(); i++)
                s += pRange->GetAt(i)->CandIndex;
            return s;
        }

        static int IndexOf(CCandidateRange* pRange, UINT vKey, WCHAR wch, UINT modifiers = 0)
        {
            return pRange->GetIndex(vKey, wch, modifiers, CANDIDATE_MODE::CANDIDATE_ORIGINAL);
        }

    public:
        TEST_METHOD_INITIALIZE(Init)
        {
            _savedSelkeyMode = CConfig::GetDayiSelkeyMode();
            _savedSpaceFirst = CConfig::GetSpaceAsFirstCaniSelkey();
            _savedSpacePageDown = CConfig::GetSpaceAsPageDown();
            _savedImeMode = Global::imeMode;
            _savedModifiers = Global::ModifiersValue;
            CConfig::SetSpaceAsPageDown(FALSE);
            Global::ModifiersValue = 0;
        }

        TEST_METHOD_CLEANUP(Cleanup)
        {
            CConfig::SetDayiSelkeyMode(_savedSelkeyMode);
            CConfig::SetSpaceAsFirstCaniSelkey(_savedSpaceFirst);
            CConfig::SetSpaceAsPageDown(_savedSpacePageDown);
            Global::imeMode = _savedImeMode;
            Global::ModifiersValue = _savedModifiers;
        }

        TEST_METHOD(DayiKeys_NineSlots_InManualOrder)
        {
            CCompositionProcessorEngine engine(nullptr);
            SetupDayi(engine, DAYI_SELKEY_MODE::DAYI_SELKEY_DAYI);
            CCandidateRange* pRange = engine.GetCandidateListIndexRange();

            Assert::AreEqual(9u, engine.GetCandidatePageSize());
            Assert::AreEqual(std::wstring(L"'[]-\\\"{}_"), Printables(pRange));
            Assert::AreEqual(std::wstring(L"'[]-\\\"{}_"), Labels(pRange), L"window labels show the keys");
            for (int i = 0; i < pRange->Count(); i++)
                Assert::AreEqual((UINT)(i + 1), pRange->GetAt(i)->Index, L"numpad 1..9 -> slot 1..9");

            // Shifted keys carry the Shift modifier and the unshifted virtual key.
            Assert::AreEqual((UINT)VK_OEM_7, pRange->GetAt(5)->VirtualKey);
            Assert::AreEqual((UINT)TF_MOD_SHIFT, pRange->GetAt(5)->Modifiers);
            Assert::AreEqual((UINT)VK_OEM_MINUS, pRange->GetAt(8)->VirtualKey);
            Assert::AreEqual((UINT)TF_MOD_SHIFT, pRange->GetAt(8)->Modifiers);
        }

        TEST_METHOD(DayiKeys_GetIndex)
        {
            CCompositionProcessorEngine engine(nullptr);
            SetupDayi(engine, DAYI_SELKEY_MODE::DAYI_SELKEY_DAYI);
            CCandidateRange* pRange = engine.GetCandidateListIndexRange();

            Assert::AreEqual(0, IndexOf(pRange, VK_OEM_7, L'\''));
            Assert::AreEqual(1, IndexOf(pRange, VK_OEM_4, L'['));
            Assert::AreEqual(2, IndexOf(pRange, VK_OEM_6, L']'));
            Assert::AreEqual(3, IndexOf(pRange, VK_OEM_MINUS, L'-'));
            Assert::AreEqual(4, IndexOf(pRange, VK_OEM_5, L'\\'));
            Assert::AreEqual(5, IndexOf(pRange, VK_OEM_7, L'"', TF_MOD_SHIFT));
            Assert::AreEqual(6, IndexOf(pRange, VK_OEM_4, L'{', TF_MOD_SHIFT));
            Assert::AreEqual(7, IndexOf(pRange, VK_OEM_6, L'}', TF_MOD_SHIFT));
            Assert::AreEqual(8, IndexOf(pRange, VK_OEM_MINUS, L'_', TF_MOD_SHIFT));
            Assert::AreEqual(-1, IndexOf(pRange, VK_OEM_5, L'|', TF_MOD_SHIFT), L"only 9 select keys");
            Assert::AreEqual(-1, IndexOf(pRange, VK_SPACE, L' '), L"space is not a slot");
            Assert::AreEqual(-1, IndexOf(pRange, '1', L'1'), L"digits stay radicals");
            Assert::AreEqual(0, IndexOf(pRange, VK_NUMPAD1, L'1'), L"numpad 1 still selects the first");
        }

        TEST_METHOD(Numeric_TenSlots)
        {
            CCompositionProcessorEngine engine(nullptr);
            SetupDayi(engine, DAYI_SELKEY_MODE::DAYI_SELKEY_NUMERIC);
            CCandidateRange* pRange = engine.GetCandidateListIndexRange();

            Assert::AreEqual(10u, engine.GetCandidatePageSize());
            Assert::AreEqual(std::wstring(L"1234567890"), Printables(pRange));
            Assert::AreEqual(std::wstring(L"1234567890"), Labels(pRange));
            Assert::AreEqual(0, IndexOf(pRange, '1', L'1'));
            Assert::AreEqual(9, IndexOf(pRange, '0', L'0'));
            Assert::AreEqual(-1, IndexOf(pRange, VK_SPACE, L' '), L"space is not a slot");
            Assert::AreEqual(-1, IndexOf(pRange, VK_OEM_7, L'\''));
        }

        TEST_METHOD(FromTable_Fallback_KeepsSpaceFirst)
        {
            // No table loaded -> the built-in Dayi fallback with space as slot 1
            // (unchanged behaviour).
            CCompositionProcessorEngine engine(nullptr);
            SetupDayi(engine, DAYI_SELKEY_MODE::DAYI_SELKEY_FROM_TABLE);
            CCandidateRange* pRange = engine.GetCandidateListIndexRange();

            Assert::AreEqual(L' ', pRange->GetAt(0)->Printable);
            Assert::AreEqual(1, IndexOf(pRange, VK_OEM_7, L'\''), L"' is the second slot after space");
        }

        TEST_METHOD(NonDayiMode_IgnoresDayiSelkeyMode)
        {
            CCompositionProcessorEngine engine(nullptr);
            Global::imeMode = IME_MODE::IME_MODE_GENERIC;
            CConfig::SetSpaceAsFirstCaniSelkey(FALSE);
            CConfig::SetDayiSelkeyMode(DAYI_SELKEY_MODE::DAYI_SELKEY_DAYI);
            engine.SetupCandidateListRange(IME_MODE::IME_MODE_GENERIC);

            Assert::AreEqual(std::wstring(L"1234567890"), Printables(engine.GetCandidateListIndexRange()));
        }

        TEST_METHOD(DayiKeys_CandidateWindowRouting)
        {
            CCompositionProcessorEngine engine(nullptr);
            SetupDayi(engine, DAYI_SELKEY_MODE::DAYI_SELKEY_DAYI);
            _KEYSTROKE_STATE state;

            WCHAR wch = L'\'';
            Assert::IsTrue(engine.IsVirtualKeyNeed(VK_OEM_7, &wch, FALSE,
                CANDIDATE_MODE::CANDIDATE_ORIGINAL, FALSE, 3, 0, &state) == TRUE);
            Assert::IsTrue(state.Function == KEYSTROKE_FUNCTION::FUNCTION_SELECT_BY_NUMBER,
                L"' selects a candidate while the window is open");

            Global::ModifiersValue = TF_MOD_SHIFT;
            wch = L'_';
            Assert::IsTrue(engine.IsVirtualKeyNeed(VK_OEM_MINUS, &wch, FALSE,
                CANDIDATE_MODE::CANDIDATE_ORIGINAL, FALSE, 12, 0, &state) == TRUE);
            Assert::IsTrue(state.Function == KEYSTROKE_FUNCTION::FUNCTION_SELECT_BY_NUMBER,
                L"Shift+- selects the 9th candidate");
            Global::ModifiersValue = 0;

            wch = L' ';
            Assert::IsTrue(engine.IsVirtualKeyNeed(VK_SPACE, &wch, FALSE,
                CANDIDATE_MODE::CANDIDATE_ORIGINAL, FALSE, 3, 0, &state) == TRUE);
            Assert::IsTrue(state.Function == KEYSTROKE_FUNCTION::FUNCTION_CONVERT,
                L"space converts the highlighted (first) candidate");
        }
    };
}
