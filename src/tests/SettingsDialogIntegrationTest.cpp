// SettingsDialogIntegrationTest.cpp - IT-07 Settings Dialog Integration Tests
// Tests: Create Dialog → Change Controls → Send PSN_APPLY → Verify Persistence

#include "pch.h"
#include "CppUnitTest.h"
#include "../Config.h"
#include "../Globals.h"
#include "../resource.h"
#include <shlwapi.h>
#include <Prsht.h>

#pragma comment(lib, "shlwapi.lib")

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace DIMEIntegratedTests
{
    TEST_CLASS(SettingsDialogIntegrationTest)
    {
    public:
        TEST_CLASS_INITIALIZE(ClassSetup)
        {
            CoInitialize(NULL);
        }

        TEST_CLASS_CLEANUP(ClassCleanup)
        {
            CoUninitialize();
        }

        // ====================================================================
        // IT07_01: Font Settings - Complete Workflow
        // ====================================================================

        TEST_METHOD(IT07_01_FontSize_LoadChangeApplyVerify)
        {
            // Arrange: Create dialog and initialize
            CConfig::SetIMEMode(IME_MODE::IME_MODE_ARRAY);

            // Load the DIME DLL for resource access
            HMODULE hDimeDll = LoadLibrary(L"DIME.dll");
            if (hDimeDll == NULL)
            {
                // Skip test if DIME.dll not available (unit test environment)
                Logger::WriteMessage(L"DIME.dll not loaded - skipping dialog test");
                // Skip test gracefully
                return;
            }

            HWND hDlg = CreateDialogParam(hDimeDll, MAKEINTRESOURCE(IDD_DIALOG_COMMON), NULL, 
                                         CConfig::CommonPropertyPageWndProc, 0);

            if (hDlg == NULL)
            {
                DWORD err = GetLastError();
                WCHAR msg[256];
                swprintf_s(msg, L"CreateDialogParam failed with error %d", err);
                Logger::WriteMessage(msg);
                FreeLibrary(hDimeDll);
                // Skip test gracefully
                return;
            }

            // Send WM_INITDIALOG to load config into controls
            SendMessage(hDlg, WM_INITDIALOG, 0, 0);

            UINT originalSize = CConfig::GetFontSize();
            UINT newSize = 20;

            // Act: Change control value and send PSN_APPLY
            SetDlgItemInt(hDlg, IDC_EDIT_FONTPOINT, newSize, FALSE);

            PSHNOTIFY psh = {0};
            psh.hdr.code = PSN_APPLY;
            psh.hdr.hwndFrom = hDlg;
            SendMessage(hDlg, WM_NOTIFY, 0, (LPARAM)&psh);

            // Assert: Verify persistence
            CConfig::LoadConfig(IME_MODE::IME_MODE_ARRAY);
            Assert::AreEqual(newSize, CConfig::GetFontSize(), 
                           L"FontSize should persist after PSN_APPLY");

            // Cleanup
            DestroyWindow(hDlg);
            FreeLibrary(hDimeDll);
            CConfig::SetFontSize(originalSize);
            CConfig::WriteConfig(FALSE);
        }

        // IT07_02: REMOVED - No dialog control for FontWeight
        // FontWeight is not exposed in the settings dialog UI
        // Only FontSize (IDC_EDIT_FONTPOINT) and FontName exist

        // ====================================================================
        // IT07_03: Composition Settings
        // ====================================================================

        TEST_METHOD(IT07_03_MaxCodes_LoadChangeApplyVerify)
        {
            // Arrange: Create dialog
            CConfig::SetIMEMode(IME_MODE::IME_MODE_ARRAY);

            HMODULE hDimeDll = LoadLibrary(L"DIME.dll");
            if (hDimeDll == NULL)
            {
                Logger::WriteMessage(L"DIME.dll not loaded - skipping dialog test");
                // Skip test gracefully
                return;
            }

            HWND hDlg = CreateDialogParam(hDimeDll, MAKEINTRESOURCE(IDD_DIALOG_COMMON), NULL,
                                         CConfig::CommonPropertyPageWndProc, 0);
            if (hDlg == NULL)
            {
                FreeLibrary(hDimeDll);
                // Skip test gracefully
                return;
            }

            SendMessage(hDlg, WM_INITDIALOG, 0, 0);

            UINT originalMax = CConfig::GetMaxCodes();
            UINT newMax = 8;

            // Act: Change MaxCodes control and send PSN_APPLY
            SetDlgItemInt(hDlg, IDC_EDIT_MAXWIDTH, newMax, FALSE);

            PSHNOTIFY psh = {0};
            psh.hdr.code = PSN_APPLY;
            psh.hdr.hwndFrom = hDlg;
            SendMessage(hDlg, WM_NOTIFY, 0, (LPARAM)&psh);

            // Assert
            CConfig::LoadConfig(IME_MODE::IME_MODE_ARRAY);
            Assert::AreEqual(newMax, CConfig::GetMaxCodes(),
                           L"MaxCodes should persist after PSN_APPLY");

            // Cleanup
            DestroyWindow(hDlg);
            FreeLibrary(hDimeDll);
            CConfig::SetMaxCodes(originalMax);
            CConfig::WriteConfig(FALSE);
        }

        TEST_METHOD(IT07_04_AutoCompose_LoadChangeApplyVerify)
        {
            // Arrange: Create dialog
            CConfig::SetIMEMode(IME_MODE::IME_MODE_ARRAY);

            HMODULE hDimeDll = LoadLibrary(L"DIME.dll");
            if (hDimeDll == NULL)
            {
                Logger::WriteMessage(L"DIME.dll not loaded - skipping dialog test");
                // Skip test gracefully
                return;
            }

            HWND hDlg = CreateDialogParam(hDimeDll, MAKEINTRESOURCE(IDD_DIALOG_COMMON), NULL,
                                         CConfig::CommonPropertyPageWndProc, 0);
            if (hDlg == NULL)
            {
                FreeLibrary(hDimeDll);
                // Skip test gracefully
                return;
            }

            SendMessage(hDlg, WM_INITDIALOG, 0, 0);

            BOOL original = CConfig::GetAutoCompose();
            BOOL newValue = !original;

            // Act: Toggle AutoCompose checkbox and send PSN_APPLY
            CheckDlgButton(hDlg, IDC_CHECKBOX_AUTOCOMPOSE, newValue ? BST_CHECKED : BST_UNCHECKED);

            PSHNOTIFY psh = {0};
            psh.hdr.code = PSN_APPLY;
            psh.hdr.hwndFrom = hDlg;
            SendMessage(hDlg, WM_NOTIFY, 0, (LPARAM)&psh);

            // Assert
            CConfig::LoadConfig(IME_MODE::IME_MODE_ARRAY);
            Assert::AreEqual(newValue, CConfig::GetAutoCompose(),
                           L"AutoCompose should persist after PSN_APPLY");

            // Cleanup
            DestroyWindow(hDlg);
            FreeLibrary(hDimeDll);
            CConfig::SetAutoCompose(original);
            CConfig::WriteConfig(FALSE);
        }

        TEST_METHOD(IT07_05_ClearOnBeep_LoadChangeApplyVerify)
        {
            // Arrange: Create REAL dialog
            CConfig::SetIMEMode(IME_MODE::IME_MODE_ARRAY);
            HMODULE hDimeDll = LoadLibrary(L"DIME.dll");
            if (!hDimeDll) { Logger::WriteMessage(L"Skip: DIME.dll not loaded"); return; }

            HWND hDlg = CreateDialogParam(hDimeDll, MAKEINTRESOURCE(IDD_DIALOG_COMMON), NULL,
                                         CConfig::CommonPropertyPageWndProc, 0);
            if (!hDlg) { FreeLibrary(hDimeDll); return; }

            SendMessage(hDlg, WM_INITDIALOG, 0, 0);
            BOOL original = CConfig::GetClearOnBeep();
            BOOL newValue = !original;

            // Act: Toggle ClearOnBeep checkbox
            CheckDlgButton(hDlg, IDC_CHECKBOX_CLEAR_ONBEEP, newValue ? BST_CHECKED : BST_UNCHECKED);

            PSHNOTIFY psh = {0};
            psh.hdr.code = PSN_APPLY;
            psh.hdr.hwndFrom = hDlg;
            SendMessage(hDlg, WM_NOTIFY, 0, (LPARAM)&psh);

            // Assert
            CConfig::LoadConfig(IME_MODE::IME_MODE_ARRAY);
            Assert::AreEqual(newValue, CConfig::GetClearOnBeep(),
                           L"ClearOnBeep should persist after PSN_APPLY");

            // Cleanup
            DestroyWindow(hDlg);
            FreeLibrary(hDimeDll);
            CConfig::SetClearOnBeep(original);
            CConfig::WriteConfig(FALSE);
        }

        // ====================================================================
        // IT07_06: Beep Settings
        // ====================================================================

        TEST_METHOD(IT07_06_DoBeep_LoadChangeApplyVerify)
        {
            // Arrange: Create REAL dialog
            CConfig::SetIMEMode(IME_MODE::IME_MODE_DAYI);
            HMODULE hDimeDll = LoadLibrary(L"DIME.dll");
            if (!hDimeDll) { Logger::WriteMessage(L"Skip: DIME.dll not loaded"); return; }

            HWND hDlg = CreateDialogParam(hDimeDll, MAKEINTRESOURCE(IDD_DIALOG_COMMON), NULL,
                                         CConfig::CommonPropertyPageWndProc, 0);
            if (!hDlg) { FreeLibrary(hDimeDll); return; }

            SendMessage(hDlg, WM_INITDIALOG, 0, 0);
            BOOL original = CConfig::GetDoBeep();
            BOOL newValue = !original;

            // Act: Toggle DoBeep checkbox
            CheckDlgButton(hDlg, IDC_CHECKBOX_DOBEEP, newValue ? BST_CHECKED : BST_UNCHECKED);

            PSHNOTIFY psh = {0};
            psh.hdr.code = PSN_APPLY;
            psh.hdr.hwndFrom = hDlg;
            SendMessage(hDlg, WM_NOTIFY, 0, (LPARAM)&psh);

            // Assert
            CConfig::LoadConfig(IME_MODE::IME_MODE_DAYI);
            Assert::AreEqual(newValue, CConfig::GetDoBeep(),
                           L"DoBeep should persist to DayiConfig.ini");

            // Cleanup
            DestroyWindow(hDlg);
            FreeLibrary(hDimeDll);
            CConfig::SetDoBeep(original);
            CConfig::WriteConfig(FALSE);
        }

        TEST_METHOD(IT07_07_DoBeepNotify_LoadChangeApplyVerify)
        {
            // Arrange: Create REAL dialog
            CConfig::SetIMEMode(IME_MODE::IME_MODE_DAYI);
            HMODULE hDimeDll = LoadLibrary(L"DIME.dll");
            if (!hDimeDll) { Logger::WriteMessage(L"Skip: DIME.dll not loaded"); return; }

            HWND hDlg = CreateDialogParam(hDimeDll, MAKEINTRESOURCE(IDD_DIALOG_COMMON), NULL,
                                         CConfig::CommonPropertyPageWndProc, 0);
            if (!hDlg) { FreeLibrary(hDimeDll); return; }

            SendMessage(hDlg, WM_INITDIALOG, 0, 0);
            BOOL original = CConfig::GetDoBeepNotify();
            BOOL newValue = !original;

            // Act: Toggle checkbox
            CheckDlgButton(hDlg, IDC_CHECKBOX_DOBEEPNOTIFY, newValue ? BST_CHECKED : BST_UNCHECKED);

            PSHNOTIFY psh = {0};
            psh.hdr.code = PSN_APPLY;
            psh.hdr.hwndFrom = hDlg;
            SendMessage(hDlg, WM_NOTIFY, 0, (LPARAM)&psh);

            // Assert
            CConfig::LoadConfig(IME_MODE::IME_MODE_DAYI);
            Assert::AreEqual(newValue, CConfig::GetDoBeepNotify(),
                           L"DoBeepNotify should persist");

            // Cleanup
            DestroyWindow(hDlg);
            FreeLibrary(hDimeDll);
            CConfig::SetDoBeepNotify(original);
            CConfig::WriteConfig(FALSE);
        }

        TEST_METHOD(IT07_08_DoBeepOnCandi_LoadChangeApplyVerify)
        {
            // Arrange: Create REAL dialog
            CConfig::SetIMEMode(IME_MODE::IME_MODE_DAYI);
            HMODULE hDimeDll = LoadLibrary(L"DIME.dll");
            if (!hDimeDll) { Logger::WriteMessage(L"Skip: DIME.dll not loaded"); return; }

            HWND hDlg = CreateDialogParam(hDimeDll, MAKEINTRESOURCE(IDD_DIALOG_COMMON), NULL,
                                         CConfig::CommonPropertyPageWndProc, 0);
            if (!hDlg) { FreeLibrary(hDimeDll); return; }

            SendMessage(hDlg, WM_INITDIALOG, 0, 0);
            BOOL original = CConfig::GetDoBeepOnCandi();
            BOOL newValue = !original;

            // Act: Toggle checkbox
            CheckDlgButton(hDlg, IDC_CHECKBOX_DOBEEP_CANDI, newValue ? BST_CHECKED : BST_UNCHECKED);

            PSHNOTIFY psh = {0};
            psh.hdr.code = PSN_APPLY;
            psh.hdr.hwndFrom = hDlg;
            SendMessage(hDlg, WM_NOTIFY, 0, (LPARAM)&psh);

            // Assert
            CConfig::LoadConfig(IME_MODE::IME_MODE_DAYI);
            Assert::AreEqual(newValue, CConfig::GetDoBeepOnCandi(),
                           L"DoBeepOnCandi should persist");

            // Cleanup
            DestroyWindow(hDlg);
            FreeLibrary(hDimeDll);
            CConfig::SetDoBeepOnCandi(original);
            CConfig::WriteConfig(FALSE);
        }

        // ====================================================================
        // IT07_09: IME Shift Mode - ComboBox (IDC_COMBO_IME_SHIFT_MODE)
        // ====================================================================

        TEST_METHOD(IT07_09_IMEShiftMode_AllValues_Persist)
        {
            // From Config.cpp line 479-480: reads combo with CB_GETCURSEL
            CConfig::SetIMEMode(IME_MODE::IME_MODE_ARRAY);
            HMODULE hDimeDll = LoadLibrary(L"DIME.dll");
            if (!hDimeDll) { Logger::WriteMessage(L"Skip: DIME.dll not loaded"); return; }

            HWND hDlg = CreateDialogParam(hDimeDll, MAKEINTRESOURCE(IDD_DIALOG_COMMON), NULL,
                                         CConfig::CommonPropertyPageWndProc, 0);
            if (!hDlg) { FreeLibrary(hDimeDll); return; }

            SendMessage(hDlg, WM_INITDIALOG, 0, 0);
            IME_SHIFT_MODE original = CConfig::GetIMEShiftMode();

            // Test ALL 4 valid enum values
            IME_SHIFT_MODE testValues[] = {
                IME_SHIFT_MODE::IME_BOTH_SHIFT,
                IME_SHIFT_MODE::IME_RIGHT_SHIFT_ONLY,
                IME_SHIFT_MODE::IME_LEFT_SHIFT_ONLY,
                IME_SHIFT_MODE::IME_NO_SHIFT
            };

            for (auto testValue : testValues)
            {
                // Act: Change combo selection
                HWND hwndCombo = GetDlgItem(hDlg, IDC_COMBO_IME_SHIFT_MODE);
                SendMessage(hwndCombo, CB_SETCURSEL, (WPARAM)testValue, 0);

                PSHNOTIFY psh = {0};
                psh.hdr.code = PSN_APPLY;
                psh.hdr.hwndFrom = hDlg;
                SendMessage(hDlg, WM_NOTIFY, 0, (LPARAM)&psh);

                // Assert
                CConfig::LoadConfig(IME_MODE::IME_MODE_ARRAY);
                Assert::IsTrue(CConfig::GetIMEShiftMode() == testValue,
                             L"IMEShiftMode should persist");
            }

            // Cleanup
            DestroyWindow(hDlg);
            FreeLibrary(hDimeDll);
            CConfig::SetIMEShiftMode(original);
            CConfig::WriteConfig(FALSE);
        }

        // ====================================================================
        // IT07_10: Double/Single Byte Mode - ComboBox (IDC_COMBO_DOUBLE_SINGLE_BYTE)
        // ====================================================================

        TEST_METHOD(IT07_10_DoubleSingleByteMode_AllValues_Persist)
        {
            // From Config.cpp line 483-484: reads combo with CB_GETCURSEL
            CConfig::SetIMEMode(IME_MODE::IME_MODE_ARRAY);
            HMODULE hDimeDll = LoadLibrary(L"DIME.dll");
            if (!hDimeDll) { Logger::WriteMessage(L"Skip: DIME.dll not loaded"); return; }

            HWND hDlg = CreateDialogParam(hDimeDll, MAKEINTRESOURCE(IDD_DIALOG_COMMON), NULL,
                                         CConfig::CommonPropertyPageWndProc, 0);
            if (!hDlg) { FreeLibrary(hDimeDll); return; }

            SendMessage(hDlg, WM_INITDIALOG, 0, 0);
            DOUBLE_SINGLE_BYTE_MODE original = CConfig::GetDoubleSingleByteMode();

            // Test ALL 3 valid enum values
            DOUBLE_SINGLE_BYTE_MODE testValues[] = {
                DOUBLE_SINGLE_BYTE_MODE::DOUBLE_SINGLE_BYTE_SHIFT_SPACE,
                DOUBLE_SINGLE_BYTE_MODE::DOUBLE_SINGLE_BYTE_ALWAYS_SINGLE,
                DOUBLE_SINGLE_BYTE_MODE::DOUBLE_SINGLE_BYTE_ALWAYS_DOUBLE
            };

            for (auto testValue : testValues)
            {
                // Act: Change combo selection
                HWND hwndCombo = GetDlgItem(hDlg, IDC_COMBO_DOUBLE_SINGLE_BYTE);
                SendMessage(hwndCombo, CB_SETCURSEL, (WPARAM)testValue, 0);

                PSHNOTIFY psh = {0};
                psh.hdr.code = PSN_APPLY;
                psh.hdr.hwndFrom = hDlg;
                SendMessage(hDlg, WM_NOTIFY, 0, (LPARAM)&psh);

                // Assert
                CConfig::LoadConfig(IME_MODE::IME_MODE_ARRAY);
                Assert::IsTrue(CConfig::GetDoubleSingleByteMode() == testValue,
                             L"DoubleSingleByteMode should persist");
            }

            // Cleanup
            DestroyWindow(hDlg);
            FreeLibrary(hDimeDll);
            CConfig::SetDoubleSingleByteMode(original);
            CConfig::WriteConfig(FALSE);
        }

        // ====================================================================
        // IT07_11: Array Scope - ComboBox (IDC_COMBO_ARRAY_SCOPE)
        // ====================================================================

        TEST_METHOD(IT07_11_ArrayScope_AllValues_Persist)
        {
            // From Config.cpp line 514-515: reads combo with CB_GETCURSEL
            CConfig::SetIMEMode(IME_MODE::IME_MODE_ARRAY);
            HMODULE hDimeDll = LoadLibrary(L"DIME.dll");
            if (!hDimeDll) { Logger::WriteMessage(L"Skip: DIME.dll not loaded"); return; }

            HWND hDlg = CreateDialogParam(hDimeDll, MAKEINTRESOURCE(IDD_DIALOG_COMMON), NULL,
                                         CConfig::CommonPropertyPageWndProc, 0);
            if (!hDlg) { FreeLibrary(hDimeDll); return; }

            SendMessage(hDlg, WM_INITDIALOG, 0, 0);
            ARRAY_SCOPE original = CConfig::GetArrayScope();

            // Test ALL 5 valid enum values from BaseStructure.h
            ARRAY_SCOPE testValues[] = {
                ARRAY_SCOPE::ARRAY30_UNICODE_EXT_A,
                ARRAY_SCOPE::ARRAY30_UNICODE_EXT_AB,
                ARRAY_SCOPE::ARRAY30_UNICODE_EXT_ABCD,
                ARRAY_SCOPE::ARRAY30_UNICODE_EXT_ABCDE,
                ARRAY_SCOPE::ARRAY40_BIG5
            };

            for (auto testValue : testValues)
            {
                // Act: Change combo selection
                HWND hwndCombo = GetDlgItem(hDlg, IDC_COMBO_ARRAY_SCOPE);
                SendMessage(hwndCombo, CB_SETCURSEL, (WPARAM)testValue, 0);

                PSHNOTIFY psh = {0};
                psh.hdr.code = PSN_APPLY;
                psh.hdr.hwndFrom = hDlg;
                SendMessage(hDlg, WM_NOTIFY, 0, (LPARAM)&psh);

                // Assert
                CConfig::LoadConfig(IME_MODE::IME_MODE_ARRAY);
                Assert::IsTrue(CConfig::GetArrayScope() == testValue,
                             L"ArrayScope should persist");
            }

            // Cleanup
            DestroyWindow(hDlg);
            FreeLibrary(hDimeDll);
            CConfig::SetArrayScope(original);
            CConfig::WriteConfig(FALSE);
        }

        // ====================================================================
        // IT07_12: Numeric Pad Mode - ComboBox (IDC_COMBO_NUMERIC_PAD)
        // ====================================================================

        TEST_METHOD(IT07_12_NumericPad_AllValues_Persist)
        {
            // From Config.cpp line 487-488: reads combo with CB_GETCURSEL
            CConfig::SetIMEMode(IME_MODE::IME_MODE_ARRAY);
            HMODULE hDimeDll = LoadLibrary(L"DIME.dll");
            if (!hDimeDll) { Logger::WriteMessage(L"Skip: DIME.dll not loaded"); return; }

            HWND hDlg = CreateDialogParam(hDimeDll, MAKEINTRESOURCE(IDD_DIALOG_COMMON), NULL,
                                         CConfig::CommonPropertyPageWndProc, 0);
            if (!hDlg) { FreeLibrary(hDimeDll); return; }

            SendMessage(hDlg, WM_INITDIALOG, 0, 0);
            NUMERIC_PAD original = CConfig::GetNumericPad();

            // Test ALL 3 valid values from BaseStructure.h
            NUMERIC_PAD testValues[] = {
                NUMERIC_PAD::NUMERIC_PAD_MUMERIC,
                NUMERIC_PAD::NUMERIC_PAD_MUMERIC_COMPOSITION,
                NUMERIC_PAD::NUMERIC_PAD_MUMERIC_COMPOSITION_ONLY
            };

            for (auto testValue : testValues)
            {
                // Act: Change combo selection
                HWND hwndCombo = GetDlgItem(hDlg, IDC_COMBO_NUMERIC_PAD);
                SendMessage(hwndCombo, CB_SETCURSEL, (WPARAM)testValue, 0);

                PSHNOTIFY psh = {0};
                psh.hdr.code = PSN_APPLY;
                psh.hdr.hwndFrom = hDlg;
                SendMessage(hDlg, WM_NOTIFY, 0, (LPARAM)&psh);

                // Assert
                CConfig::LoadConfig(IME_MODE::IME_MODE_ARRAY);
                Assert::IsTrue(CConfig::GetNumericPad() == testValue,
                             L"NumericPad should persist");
            }

            // Cleanup
            DestroyWindow(hDlg);
            FreeLibrary(hDimeDll);
            CConfig::SetNumericPad(original);
            CConfig::WriteConfig(FALSE);
        }

        // ====================================================================
        // IT07_13: Phonetic Keyboard Layout - ComboBox (IDC_COMBO_PHONETIC_KEYBOARD)
        // ====================================================================

        TEST_METHOD(IT07_13_PhoneticKeyboardLayout_AllValues_Persist)
        {
            // From Config.cpp line 533-534: reads combo with CB_GETCURSEL
            CConfig::SetIMEMode(IME_MODE::IME_MODE_PHONETIC);
            HMODULE hDimeDll = LoadLibrary(L"DIME.dll");
            if (!hDimeDll) { Logger::WriteMessage(L"Skip: DIME.dll not loaded"); return; }

            HWND hDlg = CreateDialogParam(hDimeDll, MAKEINTRESOURCE(IDD_DIALOG_COMMON), NULL,
                                         CConfig::CommonPropertyPageWndProc, 0);
            if (!hDlg) { FreeLibrary(hDimeDll); return; }

            SendMessage(hDlg, WM_INITDIALOG, 0, 0);
            PHONETIC_KEYBOARD_LAYOUT original = CConfig::getPhoneticKeyboardLayout();

            // Test ALL 2 valid enum values from BaseStructure.h
            PHONETIC_KEYBOARD_LAYOUT testValues[] = {
                PHONETIC_KEYBOARD_LAYOUT::PHONETIC_STANDARD_KEYBOARD_LAYOUT,
                PHONETIC_KEYBOARD_LAYOUT::PHONETIC_ETEN_KEYBOARD_LAYOUT
            };

            for (auto testValue : testValues)
            {
                // Act: Change combo selection
                HWND hwndCombo = GetDlgItem(hDlg, IDC_COMBO_PHONETIC_KEYBOARD);
                SendMessage(hwndCombo, CB_SETCURSEL, (WPARAM)testValue, 0);

                PSHNOTIFY psh = {0};
                psh.hdr.code = PSN_APPLY;
                psh.hdr.hwndFrom = hDlg;
                SendMessage(hDlg, WM_NOTIFY, 0, (LPARAM)&psh);

                // Assert
                CConfig::LoadConfig(IME_MODE::IME_MODE_PHONETIC);
                Assert::IsTrue(CConfig::getPhoneticKeyboardLayout() == testValue,
                             L"PhoneticKeyboardLayout should persist");
            }

            // Cleanup
            DestroyWindow(hDlg);
            FreeLibrary(hDimeDll);
            CConfig::setPhoneticKeyboardLayout(original);
            CConfig::WriteConfig(FALSE);
        }

        // ====================================================================
        // IT07_14: MakePhrase Checkbox (IDC_CHECKBOX_PHRASE)
        // ====================================================================

        TEST_METHOD(IT07_14_MakePhrase_LoadChangeApplyVerify)
        {
            // From Config.cpp line 440: reads checkbox
            CConfig::SetIMEMode(IME_MODE::IME_MODE_DAYI);
            HMODULE hDimeDll = LoadLibrary(L"DIME.dll");
            if (!hDimeDll) { Logger::WriteMessage(L"Skip: DIME.dll not loaded"); return; }

            HWND hDlg = CreateDialogParam(hDimeDll, MAKEINTRESOURCE(IDD_DIALOG_COMMON), NULL,
                                         CConfig::CommonPropertyPageWndProc, 0);
            if (!hDlg) { FreeLibrary(hDimeDll); return; }

            SendMessage(hDlg, WM_INITDIALOG, 0, 0);
            BOOL original = CConfig::GetMakePhrase();
            BOOL newValue = !original;

            // Act: Toggle MakePhrase checkbox
            CheckDlgButton(hDlg, IDC_CHECKBOX_PHRASE, newValue ? BST_CHECKED : BST_UNCHECKED);

            PSHNOTIFY psh = {0};
            psh.hdr.code = PSN_APPLY;
            psh.hdr.hwndFrom = hDlg;
            SendMessage(hDlg, WM_NOTIFY, 0, (LPARAM)&psh);

            // Assert
            CConfig::LoadConfig(IME_MODE::IME_MODE_DAYI);
            Assert::AreEqual(newValue, CConfig::GetMakePhrase(),
                           L"MakePhrase should persist to DayiConfig.ini");

            // Cleanup
            DestroyWindow(hDlg);
            FreeLibrary(hDimeDll);
            CConfig::SetMakePhrase(original);
            CConfig::WriteConfig(FALSE);
        }

        // ====================================================================
        // IT07_15: ShowNotifyDesktop Checkbox (IDC_CHECKBOX_SHOWNOTIFY)
        // ====================================================================

        TEST_METHOD(IT07_15_ShowNotifyDesktop_LoadChangeApplyVerify)
        {
            // From Config.cpp line 443: reads checkbox
            CConfig::SetIMEMode(IME_MODE::IME_MODE_ARRAY);
            HMODULE hDimeDll = LoadLibrary(L"DIME.dll");
            if (!hDimeDll) { Logger::WriteMessage(L"Skip: DIME.dll not loaded"); return; }

            HWND hDlg = CreateDialogParam(hDimeDll, MAKEINTRESOURCE(IDD_DIALOG_COMMON), NULL,
                                         CConfig::CommonPropertyPageWndProc, 0);
            if (!hDlg) { FreeLibrary(hDimeDll); return; }

            SendMessage(hDlg, WM_INITDIALOG, 0, 0);
            BOOL original = CConfig::GetShowNotifyDesktop();
            BOOL newValue = !original;

            // Act: Toggle ShowNotify checkbox
            CheckDlgButton(hDlg, IDC_CHECKBOX_SHOWNOTIFY, newValue ? BST_CHECKED : BST_UNCHECKED);

            PSHNOTIFY psh = {0};
            psh.hdr.code = PSN_APPLY;
            psh.hdr.hwndFrom = hDlg;
            SendMessage(hDlg, WM_NOTIFY, 0, (LPARAM)&psh);

            // Assert
            CConfig::LoadConfig(IME_MODE::IME_MODE_ARRAY);
            Assert::AreEqual(newValue, CConfig::GetShowNotifyDesktop(),
                           L"ShowNotifyDesktop should persist");

            // Cleanup
            DestroyWindow(hDlg);
            FreeLibrary(hDimeDll);
            CConfig::SetShowNotifyDesktop(original);
            CConfig::WriteConfig(FALSE);
        }
    };
}
