// SettingsDialogIntegrationTest.cpp - IT-07 Settings Dialog Integration Tests
// Tests: Create Dialog → Change Controls → Send PSN_APPLY → Verify Persistence

#include "pch.h"
#include "CppUnitTest.h"
#include "../Config.h"
#include "../Globals.h"
#include "../resource.h"
#include <shlwapi.h>
#include <Prsht.h>
#include <richedit.h>

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
            CConfig::WriteConfig(IME_MODE::IME_MODE_ARRAY, FALSE);
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
            CConfig::WriteConfig(IME_MODE::IME_MODE_ARRAY, FALSE);
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
            CConfig::WriteConfig(IME_MODE::IME_MODE_ARRAY, FALSE);
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
            CConfig::WriteConfig(IME_MODE::IME_MODE_ARRAY, FALSE);
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
            CConfig::WriteConfig(IME_MODE::IME_MODE_DAYI, FALSE);
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
            CConfig::WriteConfig(IME_MODE::IME_MODE_DAYI, FALSE);
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
            CConfig::WriteConfig(IME_MODE::IME_MODE_DAYI, FALSE);
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
            CConfig::WriteConfig(IME_MODE::IME_MODE_ARRAY, FALSE);
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
            CConfig::WriteConfig(IME_MODE::IME_MODE_ARRAY, FALSE);
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
            CConfig::WriteConfig(IME_MODE::IME_MODE_ARRAY, FALSE);
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
            CConfig::WriteConfig(IME_MODE::IME_MODE_ARRAY, FALSE);
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
            CConfig::WriteConfig(IME_MODE::IME_MODE_PHONETIC, FALSE);
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
            CConfig::WriteConfig(IME_MODE::IME_MODE_DAYI, FALSE);
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
            CConfig::WriteConfig(IME_MODE::IME_MODE_ARRAY, FALSE);
        }

        // ====================================================================
        // IT-CM-01: Color mode combo initialises with the currently saved mode
        // ====================================================================

        TEST_METHOD(IT_CM_01_ColorModeCombo_InitialisesWithSavedValue)
        {
            CConfig::SetIMEMode(IME_MODE::IME_MODE_ARRAY);
            IME_COLOR_MODE savedMode = CConfig::GetColorMode();

            // Arrange: persist DARK mode then reload so in-memory == DARK
            CConfig::SetColorMode(IME_COLOR_MODE::IME_COLOR_MODE_DARK);
            CConfig::WriteConfig(IME_MODE::IME_MODE_ARRAY, FALSE);
            CConfig::LoadConfig(IME_MODE::IME_MODE_ARRAY);

            HMODULE hDimeDll = LoadLibrary(L"DIME.dll");
            if (!hDimeDll) { Logger::WriteMessage(L"Skip: DIME.dll not available\n"); goto cleanup_mode_01; }

            {
                HWND hDlg = CreateDialogParam(hDimeDll, MAKEINTRESOURCE(IDD_DIALOG_COMMON), NULL,
                    CConfig::CommonPropertyPageWndProc, 0);
                if (!hDlg) { FreeLibrary(hDimeDll); goto cleanup_mode_01; }

                SendMessage(hDlg, WM_INITDIALOG, 0, 0);

                HWND hCombo = GetDlgItem(hDlg, IDC_COMBO_COLOR_MODE);
                Assert::IsNotNull(hCombo, L"IDC_COMBO_COLOR_MODE must exist in dialog");

                int sel = (int)SendMessage(hCombo, CB_GETCURSEL, 0, 0);
                Assert::AreEqual(
                    static_cast<int>(IME_COLOR_MODE::IME_COLOR_MODE_DARK), sel,
                    L"Combo selection must reflect the saved DARK mode (index 2)");

                DestroyWindow(hDlg);
                FreeLibrary(hDimeDll);
            }

        cleanup_mode_01:
            CConfig::SetColorMode(savedMode);
            CConfig::WriteConfig(IME_MODE::IME_MODE_ARRAY, FALSE);
        }

        // ====================================================================
        // IT-CM-02: Color boxes enabled only when CUSTOM mode is active
        // ====================================================================

        TEST_METHOD(IT_CM_02_ColorBoxes_DisabledUnlessCustomMode)
        {
            CConfig::SetIMEMode(IME_MODE::IME_MODE_ARRAY);
            IME_COLOR_MODE savedMode = CConfig::GetColorMode();
            CConfig::SetColorMode(IME_COLOR_MODE::IME_COLOR_MODE_LIGHT);

            HMODULE hDimeDll = LoadLibrary(L"DIME.dll");
            if (!hDimeDll) { Logger::WriteMessage(L"Skip: DIME.dll not available\n"); goto cleanup_mode_02; }

            {
                HWND hDlg = CreateDialogParam(hDimeDll, MAKEINTRESOURCE(IDD_DIALOG_COMMON), NULL,
                    CConfig::CommonPropertyPageWndProc, 0);
                if (!hDlg) { FreeLibrary(hDimeDll); goto cleanup_mode_02; }

                SendMessage(hDlg, WM_INITDIALOG, 0, 0);

                const int colorBoxIds[] = {
                    IDC_COL_BG, IDC_COL_FR, IDC_COL_NU,
                    IDC_COL_PHRASE, IDC_COL_SEBG, IDC_COL_SEFR
                };

                // In LIGHT mode all 6 colour boxes must be disabled
                for (int id : colorBoxIds)
                {
                    HWND hBox = GetDlgItem(hDlg, id);
                    if (hBox)
                        Assert::IsFalse(IsWindowEnabled(hBox) != FALSE,
                            L"Colour box must be disabled in LIGHT mode");
                }

                // Switch combo to CUSTOM and verify boxes become enabled
                HWND hCombo = GetDlgItem(hDlg, IDC_COMBO_COLOR_MODE);
                if (hCombo)
                {
                    SendMessage(hCombo, CB_SETCURSEL,
                        static_cast<WPARAM>(IME_COLOR_MODE::IME_COLOR_MODE_CUSTOM), 0);
                    SendMessage(hDlg, WM_COMMAND,
                        MAKEWPARAM(IDC_COMBO_COLOR_MODE, CBN_SELCHANGE), (LPARAM)hCombo);

                    for (int id : colorBoxIds)
                    {
                        HWND hBox = GetDlgItem(hDlg, id);
                        if (hBox)
                            Assert::IsTrue(IsWindowEnabled(hBox) != FALSE,
                                L"Colour box must be enabled in CUSTOM mode");
                    }
                }

                DestroyWindow(hDlg);
                FreeLibrary(hDimeDll);
            }

        cleanup_mode_02:
            CConfig::SetColorMode(savedMode);
        }

        // ====================================================================
        // IT-CM-03: PSN_APPLY persists the combo-selected colour mode to disk
        // ====================================================================

        TEST_METHOD(IT_CM_03_PSN_Apply_PersistsColorMode)
        {
            CConfig::SetIMEMode(IME_MODE::IME_MODE_ARRAY);
            IME_COLOR_MODE savedMode = CConfig::GetColorMode();

            // Start from SYSTEM on disk
            CConfig::SetColorMode(IME_COLOR_MODE::IME_COLOR_MODE_SYSTEM);
            CConfig::WriteConfig(IME_MODE::IME_MODE_ARRAY, FALSE);

            HMODULE hDimeDll = LoadLibrary(L"DIME.dll");
            if (!hDimeDll) { Logger::WriteMessage(L"Skip: DIME.dll not available\n"); goto cleanup_mode_03; }

            {
                HWND hDlg = CreateDialogParam(hDimeDll, MAKEINTRESOURCE(IDD_DIALOG_COMMON), NULL,
                    CConfig::CommonPropertyPageWndProc, 0);
                if (!hDlg) { FreeLibrary(hDimeDll); goto cleanup_mode_03; }

                SendMessage(hDlg, WM_INITDIALOG, 0, 0);

                // Change combo selection to DARK
                HWND hCombo = GetDlgItem(hDlg, IDC_COMBO_COLOR_MODE);
                if (hCombo)
                {
                    SendMessage(hCombo, CB_SETCURSEL,
                        static_cast<WPARAM>(IME_COLOR_MODE::IME_COLOR_MODE_DARK), 0);
                    SendMessage(hDlg, WM_COMMAND,
                        MAKEWPARAM(IDC_COMBO_COLOR_MODE, CBN_SELCHANGE), (LPARAM)hCombo);
                }

                // Simulate PSN_APPLY
                PSHNOTIFY psh = {0};
                psh.hdr.code     = PSN_APPLY;
                psh.hdr.hwndFrom = hDlg;
                SendMessage(hDlg, WM_NOTIFY, 0, (LPARAM)&psh);

                DestroyWindow(hDlg);
                FreeLibrary(hDimeDll);

                // Reload from disk and verify DARK was persisted
                CConfig::SetColorMode(IME_COLOR_MODE::IME_COLOR_MODE_SYSTEM); // perturb
                CConfig::LoadConfig(IME_MODE::IME_MODE_ARRAY);
                Assert::AreEqual(
                    static_cast<int>(IME_COLOR_MODE::IME_COLOR_MODE_DARK),
                    static_cast<int>(CConfig::GetColorMode()),
                    L"DARK mode must be persisted to disk after PSN_APPLY");
            }

        cleanup_mode_03:
            CConfig::SetColorMode(savedMode);
            CConfig::WriteConfig(IME_MODE::IME_MODE_ARRAY, FALSE);
        }

        // ====================================================================
        // IT-CM-04: Restore Default resets only the current palette; combo unchanged
        // ====================================================================

        TEST_METHOD(IT_CM_04_RestoreDefault_ResetsCurrentPaletteOnly)
        {
            CConfig::SetIMEMode(IME_MODE::IME_MODE_ARRAY);
            IME_COLOR_MODE savedMode = CConfig::GetColorMode();
            COLORREF savedItemColor = CConfig::GetItemColor();
            CConfig::SetColorMode(IME_COLOR_MODE::IME_COLOR_MODE_CUSTOM);
            // Dirty the custom palette so we can verify reset
            CConfig::SetItemColor(0x123456);

            HMODULE hDimeDll = LoadLibrary(L"DIME.dll");
            if (!hDimeDll) { Logger::WriteMessage(L"Skip: DIME.dll not available\n"); goto cleanup_mode_04; }

            {
                HWND hDlg = CreateDialogParam(hDimeDll, MAKEINTRESOURCE(IDD_DIALOG_COMMON), NULL,
                    CConfig::CommonPropertyPageWndProc, 0);
                if (!hDlg) { FreeLibrary(hDimeDll); goto cleanup_mode_04; }

                SendMessage(hDlg, WM_INITDIALOG, 0, 0);

                // Click Restore Default
                HWND hBtn = GetDlgItem(hDlg, IDC_BUTTON_RESTOREDEFAULT);
                if (hBtn)
                    SendMessage(hDlg, WM_COMMAND,
                        MAKEWPARAM(IDC_BUTTON_RESTOREDEFAULT, BN_CLICKED), (LPARAM)hBtn);

                // Combo must still show CUSTOM — not reset to SYSTEM
                HWND hCombo = GetDlgItem(hDlg, IDC_COMBO_COLOR_MODE);
                if (hCombo)
                {
                    IME_COLOR_MODE mode = CConfig::GetComboSelectedMode(hCombo);
                    Assert::AreEqual(
                        static_cast<int>(IME_COLOR_MODE::IME_COLOR_MODE_CUSTOM),
                        static_cast<int>(mode),
                        L"Combo must remain CUSTOM after Restore Default");
                }

                // Colour boxes must remain enabled (CUSTOM mode keeps them enabled)
                const int colorBoxIds[] = {
                    IDC_COL_BG, IDC_COL_FR, IDC_COL_NU,
                    IDC_COL_PHRASE, IDC_COL_SEBG, IDC_COL_SEFR
                };
                for (int id : colorBoxIds)
                {
                    HWND hBox = GetDlgItem(hDlg, id);
                    if (hBox)
                        Assert::IsTrue(IsWindowEnabled(hBox) != FALSE,
                            L"Colour boxes must remain enabled in CUSTOM mode after Restore Default");
                }

                // Custom item color must be back to factory default
                Assert::AreEqual(
                    static_cast<DWORD>(CANDWND_ITEM_COLOR),
                    static_cast<DWORD>(CConfig::GetItemColor()),
                    L"Custom item color must be reset to factory default");

                DestroyWindow(hDlg);
                FreeLibrary(hDimeDll);
            }

        cleanup_mode_04:
            CConfig::SetItemColor(savedItemColor);
            CConfig::SetColorMode(savedMode);
        }
    };

    // ====================================================================
    // Phase 5 Integration Tests: Custom Table Validation (IT-CV)
    // Covers: per-IME-mode validation, WM_THEMECHANGED, performance, context isolation
    // ====================================================================

    TEST_CLASS(CustomTableValidationIntegrationTest)
    {
    private:
        // Shared RichEdit host used by most tests.
        // Creates a popup parent window containing a RICHEDIT50W child
        // with control ID = IDC_EDIT_CUSTOM_TABLE so that
        // CConfig::ValidateCustomTableLines can be called against it directly.
        struct TestEditHost
        {
            HMODULE hRichEdit = nullptr;
            HWND    hParent   = nullptr;
            bool    ok        = false;

            TestEditHost()
            {
                hRichEdit = LoadLibraryW(L"MSFTEDIT.DLL");
                if (!hRichEdit) return;

                WNDCLASSW wc = {};
                wc.lpfnWndProc   = DefWindowProcW;
                wc.hInstance     = GetModuleHandleW(nullptr);
                wc.lpszClassName = L"DIME_ITCV_Parent";
                RegisterClassW(&wc);

                hParent = CreateWindowExW(0, L"DIME_ITCV_Parent", nullptr,
                    WS_POPUP, 0, 0, 600, 400,
                    nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
                if (!hParent) return;

                HWND hEdit = CreateWindowExW(0, L"RICHEDIT50W", nullptr,
                    WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL,
                    0, 0, 600, 400,
                    hParent, (HMENU)(INT_PTR)IDC_EDIT_CUSTOM_TABLE,
                    GetModuleHandleW(nullptr), nullptr);
                ok = (hEdit != nullptr);
            }

            void SetText(const wchar_t* text)
            {
                HWND hEdit = GetDlgItem(hParent, IDC_EDIT_CUSTOM_TABLE);
                if (hEdit) SetWindowTextW(hEdit, text);
            }

            ~TestEditHost()
            {
                if (hParent) { DestroyWindow(hParent); hParent = nullptr; }
                if (hRichEdit) { FreeLibrary(hRichEdit); hRichEdit = nullptr; }
            }
        };

    public:
        TEST_CLASS_INITIALIZE(ITCV_ClassSetup) { CoInitialize(NULL); }
        TEST_CLASS_CLEANUP(ITCV_ClassCleanup) { CoUninitialize(); }

        // ------------------------------------------------------------------
        // IT-CV-01: Array mode — valid 4-char key passes, 5-char key fails
        // ------------------------------------------------------------------
        TEST_METHOD(IT_CV_01_ArrayMode_MaxCodes4_BoundaryCheck)
        {
            TestEditHost host;
            if (!host.ok) { Logger::WriteMessage(L"Skip: MSFTEDIT.DLL not available"); return; }

            // Exactly 4 chars (maxCodes = 4): should pass
            host.SetText(L"abcd 測試詞");
            BOOL result4 = CConfig::ValidateCustomTableLines(
                host.hParent, IME_MODE::IME_MODE_ARRAY, nullptr, 4, false);
            Assert::IsTrue(result4 == TRUE, L"Array: 4-char key should pass");

            // 5 chars: Level 2 failure
            host.SetText(L"abcde 測試詞");
            BOOL result5 = CConfig::ValidateCustomTableLines(
                host.hParent, IME_MODE::IME_MODE_ARRAY, nullptr, 4, false);
            Assert::IsTrue(result5 == FALSE, L"Array: 5-char key should fail Level 2");
        }

        // ------------------------------------------------------------------
        // IT-CV-02: Dayi mode — valid ASCII-only key passes
        // ------------------------------------------------------------------
        TEST_METHOD(IT_CV_02_DayiMode_ASCIILetterKey_Passes)
        {
            TestEditHost host;
            if (!host.ok) { Logger::WriteMessage(L"Skip"); return; }

            host.SetText(L"ab 大義字根");

            BOOL result = CConfig::ValidateCustomTableLines(
                host.hParent, IME_MODE::IME_MODE_DAYI, nullptr, 4, false);

            Assert::IsTrue(result == TRUE, L"Dayi mode: ASCII-letter key should pass");
        }

        // ------------------------------------------------------------------
        // IT-CV-03: Phonetic mode — ASCII-letter key passes
        // ------------------------------------------------------------------
        TEST_METHOD(IT_CV_03_PhoneticMode_ValidKey_Passes)
        {
            TestEditHost host;
            if (!host.ok) { Logger::WriteMessage(L"Skip"); return; }

            host.SetText(L"su3 速度");

            BOOL result = CConfig::ValidateCustomTableLines(
                host.hParent, IME_MODE::IME_MODE_PHONETIC, nullptr, 10, false);

            Assert::IsTrue(result == TRUE, L"Phonetic mode: printable ASCII key should pass");
        }

        // ------------------------------------------------------------------
        // IT-CV-04: Phonetic mode — wildcard '*' in key fails (Level 3)
        // ------------------------------------------------------------------
        TEST_METHOD(IT_CV_04_PhoneticMode_WildcardInKey_Fails)
        {
            TestEditHost host;
            if (!host.ok) { Logger::WriteMessage(L"Skip"); return; }

            // '*' is excluded from valid phonetic key characters
            host.SetText(L"a*b 測試");

            BOOL result = CConfig::ValidateCustomTableLines(
                host.hParent, IME_MODE::IME_MODE_PHONETIC, nullptr, 10, false);

            Assert::IsTrue(result == FALSE,
                L"Phonetic mode: wildcard '*' in key should fail Level 3");
        }

        // ------------------------------------------------------------------
        // IT-CV-05: Phonetic mode — separator '?' in key fails (Level 3)
        // ------------------------------------------------------------------
        TEST_METHOD(IT_CV_05_PhoneticMode_QuestionMarkInKey_Fails)
        {
            TestEditHost host;
            if (!host.ok) { Logger::WriteMessage(L"Skip"); return; }

            host.SetText(L"a?b 測試");

            BOOL result = CConfig::ValidateCustomTableLines(
                host.hParent, IME_MODE::IME_MODE_PHONETIC, nullptr, 10, false);

            Assert::IsTrue(result == FALSE,
                L"Phonetic mode: '?' separator character in key should fail Level 3");
        }

        // ------------------------------------------------------------------
        // IT-CV-06: Generic mode — ASCII-letter key passes
        // ------------------------------------------------------------------
        TEST_METHOD(IT_CV_06_GenericMode_ASCIIKey_Passes)
        {
            TestEditHost host;
            if (!host.ok) { Logger::WriteMessage(L"Skip"); return; }

            host.SetText(L"abc 通用詞");

            BOOL result = CConfig::ValidateCustomTableLines(
                host.hParent, IME_MODE::IME_MODE_GENERIC, nullptr, 4, false);

            Assert::IsTrue(result == TRUE, L"Generic mode: ASCII-letter key should pass");
        }

        // ------------------------------------------------------------------
        // IT-CV-07: All 4 IME modes accept same valid line format
        // ------------------------------------------------------------------
        TEST_METHOD(IT_CV_07_AllIMEModes_ValidLine_AllPass)
        {
            TestEditHost host;
            if (!host.ok) { Logger::WriteMessage(L"Skip"); return; }

            IME_MODE modes[] = {
                IME_MODE::IME_MODE_ARRAY,
                IME_MODE::IME_MODE_DAYI,
                IME_MODE::IME_MODE_PHONETIC,
                IME_MODE::IME_MODE_GENERIC
            };

            const wchar_t* validLine = L"ab 詞彙";

            for (IME_MODE mode : modes)
            {
                host.SetText(validLine);
                BOOL result = CConfig::ValidateCustomTableLines(
                    host.hParent, mode, nullptr, 10, false);
                Assert::IsTrue(result == TRUE,
                    L"All IME modes should accept a valid ASCII-key line");
            }
        }

        // ------------------------------------------------------------------
        // IT-CV-08: All 4 IME modes reject line without separator
        // ------------------------------------------------------------------
        TEST_METHOD(IT_CV_08_AllIMEModes_NoSeparator_AllFail)
        {
            TestEditHost host;
            if (!host.ok) { Logger::WriteMessage(L"Skip"); return; }

            IME_MODE modes[] = {
                IME_MODE::IME_MODE_ARRAY,
                IME_MODE::IME_MODE_DAYI,
                IME_MODE::IME_MODE_PHONETIC,
                IME_MODE::IME_MODE_GENERIC
            };

            const wchar_t* badLine = L"ab詞彙"; // no space

            for (IME_MODE mode : modes)
            {
                host.SetText(badLine);
                BOOL result = CConfig::ValidateCustomTableLines(
                    host.hParent, mode, nullptr, 10, false);
                Assert::IsTrue(result == FALSE,
                    L"All IME modes should reject a line without separator");
            }
        }

        // ------------------------------------------------------------------
        // IT-CV-09: WM_THEMECHANGED on a window with DialogContext updates isDarkTheme
        // ------------------------------------------------------------------
        TEST_METHOD(IT_CV_09_WM_THEMECHANGED_UpdatesDarkThemeFlag)
        {
            // Use the Common page dialog (simpler setup than Dictionary page)
            // to test that WM_THEMECHANGED in CommonPropertyPageWndProc
            // re-reads IsSystemDarkTheme() and stores it in pCtx->isDarkTheme.
            HMODULE hDimeDll = LoadLibraryW(L"DIME.dll");
            if (!hDimeDll) { Logger::WriteMessage(L"Skip: DIME.dll not available"); return; }

            DialogContext* pCtx = new DialogContext();
            pCtx->imeMode  = IME_MODE::IME_MODE_ARRAY;
            pCtx->maxCodes = 4;

            // Deliberately set isDarkTheme to the *opposite* of the real theme
            // so we can detect the update.
            bool realTheme = CConfig::IsSystemDarkTheme();
            pCtx->isDarkTheme = !realTheme;

            // Inject pCtx via GWLP_USERDATA on a test window that uses
            // CommonPropertyPageWndProc as its dialog proc.
            HWND hDlg = CreateDialogParamW(hDimeDll,
                MAKEINTRESOURCE(IDD_DIALOG_COMMON), nullptr,
                CConfig::CommonPropertyPageWndProc, 0);

            if (!hDlg)
            {
                Logger::WriteMessage(L"Skip: could not create common dialog");
                delete pCtx;
                FreeLibrary(hDimeDll);
                return;
            }

            // Override GWLP_USERDATA with our test context
            pCtx->isDarkTheme = !realTheme;
            SetWindowLongPtr(hDlg, GWLP_USERDATA, (LONG_PTR)pCtx);

            // Send WM_THEMECHANGED — should call IsSystemDarkTheme() and update pCtx
            SendMessage(hDlg, WM_THEMECHANGED, 0, 0);

            // Verify isDarkTheme now matches the real system theme
            bool updated = pCtx->isDarkTheme;

            // Clean up before asserting (avoid leaking on failure)
            SetWindowLongPtr(hDlg, GWLP_USERDATA, 0);
            DestroyWindow(hDlg);
            FreeLibrary(hDimeDll);
            if (pCtx->hBrushBackground) { DeleteObject(pCtx->hBrushBackground); }
            if (pCtx->hBrushEditControl) { DeleteObject(pCtx->hBrushEditControl); }
            delete pCtx;

            Assert::AreEqual(realTheme, updated,
                L"WM_THEMECHANGED should update isDarkTheme to match system theme");
        }

        // ------------------------------------------------------------------
        // IT-CV-10: Context isolation — each DialogContext stores its own imeMode
        // ------------------------------------------------------------------
        TEST_METHOD(IT_CV_10_ContextIsolation_Each_DialogContext_StoresOwnMode)
        {
            DialogContext ctx1, ctx2;
            ctx1.imeMode = IME_MODE::IME_MODE_ARRAY;
            ctx2.imeMode = IME_MODE::IME_MODE_PHONETIC;

            // Change global _imeMode (simulates user switching the system IME)
            IME_MODE original = CConfig::GetIMEMode();
            CConfig::SetIMEMode(IME_MODE::IME_MODE_DAYI);

            // DialogContext values must not change when global mode changes
            Assert::IsTrue(ctx1.imeMode == IME_MODE::IME_MODE_ARRAY,
                L"ctx1.imeMode should remain ARRAY after global mode change");
            Assert::IsTrue(ctx2.imeMode == IME_MODE::IME_MODE_PHONETIC,
                L"ctx2.imeMode should remain PHONETIC after global mode change");

            // Restore
            CConfig::SetIMEMode(original);
        }

        // ------------------------------------------------------------------
        // IT-CV-11: Performance — 1000 valid lines validate within 500 ms
        // ------------------------------------------------------------------
        TEST_METHOD(IT_CV_11_Performance_1000ValidLines_CompletesWithin500ms)
        {
            TestEditHost host;
            if (!host.ok) { Logger::WriteMessage(L"Skip"); return; }

            // Build 1000 valid lines
            std::wstring content;
            content.reserve(1000 * 20);
            for (int i = 0; i < 1000; ++i)
            {
                content += L"ab 詞彙\r\n";
            }
            host.SetText(content.c_str());

            DWORD start = GetTickCount();
            BOOL result = CConfig::ValidateCustomTableLines(
                host.hParent, IME_MODE::IME_MODE_ARRAY, nullptr, 4, false);
            DWORD elapsed = GetTickCount() - start;

            Assert::IsTrue(result == TRUE, L"1000 valid lines should pass validation");
            Assert::IsTrue(elapsed < 500,
                L"Validation of 1000 lines should complete within 500ms");

            WCHAR msg[64];
            swprintf_s(msg, L"1000-line validation: %u ms", elapsed);
            Logger::WriteMessage(msg);
        }

        // ------------------------------------------------------------------
        // IT-CV-12: Performance — 100 lines with errors validates within 200 ms
        // ------------------------------------------------------------------
        TEST_METHOD(IT_CV_12_Performance_100ErrorLines_CompletesWithin200ms)
        {
            TestEditHost host;
            if (!host.ok) { Logger::WriteMessage(L"Skip"); return; }

            // Alternate valid/invalid lines
            std::wstring content;
            for (int i = 0; i < 50; ++i)
            {
                content += L"ab 詞彙\r\n";
                content += L"abcdef詞彙\r\n"; // no separator: Level 1 error
            }
            host.SetText(content.c_str());

            DWORD start = GetTickCount();
            BOOL result = CConfig::ValidateCustomTableLines(
                host.hParent, IME_MODE::IME_MODE_ARRAY, nullptr, 4, false);
            DWORD elapsed = GetTickCount() - start;

            Assert::IsTrue(result == FALSE, L"Content with errors should fail validation");
            Assert::IsTrue(elapsed < 200,
                L"Validation of 100 lines (with errors) should complete within 200ms");

            WCHAR msg[64];
            swprintf_s(msg, L"100-line error validation: %u ms", elapsed);
            Logger::WriteMessage(msg);
        }

        // ------------------------------------------------------------------
        // IT-CV-13: Dark theme context produces dark valid color on RichEdit
        // ------------------------------------------------------------------
        TEST_METHOD(IT_CV_13_DarkThemeContext_ValidLine_UsesWhiteColor)
        {
            TestEditHost host;
            if (!host.ok) { Logger::WriteMessage(L"Skip"); return; }

            host.SetText(L"ab 測試");

            // Inject a dark-theme DialogContext
            DialogContext ctx;
            ctx.isDarkTheme = true;
            SetWindowLongPtr(host.hParent, GWLP_USERDATA, (LONG_PTR)&ctx);

            BOOL result = CConfig::ValidateCustomTableLines(
                host.hParent, IME_MODE::IME_MODE_ARRAY, nullptr, 4, false);

            // After validation the text color for a valid line should be white (dark valid)
            HWND hEdit = GetDlgItem(host.hParent, IDC_EDIT_CUSTOM_TABLE);
            CHARFORMAT2W cf = {};
            cf.cbSize = sizeof(cf);
            SendMessageW(hEdit, EM_SETSEL, 0, 1);
            SendMessageW(hEdit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);

            SetWindowLongPtr(host.hParent, GWLP_USERDATA, 0);

            Assert::IsTrue(result == TRUE, L"Valid line should pass in dark theme");
            Assert::AreEqual((DWORD)CUSTOM_TABLE_DARK_VALID, (DWORD)cf.crTextColor,
                L"Dark theme: valid text should be painted white");
        }

        // ------------------------------------------------------------------
        // IT-CV-14: Light theme context produces black valid color on RichEdit
        // ------------------------------------------------------------------
        TEST_METHOD(IT_CV_14_LightThemeContext_ValidLine_UsesBlackColor)
        {
            TestEditHost host;
            if (!host.ok) { Logger::WriteMessage(L"Skip"); return; }

            host.SetText(L"ab 測試");

            // Light theme (isDarkTheme = false, the default)
            DialogContext ctx;
            ctx.isDarkTheme = false;
            SetWindowLongPtr(host.hParent, GWLP_USERDATA, (LONG_PTR)&ctx);

            BOOL result = CConfig::ValidateCustomTableLines(
                host.hParent, IME_MODE::IME_MODE_ARRAY, nullptr, 4, false);

            HWND hEdit = GetDlgItem(host.hParent, IDC_EDIT_CUSTOM_TABLE);
            CHARFORMAT2W cf = {};
            cf.cbSize = sizeof(cf);
            SendMessageW(hEdit, EM_SETSEL, 0, 1);
            SendMessageW(hEdit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);

            SetWindowLongPtr(host.hParent, GWLP_USERDATA, 0);

            Assert::IsTrue(result == TRUE, L"Valid line should pass in light theme");
            Assert::AreEqual((DWORD)CUSTOM_TABLE_LIGHT_VALID, (DWORD)cf.crTextColor,
                L"Light theme: valid text should be painted black");
        }

        // ====================================================================
        // IT-PT: Per-theme palette customization integration tests
        // ====================================================================

        // IT-PT-01: LIGHT mode – GetEffectiveDarkMode returns false
        TEST_METHOD(IT_PT_01_LightMode_EffectiveDarkModeFalse)
        {
            IME_COLOR_MODE saved = CConfig::GetColorMode();
            CConfig::SetColorMode(IME_COLOR_MODE::IME_COLOR_MODE_LIGHT);
            Assert::IsFalse(CConfig::GetEffectiveDarkMode(),
                L"LIGHT mode must report GetEffectiveDarkMode() == false");
            CConfig::SetColorMode(saved);
        }

        // IT-PT-02: DARK mode – GetEffectiveDarkMode returns true
        TEST_METHOD(IT_PT_02_DarkMode_EffectiveDarkModeTrue)
        {
            IME_COLOR_MODE saved = CConfig::GetColorMode();
            CConfig::SetColorMode(IME_COLOR_MODE::IME_COLOR_MODE_DARK);
            Assert::IsTrue(CConfig::GetEffectiveDarkMode(),
                L"DARK mode must report GetEffectiveDarkMode() == true");
            CConfig::SetColorMode(saved);
        }

        // IT-PT-03: CUSTOM mode – GetEffectiveDarkMode returns false
        TEST_METHOD(IT_PT_03_CustomMode_EffectiveDarkModeFalse)
        {
            IME_COLOR_MODE saved = CConfig::GetColorMode();
            CConfig::SetColorMode(IME_COLOR_MODE::IME_COLOR_MODE_CUSTOM);
            Assert::IsFalse(CConfig::GetEffectiveDarkMode(),
                L"CUSTOM mode must report GetEffectiveDarkMode() == false");
            CConfig::SetColorMode(saved);
        }

        // IT-PT-04: Light palette getter/setter round-trip in memory
        TEST_METHOD(IT_PT_04_LightPalette_GetSet_RoundTrip)
        {
            COLORREF savedItem = CConfig::GetLightItemColor();
            COLORREF savedPhrase = CConfig::GetLightPhraseColor();
            COLORREF savedBG = CConfig::GetLightItemBGColor();

            CConfig::SetLightItemColor(0xABCDEF);
            CConfig::SetLightPhraseColor(0x123456);
            CConfig::SetLightItemBGColor(0xFEDCBA);

            Assert::AreEqual(0xABCDEFul, (unsigned long)CConfig::GetLightItemColor(),
                L"LightItemColor get/set in memory");
            Assert::AreEqual(0x123456ul, (unsigned long)CConfig::GetLightPhraseColor(),
                L"LightPhraseColor get/set in memory");
            Assert::AreEqual(0xFEDCBAul, (unsigned long)CConfig::GetLightItemBGColor(),
                L"LightItemBGColor get/set in memory");

            CConfig::SetLightItemColor(savedItem);
            CConfig::SetLightPhraseColor(savedPhrase);
            CConfig::SetLightItemBGColor(savedBG);
        }

        // IT-PT-05: Dark palette getter/setter round-trip in memory
        TEST_METHOD(IT_PT_05_DarkPalette_GetSet_RoundTrip)
        {
            COLORREF savedItem = CConfig::GetDarkItemColor();
            COLORREF savedPhrase = CConfig::GetDarkPhraseColor();
            COLORREF savedBG = CConfig::GetDarkItemBGColor();

            CConfig::SetDarkItemColor(0x112233);
            CConfig::SetDarkPhraseColor(0x445566);
            CConfig::SetDarkItemBGColor(0x778899);

            Assert::AreEqual(0x112233ul, (unsigned long)CConfig::GetDarkItemColor(),
                L"DarkItemColor get/set in memory");
            Assert::AreEqual(0x445566ul, (unsigned long)CConfig::GetDarkPhraseColor(),
                L"DarkPhraseColor get/set in memory");
            Assert::AreEqual(0x778899ul, (unsigned long)CConfig::GetDarkItemBGColor(),
                L"DarkItemBGColor get/set in memory");

            CConfig::SetDarkItemColor(savedItem);
            CConfig::SetDarkPhraseColor(savedPhrase);
            CConfig::SetDarkItemBGColor(savedBG);
        }

        // IT-PT-06: Light palette factory defaults match compile-time constants
        TEST_METHOD(IT_PT_06_LightPalette_StaticDefaults_MatchConstants)
        {
            // Reset to factory defaults
            CConfig::SetLightItemColor(CANDWND_ITEM_COLOR);
            CConfig::SetLightPhraseColor(CANDWND_PHRASE_COLOR);
            CConfig::SetLightNumberColor(CANDWND_NUM_COLOR);
            CConfig::SetLightSelectedColor(CANDWND_SELECTED_ITEM_COLOR);
            CConfig::SetLightSelectedBGColor(CANDWND_SELECTED_BK_COLOR);

            Assert::AreEqual((unsigned long)CANDWND_ITEM_COLOR,
                (unsigned long)CConfig::GetLightItemColor(),
                L"Light ItemColor factory default");
            Assert::AreEqual((unsigned long)CANDWND_PHRASE_COLOR,
                (unsigned long)CConfig::GetLightPhraseColor(),
                L"Light PhraseColor factory default");
            Assert::AreEqual((unsigned long)CANDWND_NUM_COLOR,
                (unsigned long)CConfig::GetLightNumberColor(),
                L"Light NumberColor factory default");
            Assert::AreEqual((unsigned long)CANDWND_SELECTED_ITEM_COLOR,
                (unsigned long)CConfig::GetLightSelectedColor(),
                L"Light SelectedColor factory default");
            Assert::AreEqual((unsigned long)CANDWND_SELECTED_BK_COLOR,
                (unsigned long)CConfig::GetLightSelectedBGColor(),
                L"Light SelectedBGColor factory default");
        }

        // IT-PT-07: Dark palette factory defaults match compile-time constants
        TEST_METHOD(IT_PT_07_DarkPalette_StaticDefaults_MatchConstants)
        {
            CConfig::SetDarkItemColor(CANDWND_DARK_ITEM_COLOR);
            CConfig::SetDarkPhraseColor(CANDWND_DARK_PHRASE_COLOR);
            CConfig::SetDarkNumberColor(CANDWND_DARK_NUM_COLOR);
            CConfig::SetDarkItemBGColor(CANDWND_DARK_ITEM_BG_COLOR);
            CConfig::SetDarkSelectedColor(CANDWND_DARK_SELECTED_COLOR);
            CConfig::SetDarkSelectedBGColor(CANDWND_DARK_SELECTED_BG_COLOR);

            Assert::AreEqual((unsigned long)CANDWND_DARK_ITEM_COLOR,
                (unsigned long)CConfig::GetDarkItemColor(),
                L"Dark ItemColor factory default");
            Assert::AreEqual((unsigned long)CANDWND_DARK_PHRASE_COLOR,
                (unsigned long)CConfig::GetDarkPhraseColor(),
                L"Dark PhraseColor factory default");
            Assert::AreEqual((unsigned long)CANDWND_DARK_NUM_COLOR,
                (unsigned long)CConfig::GetDarkNumberColor(),
                L"Dark NumberColor factory default");
            Assert::AreEqual((unsigned long)CANDWND_DARK_ITEM_BG_COLOR,
                (unsigned long)CConfig::GetDarkItemBGColor(),
                L"Dark ItemBGColor factory default");
            Assert::AreEqual((unsigned long)CANDWND_DARK_SELECTED_COLOR,
                (unsigned long)CConfig::GetDarkSelectedColor(),
                L"Dark SelectedColor factory default");
            Assert::AreEqual((unsigned long)CANDWND_DARK_SELECTED_BG_COLOR,
                (unsigned long)CConfig::GetDarkSelectedBGColor(),
                L"Dark SelectedBGColor factory default");
        }

        // IT-PT-08: Dark palette has higher luminance than light palette for
        //           item colors (dark mode text must be lighter than black)
        TEST_METHOD(IT_PT_08_DarkPalette_ItemColor_LighterThanLight)
        {
            COLORREF lightItem = CANDWND_ITEM_COLOR;        // RGB(0,0,0) = black
            COLORREF darkItem  = CANDWND_DARK_ITEM_COLOR;   // RGB(220,220,220) = near-white

            auto luminance = [](COLORREF c) -> int {
                return GetRValue(c) + GetGValue(c) + GetBValue(c);
            };

            int lightLum = luminance(lightItem);
            int darkLum  = luminance(darkItem);

            Assert::IsTrue(darkLum > lightLum,
                L"Dark item color must have higher total luminance than light item color (dark text must be lighter for visibility)");
        }

    };
}
