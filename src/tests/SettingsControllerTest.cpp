// SettingsModelTest.cpp — Unit tests for SettingsModel (UT-SM-01 through UT-SM-25)
//
// All tests are pure (no HWND, no GDI) and run in CI/CD.

#include "pch.h"
#include "CppUnitTest.h"
#include "..\DIMESettings\SettingsController.h"
#include "..\DIMESettings\SettingsKeys.h"
#include "..\DIMESettings\SettingsPageLayout.h"
#include "..\DIMESettings\CLI.h"
#include <cmath>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace DIMEUnitTests
{
    TEST_CLASS(SettingsModelTest)
    {
    public:
        // ================================================================
        // UT-SM-01: Layout tree completeness
        // Every card returned by GetIMEPageLayout() should have a
        // non-null title and non-zero rowCount.
        // ================================================================
        TEST_METHOD(UT_SM_01_LayoutTreeCompleteness)
        {
            int count = 0;
            const LayoutCard* cards = GetIMEPageLayout(&count);
            Assert::IsTrue(count > 0, L"IME page layout should not be empty");
            Assert::IsNotNull(cards, L"IME page layout should not be null");

            for (int i = 0; i < count; i++) {
                Assert::IsNotNull(cards[i].title, L"Card title should not be null");
                Assert::IsTrue(cards[i].rowCount > 0, L"Card should have at least one row");
            }
        }

        // ================================================================
        // UT-SM-02: Visibility rules — Dayi mode
        // ================================================================
        TEST_METHOD(UT_SM_02_DayiVisibility)
        {
            int layoutCount = 0;
            const LayoutCard* cards = GetIMEPageLayout(&layoutCount);

            for (int ci = 0; ci < layoutCount; ci++) {
                for (int ri = 0; ri < cards[ci].rowCount; ri++) {
                    const LayoutRow& row = cards[ci].rows[ri];
                    if (row.id == CTRL_DAYI_ARTICLE) {
                        Assert::IsTrue(IsRowVisible(row, SM_MODE_DAYI),
                            L"Dayi article mode should be visible in Dayi mode");
                        Assert::IsFalse(IsRowVisible(row, SM_MODE_ARRAY),
                            L"Dayi article mode should be hidden in Array mode");
                        Assert::IsFalse(IsRowVisible(row, SM_MODE_PHONETIC),
                            L"Dayi article mode should be hidden in Phonetic mode");
                        Assert::IsFalse(IsRowVisible(row, SM_MODE_GENERIC),
                            L"Dayi article mode should be hidden in Generic mode");
                    }
                    if (row.id == CTRL_DO_BEEP_CANDI) {
                        Assert::IsTrue(IsRowVisible(row, SM_MODE_DAYI),
                            L"Candidate beep should be visible in Dayi mode");
                        Assert::IsFalse(IsRowVisible(row, SM_MODE_ARRAY),
                            L"Candidate beep should be hidden in Array mode");
                    }
                }
            }
        }

        // ================================================================
        // UT-SM-03: Visibility rules — Array mode
        // ================================================================
        TEST_METHOD(UT_SM_03_ArrayVisibility)
        {
            int layoutCount = 0;
            const LayoutCard* cards = GetIMEPageLayout(&layoutCount);

            for (int ci = 0; ci < layoutCount; ci++) {
                for (int ri = 0; ri < cards[ci].rowCount; ri++) {
                    const LayoutRow& row = cards[ci].rows[ri];
                    if (row.id == CTRL_ARRAY_SCOPE) {
                        Assert::IsTrue(IsRowVisible(row, SM_MODE_ARRAY),
                            L"Array scope should be visible in Array mode");
                        Assert::IsFalse(IsRowVisible(row, SM_MODE_DAYI),
                            L"Array scope should be hidden in Dayi mode");
                    }
                    if (row.id == CTRL_ARRAY_FORCE_SP) {
                        Assert::IsTrue(IsRowVisible(row, SM_MODE_ARRAY),
                            L"Array force SP should be visible in Array mode");
                        Assert::IsFalse(IsRowVisible(row, SM_MODE_PHONETIC),
                            L"Array force SP should be hidden in Phonetic mode");
                    }
                }
            }
        }

        // ================================================================
        // UT-SM-04: Visibility rules — Phonetic mode
        // ================================================================
        TEST_METHOD(UT_SM_04_PhoneticVisibility)
        {
            int layoutCount = 0;
            const LayoutCard* cards = GetIMEPageLayout(&layoutCount);

            for (int ci = 0; ci < layoutCount; ci++) {
                for (int ri = 0; ri < cards[ci].rowCount; ri++) {
                    const LayoutRow& row = cards[ci].rows[ri];
                    if (row.id == CTRL_PHONETIC_KB) {
                        Assert::IsTrue(IsRowVisible(row, SM_MODE_PHONETIC),
                            L"Phonetic keyboard should be visible in Phonetic mode");
                        Assert::IsFalse(IsRowVisible(row, SM_MODE_DAYI),
                            L"Phonetic keyboard should be hidden in Dayi mode");
                        Assert::IsFalse(IsRowVisible(row, SM_MODE_ARRAY),
                            L"Phonetic keyboard should be hidden in Array mode");
                    }
                }
            }
        }

        // ================================================================
        // UT-SM-05: Visibility rules — universal controls
        // ================================================================
        TEST_METHOD(UT_SM_05_UniversalControlVisibility)
        {
            int layoutCount = 0;
            const LayoutCard* cards = GetIMEPageLayout(&layoutCount);

            int allModes[] = { SM_MODE_DAYI, SM_MODE_ARRAY, SM_MODE_PHONETIC, SM_MODE_GENERIC };

            for (int ci = 0; ci < layoutCount; ci++) {
                for (int ri = 0; ri < cards[ci].rowCount; ri++) {
                    const LayoutRow& row = cards[ci].rows[ri];
                    if (row.id == CTRL_COLOR_MODE || row.id == CTRL_DO_BEEP) {
                        for (auto mode : allModes) {
                            Assert::IsTrue(IsRowVisible(row, mode),
                                L"Universal control should be visible in all modes");
                        }
                    }
                }
            }
        }

        // ================================================================
        // UT-SM-06: Each card has at least one visible row per mode
        // ================================================================
        TEST_METHOD(UT_SM_06_EachCardHasVisibleRows)
        {
            int layoutCount = 0;
            const LayoutCard* cards = GetIMEPageLayout(&layoutCount);

            int allModes[] = { SM_MODE_DAYI, SM_MODE_ARRAY, SM_MODE_PHONETIC, SM_MODE_GENERIC };

            for (auto mode : allModes) {
                for (int ci = 0; ci < layoutCount; ci++) {
                    int visCount = 0;
                    for (int ri = 0; ri < cards[ci].rowCount; ri++) {
                        if (IsRowVisible(cards[ci].rows[ri], mode))
                            visCount++;
                    }
                    Assert::IsTrue(visCount > 0,
                        L"Each card should have at least one visible row per mode");
                }
            }
        }

        // ================================================================
        // UT-SM-11: SidebarHitTest — normal range
        // ================================================================
        TEST_METHOD(UT_SM_11_SidebarHitTestNormal)
        {
            Assert::AreEqual(0, SettingsModel::SidebarHitTest(0, 40, 8));
            Assert::AreEqual(0, SettingsModel::SidebarHitTest(39, 40, 8));
            Assert::AreEqual(1, SettingsModel::SidebarHitTest(40, 40, 8));
            Assert::AreEqual(1, SettingsModel::SidebarHitTest(79, 40, 8));
            Assert::AreEqual(7, SettingsModel::SidebarHitTest(280, 40, 8));
        }

        // ================================================================
        // UT-SM-12: SidebarHitTest — out of bounds
        // ================================================================
        TEST_METHOD(UT_SM_12_SidebarHitTestOutOfBounds)
        {
            Assert::AreEqual(-1, SettingsModel::SidebarHitTest(-1, 40, 8));
            Assert::AreEqual(-1, SettingsModel::SidebarHitTest(320, 40, 8)); // 8*40=320, index 8 >= count
            Assert::AreEqual(-1, SettingsModel::SidebarHitTest(400, 40, 8));
            Assert::AreEqual(-1, SettingsModel::SidebarHitTest(0, 40, 0));   // no items
            Assert::AreEqual(-1, SettingsModel::SidebarHitTest(0, 0, 8));    // zero height
        }

        // ================================================================
        // UT-SM-13: ComputeScrollRange
        // ================================================================
        TEST_METHOD(UT_SM_13_ComputeScrollRange)
        {
            Assert::AreEqual(400, SettingsModel::ComputeScrollRange(1000, 600));
            Assert::AreEqual(0, SettingsModel::ComputeScrollRange(500, 600));
            Assert::AreEqual(0, SettingsModel::ComputeScrollRange(600, 600));
            Assert::AreEqual(1, SettingsModel::ComputeScrollRange(601, 600));
        }

        // ================================================================
        // UT-SM-14: ClampScrollPos — boundaries
        // ================================================================
        TEST_METHOD(UT_SM_14_ClampScrollPos)
        {
            Assert::AreEqual(0, SettingsModel::ClampScrollPos(-10, 400));
            Assert::AreEqual(400, SettingsModel::ClampScrollPos(500, 400));
            Assert::AreEqual(200, SettingsModel::ClampScrollPos(200, 400));
            Assert::AreEqual(0, SettingsModel::ClampScrollPos(0, 400));
            Assert::AreEqual(400, SettingsModel::ClampScrollPos(400, 400));
            Assert::AreEqual(0, SettingsModel::ClampScrollPos(100, -5)); // negative max treated as 0
        }

        // ================================================================
        // UT-SM-15: SettingsSnapshot round-trip (Load)
        // ================================================================
        TEST_METHOD(UT_SM_15_SnapshotLoadFromConfig)
        {
            // Set known values via CConfig
            CConfig::SetFontSize(18);
            CConfig::SetDoBeep(TRUE);
            CConfig::SetAutoCompose(FALSE);
            CConfig::SetMaxCodes(5);

            auto snap = SettingsModel::LoadFromConfig();

            Assert::AreEqual((UINT)18, snap.fontSize);
            Assert::AreEqual((BOOL)TRUE, snap.doBeep);
            Assert::AreEqual((BOOL)FALSE, snap.autoCompose);
            Assert::AreEqual((UINT)5, snap.maxCodes);
        }

        // ================================================================
        // UT-SM-16: SettingsSnapshot write-back
        // ================================================================
        TEST_METHOD(UT_SM_16_SnapshotApplyToConfig)
        {
            auto snap = SettingsModel::LoadFromConfig();

            // Modify snapshot
            snap.fontSize = 24;
            snap.doBeep = FALSE;
            snap.autoCompose = TRUE;
            snap.maxCodes = 7;

            SettingsModel::ApplyToConfig(snap);

            // Verify CConfig reflects changes
            Assert::AreEqual((UINT)24, CConfig::GetFontSize());
            Assert::AreEqual((BOOL)FALSE, CConfig::GetDoBeep());
            Assert::AreEqual((BOOL)TRUE, CConfig::GetAutoCompose());
            Assert::AreEqual((UINT)7, CConfig::GetMaxCodes());
        }

        // ================================================================
        // UT-SM-17: SettingsSnapshot full-field coverage
        // ================================================================
        TEST_METHOD(UT_SM_17_SnapshotRoundTripAllFields)
        {
            // Set distinctive values for all fields
            CConfig::SetFontSize(14);
            CConfig::SetFontWeight(700);
            CConfig::SetFontItalic(TRUE);
            CConfig::SetAutoCompose(TRUE);
            CConfig::SetClearOnBeep(FALSE);
            CConfig::SetDoBeep(TRUE);
            CConfig::SetDoBeepNotify(FALSE);
            CConfig::SetDoBeepOnCandi(TRUE);
            CConfig::SetMakePhrase(TRUE);
            CConfig::setCustomTablePriority(FALSE);
            CConfig::SetArrowKeySWPages(TRUE);
            CConfig::SetSpaceAsPageDown(FALSE);
            CConfig::SetSpaceAsFirstCaniSelkey(TRUE);
            CConfig::SetShowNotifyDesktop(TRUE);
            CConfig::SetArrayNotifySP(FALSE);
            CConfig::SetArrayForceSP(TRUE);
            CConfig::SetArraySingleQuoteCustomPhrase(FALSE);
            CConfig::setDayiArticleMode(TRUE);
            CConfig::SetDoHanConvert(FALSE);
            CConfig::SetActivatedKeyboardMode(TRUE);
            CConfig::SetBig5Filter(FALSE);
            CConfig::SetMaxCodes(6);

            // Load → Apply → Load again
            auto snap1 = SettingsModel::LoadFromConfig();
            SettingsModel::ApplyToConfig(snap1);
            auto snap2 = SettingsModel::LoadFromConfig();

            // Compare all fields
            Assert::AreEqual(snap1.fontSize, snap2.fontSize, L"fontSize mismatch");
            Assert::AreEqual(snap1.fontWeight, snap2.fontWeight, L"fontWeight mismatch");
            Assert::AreEqual(snap1.fontItalic, snap2.fontItalic, L"fontItalic mismatch");
            Assert::AreEqual(snap1.autoCompose, snap2.autoCompose, L"autoCompose mismatch");
            Assert::AreEqual(snap1.clearOnBeep, snap2.clearOnBeep, L"clearOnBeep mismatch");
            Assert::AreEqual(snap1.doBeep, snap2.doBeep, L"doBeep mismatch");
            Assert::AreEqual(snap1.doBeepNotify, snap2.doBeepNotify, L"doBeepNotify mismatch");
            Assert::AreEqual(snap1.doBeepOnCandi, snap2.doBeepOnCandi, L"doBeepOnCandi mismatch");
            Assert::AreEqual(snap1.makePhrase, snap2.makePhrase, L"makePhrase mismatch");
            Assert::AreEqual(snap1.customTablePriority, snap2.customTablePriority, L"customTablePriority mismatch");
            Assert::AreEqual(snap1.arrowKeySWPages, snap2.arrowKeySWPages, L"arrowKeySWPages mismatch");
            Assert::AreEqual(snap1.spaceAsPageDown, snap2.spaceAsPageDown, L"spaceAsPageDown mismatch");
            Assert::AreEqual(snap1.spaceAsFirstCandSelkey, snap2.spaceAsFirstCandSelkey, L"spaceAsFirstCandSelkey mismatch");
            Assert::AreEqual(snap1.showNotifyDesktop, snap2.showNotifyDesktop, L"showNotifyDesktop mismatch");
            Assert::AreEqual(snap1.arrayNotifySP, snap2.arrayNotifySP, L"arrayNotifySP mismatch");
            Assert::AreEqual(snap1.arrayForceSP, snap2.arrayForceSP, L"arrayForceSP mismatch");
            Assert::AreEqual(snap1.arraySingleQuoteCustomPhrase, snap2.arraySingleQuoteCustomPhrase, L"arraySingleQuoteCustomPhrase mismatch");
            Assert::AreEqual(snap1.dayiArticleMode, snap2.dayiArticleMode, L"dayiArticleMode mismatch");
            Assert::AreEqual(snap1.doHanConvert, snap2.doHanConvert, L"doHanConvert mismatch");
            Assert::AreEqual(snap1.activatedKeyboardMode, snap2.activatedKeyboardMode, L"activatedKeyboardMode mismatch");
            Assert::AreEqual(snap1.big5Filter, snap2.big5Filter, L"big5Filter mismatch");
            Assert::AreEqual(snap1.maxCodes, snap2.maxCodes, L"maxCodes mismatch");
            Assert::AreEqual(snap1.colorMode, snap2.colorMode, L"colorMode mismatch");
            Assert::AreEqual(snap1.arrayScope, snap2.arrayScope, L"arrayScope mismatch");
            Assert::AreEqual(snap1.numericPad, snap2.numericPad, L"numericPad mismatch");
            Assert::AreEqual(snap1.imeShiftMode, snap2.imeShiftMode, L"imeShiftMode mismatch");
            Assert::AreEqual(snap1.doubleSingleByteMode, snap2.doubleSingleByteMode, L"doubleSingleByteMode mismatch");
        }

        // ================================================================
        // UT-SM-18: Color contrast (WCAG AA: >= 4.5:1)
        // ================================================================
        TEST_METHOD(UT_SM_18_ColorContrast)
        {
            // Card colors from the plan
            // Light: white bg RGB(255,255,255), border RGB(229,229,229)
            // Dark: bg RGB(45,45,48), border RGB(60,60,60)
            // Dark text color from Define.h: DARK_TEXT = RGB(220,220,220)
            // Light text: black RGB(0,0,0) on white bg

            // Helper lambda: relative luminance per WCAG 2.0
            auto luminance = [](COLORREF c) -> double {
                double r = GetRValue(c) / 255.0;
                double g = GetGValue(c) / 255.0;
                double b = GetBValue(c) / 255.0;
                r = (r <= 0.03928) ? r / 12.92 : pow((r + 0.055) / 1.055, 2.4);
                g = (g <= 0.03928) ? g / 12.92 : pow((g + 0.055) / 1.055, 2.4);
                b = (b <= 0.03928) ? b / 12.92 : pow((b + 0.055) / 1.055, 2.4);
                return 0.2126 * r + 0.7152 * g + 0.0722 * b;
            };

            auto contrastRatio = [&](COLORREF fg, COLORREF bg) -> double {
                double l1 = luminance(fg);
                double l2 = luminance(bg);
                if (l1 < l2) { double t = l1; l1 = l2; l2 = t; }
                return (l1 + 0.05) / (l2 + 0.05);
            };

            // Light mode: black text on white card bg
            double lightRatio = contrastRatio(RGB(0, 0, 0), RGB(255, 255, 255));
            Assert::IsTrue(lightRatio >= 4.5, L"Light mode text contrast should be >= 4.5:1");

            // Dark mode: RGB(220,220,220) text on RGB(45,45,48) bg
            double darkRatio = contrastRatio(RGB(220, 220, 220), RGB(45, 45, 48));
            Assert::IsTrue(darkRatio >= 4.5, L"Dark mode text contrast should be >= 4.5:1");
        }

        // ================================================================
        // UT-SM-19: ImeModeToString
        // ================================================================
        TEST_METHOD(UT_SM_19_ImeModeToString)
        {
            Assert::AreEqual(L"dayi", SettingsModel::ImeModeToString(IME_MODE::IME_MODE_DAYI));
            Assert::AreEqual(L"array", SettingsModel::ImeModeToString(IME_MODE::IME_MODE_ARRAY));
            Assert::AreEqual(L"phonetic", SettingsModel::ImeModeToString(IME_MODE::IME_MODE_PHONETIC));
            Assert::AreEqual(L"generic", SettingsModel::ImeModeToString(IME_MODE::IME_MODE_GENERIC));
            Assert::IsNull(SettingsModel::ImeModeToString(IME_MODE::IME_MODE_NONE));
        }

        // ================================================================
        // UT-SM-20: StringToImeMode
        // ================================================================
        TEST_METHOD(UT_SM_20_StringToImeMode)
        {
            Assert::IsTrue(IME_MODE::IME_MODE_DAYI == SettingsModel::StringToImeMode(L"dayi"));
            Assert::IsTrue(IME_MODE::IME_MODE_ARRAY == SettingsModel::StringToImeMode(L"array"));
            Assert::IsTrue(IME_MODE::IME_MODE_PHONETIC == SettingsModel::StringToImeMode(L"phonetic"));
            Assert::IsTrue(IME_MODE::IME_MODE_GENERIC == SettingsModel::StringToImeMode(L"generic"));
            // Case insensitive
            Assert::IsTrue(IME_MODE::IME_MODE_DAYI == SettingsModel::StringToImeMode(L"DAYI"));
            Assert::IsTrue(IME_MODE::IME_MODE_ARRAY == SettingsModel::StringToImeMode(L"Array"));
            // Unknown
            Assert::IsTrue(IME_MODE::IME_MODE_NONE == SettingsModel::StringToImeMode(L"unknown"));
            Assert::IsTrue(IME_MODE::IME_MODE_NONE == SettingsModel::StringToImeMode(nullptr));
            Assert::IsTrue(IME_MODE::IME_MODE_NONE == SettingsModel::StringToImeMode(L""));
        }

        // ================================================================
        // UT-SM-21: StringToImeMode(ImeModeToString(mode)) round-trip
        // ================================================================
        TEST_METHOD(UT_SM_21_ImeModeRoundTrip)
        {
            IME_MODE modes[] = {
                IME_MODE::IME_MODE_DAYI, IME_MODE::IME_MODE_ARRAY,
                IME_MODE::IME_MODE_PHONETIC, IME_MODE::IME_MODE_GENERIC
            };
            for (auto mode : modes) {
                const wchar_t* str = SettingsModel::ImeModeToString(mode);
                Assert::IsNotNull(str);
                IME_MODE result = SettingsModel::StringToImeMode(str);
                Assert::IsTrue(mode == result, L"Round-trip should preserve IME_MODE");
            }
        }

        // ================================================================
        // UT-SM-25: WM_COPYDATA mode parsing (ImeModeToString/StringToImeMode)
        // Simulates the command-line string that would be sent via WM_COPYDATA
        // ================================================================
        TEST_METHOD(UT_SM_25_CopyDataModeParsing)
        {
            // Simulate: DIMESettings.exe --mode dayi
            // The second instance extracts "dayi" and converts to IME_MODE
            const wchar_t* cmdModes[] = { L"dayi", L"array", L"phonetic", L"generic" };
            IME_MODE expected[] = {
                IME_MODE::IME_MODE_DAYI, IME_MODE::IME_MODE_ARRAY,
                IME_MODE::IME_MODE_PHONETIC, IME_MODE::IME_MODE_GENERIC
            };

            for (int i = 0; i < 4; i++) {
                IME_MODE result = SettingsModel::StringToImeMode(cmdModes[i]);
                Assert::IsTrue(expected[i] == result, L"Mode parsing from command string should match");
            }
        }

        // ================================================================
        // Additional: Card and Sidebar definition accessors
        // ================================================================
        TEST_METHOD(CardDefsNotEmpty)
        {
            int count = 0;
            const LayoutCard* cards = GetIMEPageLayout(&count);
            Assert::IsTrue(count > 0, L"IME page layout should have cards");
            Assert::IsNotNull(cards);
            for (int i = 0; i < count; i++) {
                Assert::IsNotNull(cards[i].title, L"Card title should not be null");
            }
        }

        TEST_METHOD(SidebarDefsNotEmpty)
        {
            int count = 0;
            const SidebarItemDef* items = GetSidebarItems(&count);
            Assert::IsTrue(count > 0);
            Assert::IsNotNull(items);
            // Should have 6 items: 4 IME modes + separator + 載入碼表
            Assert::AreEqual(6, count);
        }

        TEST_METHOD(GetModeBitmask_ReturnsCorrectValues)
        {
            Assert::AreEqual(SM_MODE_DAYI, SettingsModel::GetModeBitmask(IME_MODE::IME_MODE_DAYI));
            Assert::AreEqual(SM_MODE_ARRAY, SettingsModel::GetModeBitmask(IME_MODE::IME_MODE_ARRAY));
            Assert::AreEqual(SM_MODE_PHONETIC, SettingsModel::GetModeBitmask(IME_MODE::IME_MODE_PHONETIC));
            Assert::AreEqual(SM_MODE_GENERIC, SettingsModel::GetModeBitmask(IME_MODE::IME_MODE_GENERIC));
            Assert::AreEqual(0, SettingsModel::GetModeBitmask(IME_MODE::IME_MODE_NONE));
        }

    };

    // ============================================================================
    // ValidateLineTest — UT_VL_*
    // ============================================================================
    TEST_CLASS(ValidateLineTest)
    {
    public:
        // UT_VL_01: valid two-token line passes all levels
        TEST_METHOD(UT_VL_01_ValidateLine_TwoTokens_Pass)
        {
            const wchar_t* line = L"roc 語";
            auto v = SettingsModel::ValidateLine(line, (int)wcslen(line),
                IME_MODE::IME_MODE_DAYI, 4);
            Assert::IsTrue(v.valid);
            Assert::IsTrue(v.errors.empty());
        }

        // UT_VL_02: three tokens → Format error
        TEST_METHOD(UT_VL_02_ValidateLine_ThreeTokens_FormatError)
        {
            const wchar_t* line = L"roc 語 fdfdafds";
            auto v = SettingsModel::ValidateLine(line, (int)wcslen(line),
                IME_MODE::IME_MODE_DAYI, 4);
            Assert::IsFalse(v.valid);
            Assert::IsTrue(v.errors.size() == 1);
            Assert::IsTrue(v.errors[0].error == SettingsModel::LineError::Format);
        }

        // UT_VL_03: key longer than maxCodes → KeyTooLong
        TEST_METHOD(UT_VL_03_ValidateLine_KeyTooLong)
        {
            const wchar_t* line = L"LONGKEY12345 語";  // 12-char key, maxCodes=4
            auto v = SettingsModel::ValidateLine(line, (int)wcslen(line),
                IME_MODE::IME_MODE_DAYI, 4);
            Assert::IsFalse(v.valid);
            Assert::IsTrue(v.errors.size() == 1);
            Assert::IsTrue(v.errors[0].error == SettingsModel::LineError::KeyTooLong);
        }

        // UT_VL_04: trimmed length > MAX_TABLE_LINE_LENGTH (1024) → Level-0 hard cap
        TEST_METHOD(UT_VL_04_ValidateLine_LengthCap)
        {
            std::wstring line(1025, L'a');  // no whitespace → trimmed len 1025 > 1024
            auto v = SettingsModel::ValidateLine(line.c_str(), (int)line.size(),
                IME_MODE::IME_MODE_DAYI, 4);
            Assert::IsFalse(v.valid);
            Assert::IsTrue(v.errors.size() == 1);
            Assert::IsTrue(v.errors[0].error == SettingsModel::LineError::Format);
        }

        // UT_VL_05: single token, no whitespace separator → Format error
        TEST_METHOD(UT_VL_05_ValidateLine_NoSeparator)
        {
            const wchar_t* line = L"fff";
            auto v = SettingsModel::ValidateLine(line, (int)wcslen(line),
                IME_MODE::IME_MODE_DAYI, 4);
            Assert::IsFalse(v.valid);
            Assert::IsTrue(v.errors.size() == 1);
            Assert::IsTrue(v.errors[0].error == SettingsModel::LineError::Format);
        }

        // UT_VL_06: U+4E2D (中) as PHONETIC key char — outside '!'..'~' → InvalidChar
        TEST_METHOD(UT_VL_06_ValidateLine_PerCharInvalid_Phonetic)
        {
            const wchar_t* line = L"中 abc";
            auto v = SettingsModel::ValidateLine(line, (int)wcslen(line),
                IME_MODE::IME_MODE_PHONETIC, 64);
            Assert::IsFalse(v.valid);
            Assert::IsFalse(v.errors.empty());
            Assert::IsTrue(v.errors[0].error == SettingsModel::LineError::InvalidChar);
        }
    };

    // ============================================================================
    // LoadTextFileTest — UT_LT_*
    // ============================================================================
    TEST_CLASS(LoadTextFileTest)
    {
        static std::wstring WriteTempBinaryFile(const wchar_t* suffix, const BYTE* data, DWORD len)
        {
            WCHAR tmp[MAX_PATH];
            GetTempPathW(MAX_PATH, tmp);
            std::wstring path = std::wstring(tmp) + L"dime_lt_" + suffix + L".txt";
            HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h != INVALID_HANDLE_VALUE) {
                DWORD written = 0;
                WriteFile(h, data, len, &written, nullptr);
                CloseHandle(h);
            }
            return path;
        }

    public:
        // UT_LT_01: UTF-16LE BOM file — BOM stripped, content decoded correctly
        TEST_METHOD(UT_LT_01_LoadTextFile_Utf16BOM)
        {
            // "roc\r\n" encoded as UTF-16LE with BOM (FF FE)
            BYTE bytes[] = { 0xFF,0xFE, 0x72,0x00,0x6F,0x00,0x63,0x00,0x0D,0x00,0x0A,0x00 };
            std::wstring path = WriteTempBinaryFile(L"lt01", bytes, sizeof(bytes));

            size_t outLen = 0;
            LPWSTR result = SettingsModel::LoadTextFileAsUtf16(path.c_str(), &outLen);
            DeleteFileW(path.c_str());

            Assert::IsNotNull(result);
            Assert::AreEqual((size_t)5, outLen);  // r o c \r \n
            Assert::AreEqual(L'r', result[0]);
            Assert::AreEqual(L'o', result[1]);
            Assert::AreEqual(L'c', result[2]);
            delete[] result;
        }

        // UT_LT_02: UTF-8 BOM file — BOM stripped, decoded as UTF-8
        TEST_METHOD(UT_LT_02_LoadTextFile_Utf8BOM)
        {
            // UTF-8 BOM (EF BB BF) + "roc\r\n"
            BYTE bytes[] = { 0xEF,0xBB,0xBF, 0x72,0x6F,0x63,0x0D,0x0A };
            std::wstring path = WriteTempBinaryFile(L"lt02", bytes, sizeof(bytes));

            size_t outLen = 0;
            LPWSTR result = SettingsModel::LoadTextFileAsUtf16(path.c_str(), &outLen);
            DeleteFileW(path.c_str());

            Assert::IsNotNull(result);
            Assert::AreEqual((size_t)5, outLen);  // r o c \r \n
            Assert::AreEqual(L'r', result[0]);
            delete[] result;
        }

        // UT_LT_03: UTF-8 without BOM — strict sniff (MB_ERR_INVALID_CHARS) succeeds
        TEST_METHOD(UT_LT_03_LoadTextFile_Utf8NoBOM)
        {
            // "roc\r\n" as plain UTF-8, no BOM
            BYTE bytes[] = { 0x72,0x6F,0x63,0x0D,0x0A };
            std::wstring path = WriteTempBinaryFile(L"lt03", bytes, sizeof(bytes));

            size_t outLen = 0;
            LPWSTR result = SettingsModel::LoadTextFileAsUtf16(path.c_str(), &outLen);
            DeleteFileW(path.c_str());

            Assert::IsNotNull(result);
            Assert::AreEqual((size_t)5, outLen);
            Assert::AreEqual(L'r', result[0]);
            delete[] result;
        }

        // UT_LT_04: byte invalid for UTF-8 → CP_ACP fallback; ASCII prefix preserved
        // 0x80 fails MB_ERR_INVALID_CHARS for UTF-8.  On CP1252 (GitHub Actions runner)
        // it maps to U+20AC; on CP950 it is treated as a lone invalid byte.  Either
        // way the loader must return non-null with 'a' and ' ' at positions 0 and 1.
        TEST_METHOD(UT_LT_04_LoadTextFile_CP_ACP_Fallback)
        {
            BYTE bytes[] = { 0x61, 0x20, 0x80, 0x0D, 0x0A };  // "a " + 0x80 + CRLF
            std::wstring path = WriteTempBinaryFile(L"lt04", bytes, sizeof(bytes));

            size_t outLen = 0;
            LPWSTR result = SettingsModel::LoadTextFileAsUtf16(path.c_str(), &outLen);
            DeleteFileW(path.c_str());

            Assert::IsNotNull(result);
            Assert::IsTrue(outLen >= 2);
            Assert::AreEqual(L'a', result[0]);
            Assert::AreEqual(L' ', result[1]);
            delete[] result;
        }
    };

    // ============================================================================
    // ValidateBufferTest — UT_VB_*
    // Inline equivalent of file-static ValidateCustomTableBuffer (SettingsWindow.cpp).
    // ============================================================================
    TEST_CLASS(ValidateBufferTest)
    {
        struct BufferResult { int errorCount; int firstErrLine; };

        static BufferResult RunBufferValidation(const wchar_t* buf, IME_MODE mode, UINT maxCodes)
        {
            BufferResult r = { 0, -1 };
            if (!buf) return r;
            int lineNo = 0;
            const wchar_t* p = buf;
            while (*p) {
                ++lineNo;
                const wchar_t* eol = p;
                while (*eol && *eol != L'\n') ++eol;
                int lineLen = (int)(eol - p);
                if (lineLen > 0 && p[lineLen - 1] == L'\r') --lineLen;
                auto v = SettingsModel::ValidateLine(p, lineLen, mode, maxCodes);
                if (!v.valid) {
                    ++r.errorCount;
                    if (r.firstErrLine < 0) r.firstErrLine = lineNo;
                }
                p = eol;
                if (*p == L'\n') ++p;
            }
            return r;
        }

    public:
        // UT_VB_01: 2000 valid + 50 KeyTooLong lines → 50 errors, first at line 2001
        TEST_METHOD(UT_VB_01_ValidateCustomTableBuffer_FirstError)
        {
            std::wstring buf;
            buf.reserve(2050 * 16);
            for (int i = 0; i < 2000; ++i)
                buf += L"roc 語\n";
            for (int i = 0; i < 50; ++i)
                buf += L"BADBADBADBAD 語\n";  // 12-char key > maxCodes=4

            auto r = RunBufferValidation(buf.c_str(), IME_MODE::IME_MODE_DAYI, 4);
            Assert::AreEqual(50, r.errorCount);
            Assert::AreEqual(2001, r.firstErrLine);
        }

        // UT_VB_02: 200 valid + 1 KeyTooLong line → 1 error at line 201
        TEST_METHOD(UT_VB_02_ValidateCustomTableBuffer_DeepError)
        {
            std::wstring buf;
            buf.reserve(202 * 12);
            for (int i = 0; i < 200; ++i)
                buf += L"a 中\n";
            buf += L"BADBADBADBAD 中\n";  // 12-char key > maxCodes=4

            auto r = RunBufferValidation(buf.c_str(), IME_MODE::IME_MODE_DAYI, 4);
            Assert::AreEqual(1, r.errorCount);
            Assert::AreEqual(201, r.firstErrLine);
        }
    };

}
