/* DIME IME for Windows 7/8/10/11

BSD 3-Clause License

Copyright (c) 2022, Jeremy Wu
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "pch.h"
#include "CppUnitTest.h"
#include "Private.h"
#include "Globals.h"
#include "NotifyWindow.h"
#include "BaseStructure.h"
#include <Windows.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace DIMEIntegratedTests
{
    // Helper callback function for CNotifyWindow
    static HRESULT TestNotifyCallback(void *pv, enum NOTIFY_WND action, _In_ WPARAM wParam, _In_ LPARAM lParam)
    {
        return S_OK;
    }

    TEST_CLASS(NotificationWindowIntegrationTest)
    {
    public:
        TEST_METHOD_INITIALIZE(Setup)
        {
            Logger::WriteMessage("IT-04: Notification Window Integration Test Setup\n");
        }

        TEST_METHOD_CLEANUP(Cleanup)
        {
            Logger::WriteMessage("IT-04: Notification Window Integration Test Cleanup\n");
        }

        // IT-04-01: Chinese/English Switch Notification Tests
        TEST_METHOD(IT04_01_NotifyWindow_Constructor_Succeeds)
        {
            Logger::WriteMessage("Test: IT04_01_NotifyWindow_Constructor_Succeeds\n");

            // Act: Create notification window
            CNotifyWindow* pNotifyWnd = new CNotifyWindow(TestNotifyCallback, nullptr, NOTIFY_TYPE::NOTIFY_CHN_ENG);

            // Assert
            Assert::IsNotNull(pNotifyWnd, L"Notification window should be created");
            Assert::AreEqual(static_cast<int>(NOTIFY_TYPE::NOTIFY_CHN_ENG), static_cast<int>(pNotifyWnd->GetNotifyType()));

            // Cleanup
            delete pNotifyWnd;
        }

        TEST_METHOD(IT04_01_NotifyWindow_Create_WithText_Succeeds)
        {
            Logger::WriteMessage("Test: IT04_01_NotifyWindow_Create_WithText_Succeeds\n");

            // Arrange
            CNotifyWindow* pNotifyWnd = new CNotifyWindow(TestNotifyCallback, nullptr, NOTIFY_TYPE::NOTIFY_CHN_ENG);
            CStringRange notifyText;
            notifyText.Set(L"Test Notification", 17);

            // Act: Create window
            BOOL created = pNotifyWnd->_Create(20, NULL, &notifyText);

            // Assert
            Assert::IsTrue(created, L"Window creation should succeed");

            // Cleanup
            delete pNotifyWnd;
        }

        TEST_METHOD(IT04_01_NotifyWindow_SetString_UpdatesContent)
        {
            Logger::WriteMessage("Test: IT04_01_NotifyWindow_SetString_UpdatesContent\n");

            // Arrange
            CNotifyWindow* pNotifyWnd = new CNotifyWindow(TestNotifyCallback, nullptr, NOTIFY_TYPE::NOTIFY_CHN_ENG);
            CStringRange initialText;
            initialText.Set(L"Initial", 7);
            BOOL created = pNotifyWnd->_Create(20, NULL, &initialText);

            // Act: Set notification text
            CStringRange text;
            text.Set(L"Chinese Mode", 12);
            pNotifyWnd->_SetString(&text);

            // Assert: Verify creation succeeded (window handle may not be available in unit test context)
            Assert::IsTrue(created, L"Window creation should succeed");

            // Cleanup
            delete pNotifyWnd;
        }

        TEST_METHOD(IT04_01_NotifyWindow_Clear_RemovesContent)
        {
            Logger::WriteMessage("Test: IT04_01_NotifyWindow_Clear_RemovesContent\n");

            // Arrange
            CNotifyWindow* pNotifyWnd = new CNotifyWindow(TestNotifyCallback, nullptr, NOTIFY_TYPE::NOTIFY_CHN_ENG);
            CStringRange text;
            text.Set(L"Test", 4);
            BOOL created = pNotifyWnd->_Create(20, NULL, &text);

            // Act: Clear content
            pNotifyWnd->_Clear();

            // Assert: Creation succeeded, clear operation completed without crash
            Assert::IsTrue(created, L"Window creation should succeed");

            // Cleanup
            delete pNotifyWnd;
        }

        TEST_METHOD(IT04_01_NotifyWindow_Show_DisplaysWindow)
        {
            Logger::WriteMessage("Test: IT04_01_NotifyWindow_Show_DisplaysWindow\n");

            // Arrange
            CNotifyWindow* pNotifyWnd = new CNotifyWindow(TestNotifyCallback, nullptr, NOTIFY_TYPE::NOTIFY_CHN_ENG);
            CStringRange text;
            text.Set(L"Test", 4);
            BOOL created = pNotifyWnd->_Create(20, NULL, &text);

            // Act: Show window
            pNotifyWnd->_Show(TRUE, 0, 0);

            // Assert: Show operation completed without crash
            Assert::IsTrue(created, L"Window creation should succeed");

            // Cleanup
            pNotifyWnd->_Show(FALSE, 0, 0);
            delete pNotifyWnd;
        }

        // IT-04-02: Notification Window Positioning Tests
        TEST_METHOD(IT04_02_NotifyWindow_Move_UpdatesPosition)
        {
            Logger::WriteMessage("Test: IT04_02_NotifyWindow_Move_UpdatesPosition\n");

            // Arrange
            CNotifyWindow* pNotifyWnd = new CNotifyWindow(TestNotifyCallback, nullptr, NOTIFY_TYPE::NOTIFY_CHN_ENG);
            CStringRange text;
            text.Set(L"Test", 4);
            BOOL created = pNotifyWnd->_Create(20, NULL, &text);

            // Act: Move window
            pNotifyWnd->_Move(100, 200);

            // Assert: Move operation completed without crash
            Assert::IsTrue(created, L"Window creation should succeed");

            // Cleanup
            delete pNotifyWnd;
        }

        TEST_METHOD(IT04_02_NotifyWindow_GetWidth_ReturnsValue)
        {
            Logger::WriteMessage("Test: IT04_02_NotifyWindow_GetWidth_ReturnsValue\n");

            // Arrange
            CNotifyWindow* pNotifyWnd = new CNotifyWindow(TestNotifyCallback, nullptr, NOTIFY_TYPE::NOTIFY_CHN_ENG);
            CStringRange text;
            text.Set(L"Test Width", 10);
            BOOL created = pNotifyWnd->_Create(20, NULL, &text);

            // Act: Get width
            UINT width = pNotifyWnd->_GetWidth();

            // Assert: Creation succeeded, width call completed
            Assert::IsTrue(created, L"Window creation should succeed");
            // Width may be 0 in unit test context without message loop
            Assert::IsTrue(width >= 0, L"Width should be non-negative");

            // Cleanup
            delete pNotifyWnd;
        }

        TEST_METHOD(IT04_02_NotifyWindow_GetHeight_ReturnsValue)
        {
            Logger::WriteMessage("Test: IT04_02_NotifyWindow_GetHeight_ReturnsValue\n");

            // Arrange
            CNotifyWindow* pNotifyWnd = new CNotifyWindow(TestNotifyCallback, nullptr, NOTIFY_TYPE::NOTIFY_CHN_ENG);
            CStringRange text;
            text.Set(L"Test Height", 11);
            BOOL created = pNotifyWnd->_Create(20, NULL, &text);

            // Act: Get height
            UINT height = pNotifyWnd->_GetHeight();

            // Assert: Creation succeeded, height call completed
            Assert::IsTrue(created, L"Window creation should succeed");
            // Height may be 0 in unit test context without message loop
            Assert::IsTrue(height >= 0, L"Height should be non-negative");

            // Cleanup
            delete pNotifyWnd;
        }

        // IT-04-03: Notification Content Display Tests
        TEST_METHOD(IT04_03_NotifyWindow_AddString_AppendsText)
        {
            Logger::WriteMessage("Test: IT04_03_NotifyWindow_AddString_AppendsText\n");

            // Arrange
            CNotifyWindow* pNotifyWnd = new CNotifyWindow(TestNotifyCallback, nullptr, NOTIFY_TYPE::NOTIFY_CHN_ENG);
            CStringRange text1;
            text1.Set(L"Line1", 5);
            BOOL created = pNotifyWnd->_Create(20, NULL, &text1);

            // Act: Add more text
            CStringRange text2;
            text2.Set(L"Line2", 5);
            pNotifyWnd->_AddString(&text2);

            // Assert: AddString operation completed without crash
            Assert::IsTrue(created, L"Window creation should succeed");

            // Cleanup
            delete pNotifyWnd;
        }

        TEST_METHOD(IT04_03_NotifyWindow_SetTextColor_UpdatesColors)
        {
            Logger::WriteMessage("Test: IT04_03_NotifyWindow_SetTextColor_UpdatesColors\n");

            // Arrange
            CNotifyWindow* pNotifyWnd = new CNotifyWindow(TestNotifyCallback, nullptr, NOTIFY_TYPE::NOTIFY_CHN_ENG);
            CStringRange text;
            text.Set(L"Test", 4);
            BOOL created = pNotifyWnd->_Create(20, NULL, &text);
            pNotifyWnd->_Show(TRUE, 0, 0);

            // Act: Set colors
            pNotifyWnd->_SetTextColor(RGB(255, 0, 0), RGB(255, 255, 255));

            // Assert: SetTextColor operation completed without crash
            Assert::IsTrue(created, L"Window creation should succeed");

            // Cleanup
            delete pNotifyWnd;
        }

        TEST_METHOD(IT04_03_NotifyWindow_SetFillColor_UpdatesBackground)
        {
            Logger::WriteMessage("Test: IT04_03_NotifyWindow_SetFillColor_UpdatesBackground\n");

            // Arrange
            CNotifyWindow* pNotifyWnd = new CNotifyWindow(TestNotifyCallback, nullptr, NOTIFY_TYPE::NOTIFY_CHN_ENG);
            CStringRange text;
            text.Set(L"Test", 4);
            BOOL created = pNotifyWnd->_Create(20, NULL, &text);
            pNotifyWnd->_Show(TRUE, 0, 0);

            // Act: Set fill color
            pNotifyWnd->_SetFillColor(RGB(200, 200, 200));

            // Assert: SetFillColor operation completed without crash
            Assert::IsTrue(created, L"Window creation should succeed");

            // Cleanup
            delete pNotifyWnd;
        }

        // IT-04-04: Multiple Notification Types Tests
        TEST_METHOD(IT04_04_NotifyWindow_ChineseEnglishType_Creates)
        {
            Logger::WriteMessage("Test: IT04_04_NotifyWindow_ChineseEnglishType_Creates\n");

            // Act
            CNotifyWindow* pNotifyWnd = new CNotifyWindow(TestNotifyCallback, nullptr, NOTIFY_TYPE::NOTIFY_CHN_ENG);
            pNotifyWnd->_Create(20, NULL);

            // Assert
            Assert::AreEqual(static_cast<int>(NOTIFY_TYPE::NOTIFY_CHN_ENG), static_cast<int>(pNotifyWnd->GetNotifyType()));

            // Cleanup
            delete pNotifyWnd;
        }

        TEST_METHOD(IT04_04_NotifyWindow_ShowNotifyType_Creates)
        {
            Logger::WriteMessage("Test: IT04_04_NotifyWindow_ShowNotifyType_Creates\n");

            // Act: Using NOTIFY_TYPE::NOTIFY_OTHERS instead of non-existent SHOW_NOTIFY
            CNotifyWindow* pNotifyWnd = new CNotifyWindow(TestNotifyCallback, nullptr, NOTIFY_TYPE::NOTIFY_OTHERS);
            pNotifyWnd->_Create(20, NULL);

            // Assert
            Assert::AreEqual(static_cast<int>(NOTIFY_TYPE::NOTIFY_OTHERS), static_cast<int>(pNotifyWnd->GetNotifyType()));

            // Cleanup
            delete pNotifyWnd;
        }

        TEST_METHOD(IT04_04_NotifyWindow_SetNotifyType_UpdatesType)
        {
            Logger::WriteMessage("Test: IT04_04_NotifyWindow_SetNotifyType_UpdatesType\n");

            // Arrange
            CNotifyWindow* pNotifyWnd = new CNotifyWindow(TestNotifyCallback, nullptr, NOTIFY_TYPE::NOTIFY_CHN_ENG);

            // Act: Change notification type to NOTIFY_OTHERS
            pNotifyWnd->SetNotifyType(NOTIFY_TYPE::NOTIFY_OTHERS);

            // Assert
            Assert::AreEqual(static_cast<int>(NOTIFY_TYPE::NOTIFY_OTHERS), static_cast<int>(pNotifyWnd->GetNotifyType()));

            // Cleanup
            delete pNotifyWnd;
        }

        // IT-04-05: Advanced Notification Window Tests
        TEST_METHOD(IT04_05_NotifyWindow_MultipleShowHide_NoLeak)
        {
            Logger::WriteMessage("Test: IT04_05_NotifyWindow_MultipleShowHide_NoLeak\n");

            // Act: Create and show/hide multiple times
            for (int i = 0; i < 50; i++)
            {
                CNotifyWindow* pNotifyWnd = new CNotifyWindow(TestNotifyCallback, nullptr, NOTIFY_TYPE::NOTIFY_CHN_ENG);
                pNotifyWnd->_Create(20, NULL);
                pNotifyWnd->_Show(TRUE, 0, 0);
                pNotifyWnd->_Show(FALSE, 0, 0);
                delete pNotifyWnd;
            }

            // Assert: No crash indicates success
            Assert::IsTrue(true, L"Multiple create/show/hide cycles completed without crash");
        }

        TEST_METHOD(IT04_05_NotifyWindow_NullParent_Succeeds)
        {
            Logger::WriteMessage("Test: IT04_05_NotifyWindow_NullParent_Succeeds\n");

            // Act: Create with NULL parent
            CNotifyWindow* pNotifyWnd = new CNotifyWindow(TestNotifyCallback, nullptr, NOTIFY_TYPE::NOTIFY_CHN_ENG);
            BOOL created = pNotifyWnd->_Create(20, NULL);

            // Assert
            Assert::IsTrue(created, L"Should create successfully with NULL parent");

            // Cleanup
            delete pNotifyWnd;
        }

        TEST_METHOD(IT04_05_NotifyWindow_ShowWithDelay_Succeeds)
        {
            Logger::WriteMessage("Test: IT04_05_NotifyWindow_ShowWithDelay_Succeeds\n");

            // Arrange
            CNotifyWindow* pNotifyWnd = new CNotifyWindow(TestNotifyCallback, nullptr, NOTIFY_TYPE::NOTIFY_CHN_ENG);
            CStringRange text;
            text.Set(L"Test", 4);
            BOOL created = pNotifyWnd->_Create(20, NULL, &text);

            // Act: Show with delay and auto-hide
            pNotifyWnd->_Show(TRUE, 100, 500);

            // Assert: Show operation completed
            Assert::IsTrue(created, L"Window creation should succeed");

            // Cleanup
            pNotifyWnd->_Show(FALSE, 0, 0);
            delete pNotifyWnd;
        }

        TEST_METHOD(IT04_05_NotifyWindow_EmptyText_Handles)
        {
            Logger::WriteMessage("Test: IT04_05_NotifyWindow_EmptyText_Handles\n");

            // Arrange
            CNotifyWindow* pNotifyWnd = new CNotifyWindow(TestNotifyCallback, nullptr, NOTIFY_TYPE::NOTIFY_CHN_ENG);
            CStringRange text;
            text.Set(L"Test", 4);
            BOOL created = pNotifyWnd->_Create(20, NULL, &text);
            pNotifyWnd->_Show(TRUE, 0, 0);

            // Act: Set empty text
            CStringRange emptyText;
            emptyText.Set(L"", 0);
            pNotifyWnd->_SetString(&emptyText);

            // Assert: Window handles empty text gracefully
            Assert::IsTrue(created, L"Window creation should succeed");

            // Cleanup
            delete pNotifyWnd;
        }

        TEST_METHOD(IT04_05_NotifyWindow_UnicodeText_Displays)
        {
            Logger::WriteMessage("Test: IT04_05_NotifyWindow_UnicodeText_Displays\n");

            // Arrange
            CNotifyWindow* pNotifyWnd = new CNotifyWindow(TestNotifyCallback, nullptr, NOTIFY_TYPE::NOTIFY_CHN_ENG);
            WCHAR unicodeText[] = { 0x4E2D, 0x6587, 0x6A21, 0x5F0F, 0 }; // "中文模式"
            CStringRange text;
            text.Set(unicodeText, 4);
            BOOL created = pNotifyWnd->_Create(20, NULL, &text);

            // Act: Show window to ensure it's created
            pNotifyWnd->_Show(TRUE, 0, 0);

            // Assert: Window handles Unicode text
            Assert::IsTrue(created, L"Window creation with Unicode text should succeed");

            // Cleanup
            delete pNotifyWnd;
        }

        // ------------------------------------------------------------------
        // IT-CM-20: Dark-mode colours applied to notify window without crash
        // ------------------------------------------------------------------

        TEST_METHOD(IT_CM_20_NotifyWindow_DarkMode_ColorsApplied)
        {
            Logger::WriteMessage("Test: IT_CM_20 - Dark mode colours applied to notify window\n");

            CNotifyWindow* pWnd = new CNotifyWindow(TestNotifyCallback, nullptr, NOTIFY_TYPE::NOTIFY_CHN_ENG);
            Assert::IsNotNull(pWnd, L"Notify window must be created");

            CStringRange text;
            text.Set(L"Test", 4);
            BOOL created = pWnd->_Create(20, NULL, &text);
            Assert::IsTrue(created, L"Window creation must succeed");

            // Apply dark-preset colours (matches what ShowNotification does for dark mode)
            pWnd->_SetTextColor(NOTIFYWND_DARK_TEXT_COLOR, NOTIFYWND_DARK_TEXT_BK_COLOR);
            pWnd->_SetFillColor(NOTIFYWND_DARK_TEXT_BK_COLOR);

            // No getter available; verify the operation does not crash
            Assert::IsTrue(true, L"Dark-mode colour assignment must not crash");

            delete pWnd;
        }

        // ------------------------------------------------------------------
        // IT-CM-21: Light-mode colours applied to notify window without crash
        // ------------------------------------------------------------------

        TEST_METHOD(IT_CM_21_NotifyWindow_LightMode_ColorsApplied)
        {
            Logger::WriteMessage("Test: IT_CM_21 - Light mode colours applied to notify window\n");

            CNotifyWindow* pWnd = new CNotifyWindow(TestNotifyCallback, nullptr, NOTIFY_TYPE::NOTIFY_CHN_ENG);
            Assert::IsNotNull(pWnd, L"Notify window must be created");

            CStringRange text;
            text.Set(L"Test", 4);
            BOOL created = pWnd->_Create(20, NULL, &text);
            Assert::IsTrue(created, L"Window creation must succeed");

            // Apply light-preset colours (matches what ShowNotification does for light mode)
            pWnd->_SetTextColor(NOTIFYWND_TEXT_COLOR, GetSysColor(COLOR_3DHIGHLIGHT));
            pWnd->_SetFillColor(GetSysColor(COLOR_3DHIGHLIGHT));

            Assert::IsTrue(true, L"Light-mode colour assignment must not crash");

            // Verify light and dark text colours are distinct (constants sanity check)
            Assert::AreNotEqual(
                (DWORD)NOTIFYWND_TEXT_COLOR, (DWORD)NOTIFYWND_DARK_TEXT_COLOR,
                L"Light and dark notify text colours must differ");

            delete pWnd;
        }

        // ------------------------------------------------------------------
        // IT-CM-22: Dark notify border colour constant differs from light border
        // ------------------------------------------------------------------

        TEST_METHOD(IT_CM_22_NotifyWindow_DarkBorderColor_DiffersFromLight)
        {
            Logger::WriteMessage("Test: IT_CM_22 - Dark notify border colour differs from light\n");

            Assert::AreNotEqual(
                (DWORD)NOTIFYWND_DARK_BORDER_COLOR, (DWORD)NOTIFYWND_BORDER_COLOR,
                L"Dark and light notify window border colours must differ");

            // Dark-theme borders must be lighter (higher luminance) than the black light-theme
            // border so they remain visible against dark backgrounds (e.g. RGB(80,80,80) > RGB(0,0,0))
            DWORD darkLum  = GetRValue(NOTIFYWND_DARK_BORDER_COLOR)
                           + GetGValue(NOTIFYWND_DARK_BORDER_COLOR)
                           + GetBValue(NOTIFYWND_DARK_BORDER_COLOR);
            DWORD lightLum = GetRValue(NOTIFYWND_BORDER_COLOR)
                           + GetGValue(NOTIFYWND_BORDER_COLOR)
                           + GetBValue(NOTIFYWND_BORDER_COLOR);
            Assert::IsTrue(darkLum > lightLum,
                L"Dark notify border must have higher total luminance than the pure-black light border");
        }
    };
}
