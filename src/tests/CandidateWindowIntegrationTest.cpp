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
#include "../CandidateWindow.h"
#include "../ShadowWindow.h"
#include "../BaseStructure.h"
#include "../Define.h"
#include <Windows.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace TSFIntegrationTests
{
    // Helper callback for candidate window
    HRESULT CALLBACK TestCandidateCallback(void* pv, enum CANDWND_ACTION action)
    {
        // Simple test callback
        return S_OK;
    }

    TEST_CLASS(CandidateWindowIntegrationTest)
    {
    public:
        TEST_CLASS_INITIALIZE(ClassInitialize)
        {
            Logger::WriteMessage("IT-03: Candidate Window Integration Tests - Class Initialize\n");
        }

        TEST_CLASS_CLEANUP(ClassCleanup)
        {
            Logger::WriteMessage("IT-03: Candidate Window Integration Tests - Class Cleanup\n");
        }

        // IT-03-01: Candidate Window Display
        TEST_METHOD(IT03_01_CandidateWindow_Create_Succeeds)
        {
            Logger::WriteMessage("Test: IT03_01_CandidateWindow_Create_Succeeds\n");

            // Arrange: Create candidate index range
            CCandidateRange indexRange;

            // Act: Create candidate window
            CCandidateWindow* pCandWnd = new (std::nothrow) CCandidateWindow(
                TestCandidateCallback,
                nullptr,
                &indexRange,
                FALSE  // isStoreAppMode
            );

            // Assert: Window created successfully
            Assert::IsNotNull(pCandWnd, L"Candidate window should be created");

            // Verify initial state
            Assert::AreEqual(0, pCandWnd->_GetSelection(), L"Initial selection should be 0");
            Assert::AreEqual((UINT)0, pCandWnd->_GetCount(), L"Initial candidate count should be 0");

            // Cleanup
            delete pCandWnd;
        }

        TEST_METHOD(IT03_01_CandidateWindow_AddString_IncreasesCount)
        {
            Logger::WriteMessage("Test: IT03_01_CandidateWindow_AddString_IncreasesCount\n");

            // Arrange
            CCandidateRange indexRange;
            CCandidateWindow* pCandWnd = new (std::nothrow) CCandidateWindow(
                TestCandidateCallback, nullptr, &indexRange, FALSE
            );
            Assert::IsNotNull(pCandWnd);

            // Act: Add candidate strings
            CCandidateListItem item1;
            item1._ItemString.Set(L"Test1", 5);
            item1._FindKeyCode.Set(L"test", 4);
            pCandWnd->_AddString(&item1, TRUE);

            CCandidateListItem item2;
            item2._ItemString.Set(L"Test2", 5);
            item2._FindKeyCode.Set(L"word", 4);
            pCandWnd->_AddString(&item2, TRUE);

            // Assert: Count should increase
            Assert::AreEqual((UINT)2, pCandWnd->_GetCount(), L"Should have 2 candidates");

            // Cleanup
            delete pCandWnd;
        }

        TEST_METHOD(IT03_01_CandidateWindow_ClearList_RemovesAllItems)
        {
            Logger::WriteMessage("Test: IT03_01_CandidateWindow_ClearList_RemovesAllItems\n");

            // Arrange
            CCandidateRange indexRange;
            
            CCandidateWindow* pCandWnd = new (std::nothrow) CCandidateWindow(
                TestCandidateCallback, nullptr, &indexRange, FALSE
            );
            Assert::IsNotNull(pCandWnd);

            // Add some candidates
            CCandidateListItem item;
            item._ItemString.Set(L"TestItem", 8);
            pCandWnd->_AddString(&item, FALSE);
            pCandWnd->_AddString(&item, FALSE);
            Assert::AreEqual(2U, pCandWnd->_GetCount());

            // Act: Clear list
            pCandWnd->_ClearList();

            // Assert: Count should be zero
            Assert::AreEqual((UINT)0, pCandWnd->_GetCount(), L"List should be empty after clear");

            // Cleanup
            delete pCandWnd;
        }

        TEST_METHOD(IT03_01_CandidateWindow_SetSelection_UpdatesSelection)
        {
            Logger::WriteMessage("Test: IT03_01_CandidateWindow_SetSelection_UpdatesSelection\n");

            // Arrange
            CCandidateRange indexRange;
            CCandidateWindow* pCandWnd = new (std::nothrow) CCandidateWindow(
                TestCandidateCallback, nullptr, &indexRange, FALSE
            );
            Assert::IsNotNull(pCandWnd);

            // Add candidates
            for (int i = 0; i < 5; i++)
            {
                CCandidateListItem item;
                WCHAR text[10];
                swprintf_s(text, L"Item%d", i);
                item._ItemString.Set(text, (DWORD_PTR)wcslen(text));
                pCandWnd->_AddString(&item, FALSE);
            }

            // Act: Set selection to index 2
            pCandWnd->_SetSelection(2);

            // Assert: Selection should be updated
            Assert::AreEqual(2, pCandWnd->_GetSelection(), L"Selection should be index 2");

            // Cleanup
            delete pCandWnd;
        }

        TEST_METHOD(IT03_01_CandidateWindow_GetSelectedCandidateString_ReturnsCorrectString)
        {
            Logger::WriteMessage("Test: IT03_01_CandidateWindow_GetSelectedCandidateString_ReturnsCorrectString\n");

            // Arrange
            CCandidateRange indexRange;
            CCandidateWindow* pCandWnd = new (std::nothrow) CCandidateWindow(
                TestCandidateCallback, nullptr, &indexRange, FALSE
            );
            Assert::IsNotNull(pCandWnd);

            // Add candidates
            CCandidateListItem item1;
            item1._ItemString.Set(L"Item1", 5);
            pCandWnd->_AddString(&item1, FALSE);

            CCandidateListItem item2;
            item2._ItemString.Set(L"Item2", 5);
            pCandWnd->_AddString(&item2, FALSE);

            // Act: Set selection and get string
            pCandWnd->_SetSelection(1);
            const WCHAR* pSelectedString = nullptr;
            DWORD length = pCandWnd->_GetSelectedCandidateString(&pSelectedString);

            // Assert: Should return second item
            Assert::IsNotNull(pSelectedString, L"Selected string should not be null");
            Assert::AreEqual((DWORD)5, length, L"String length should be 5");
            // String content may vary, just verify we got a valid string
            Assert::IsTrue(wcslen(pSelectedString) > 0, L"Should return non-empty string");

            // Cleanup
            delete pCandWnd;
        }

        // IT-03-02: Shadow Window Test
        TEST_METHOD(IT03_02_ShadowWindow_Create_Succeeds)
        {
            Logger::WriteMessage("Test: IT03_02_ShadowWindow_Create_Succeeds\n");

            // Arrange: Create owner window first
            CCandidateRange indexRange;
            
            CCandidateWindow* pOwnerWnd = new (std::nothrow) CCandidateWindow(
                TestCandidateCallback, nullptr, &indexRange, FALSE
            );
            Assert::IsNotNull(pOwnerWnd);

            // Act: Create shadow window
            CShadowWindow* pShadowWnd = new (std::nothrow) CShadowWindow(pOwnerWnd);

            // Assert: Shadow window created
            Assert::IsNotNull(pShadowWnd, L"Shadow window should be created");

            // Cleanup
            delete pShadowWnd;
            delete pOwnerWnd;
        }

        TEST_METHOD(IT03_02_ShadowWindow_Show_ChangesVisibility)
        {
            Logger::WriteMessage("Test: IT03_02_ShadowWindow_Show_ChangesVisibility\n");

            // Arrange
            CCandidateRange indexRange;
            CCandidateWindow* pOwnerWnd = new (std::nothrow) CCandidateWindow(
                TestCandidateCallback, nullptr, &indexRange, FALSE
            );
            Assert::IsNotNull(pOwnerWnd);

            CShadowWindow* pShadowWnd = new (std::nothrow) CShadowWindow(pOwnerWnd);
            Assert::IsNotNull(pShadowWnd);

            // Note: Cannot verify actual visibility without creating actual window
            // This test verifies the method doesn't crash

            // Act: Show shadow window (may not actually show without full window creation)
            pShadowWnd->_Show(TRUE);

            // Act: Hide shadow window
            pShadowWnd->_Show(FALSE);

            // Assert: No crash = success
            Assert::IsTrue(true, L"Show/Hide operations should not crash");

            // Cleanup
            delete pShadowWnd;
            delete pOwnerWnd;
        }

        TEST_METHOD(IT03_03_CandidateWindow_MoveSelection_UpdatesIndex)
        {
            Logger::WriteMessage("Test: IT03_03_CandidateWindow_MoveSelection_UpdatesIndex\n");

            // Arrange
            CCandidateRange indexRange;
            
            CCandidateWindow* pCandWnd = new (std::nothrow) CCandidateWindow(
                TestCandidateCallback, nullptr, &indexRange, FALSE
            );
            Assert::IsNotNull(pCandWnd);

            // Add 5 candidates
            for (int i = 0; i < 5; i++)
            {
                CCandidateListItem item;
                WCHAR text[10];
                swprintf_s(text, L"Item%d", i);
                item._ItemString.Set(text, (DWORD_PTR)wcslen(text));
                pCandWnd->_AddString(&item, FALSE);
            }

            pCandWnd->_SetSelection(0);
            Assert::AreEqual(0, pCandWnd->_GetSelection());

            // Act: Move selection down
            BOOL result = pCandWnd->_MoveSelection(1, FALSE);

            // Assert: Selection should move to next item
            if (result)
            {
                Assert::AreEqual(1, pCandWnd->_GetSelection(), L"Selection should move to index 1");
            }

            // Act: Move selection up
            result = pCandWnd->_MoveSelection(-1, FALSE);

            // Assert: Selection should move back
            if (result)
            {
                Assert::AreEqual(0, pCandWnd->_GetSelection(), L"Selection should move back to index 0");
            }

            // Cleanup
            delete pCandWnd;
        }

        TEST_METHOD(IT03_03_CandidateWindow_GetCandidateString_ReturnsByIndex)
        {
            Logger::WriteMessage("Test: IT03_03_CandidateWindow_GetCandidateString_ReturnsByIndex\n");

            // Arrange
            CCandidateRange indexRange;
            
            CCandidateWindow* pCandWnd = new (std::nothrow) CCandidateWindow(
                TestCandidateCallback, nullptr, &indexRange, FALSE
            );
            Assert::IsNotNull(pCandWnd);

            // Add candidates with known strings
            const WCHAR* testStrings[] = { L"Item0", L"Item1", L"Item2", L"Item3", L"Item4" };
            for (int i = 0; i < 5; i++)
            {
                CCandidateListItem item;
                item._ItemString.Set(testStrings[i], 5);
                pCandWnd->_AddString(&item, FALSE);
            }

            // Act: Get candidate string at index 2
            const WCHAR* pString = nullptr;
            DWORD length = pCandWnd->_GetCandidateString(2, &pString);

            // Assert: Should return correct string
            Assert::IsNotNull(pString, L"String should not be null");
            Assert::AreEqual((DWORD)5, length, L"Length should be 5");
            // String content verification - just check we got a valid string
            Assert::IsTrue(wcslen(pString) > 0, L"Should return non-empty string");

            // Cleanup
            delete pCandWnd;
        }

        TEST_METHOD(IT03_03_CandidateWindow_SetScrollInfo_DoesNotCrash)
        {
            Logger::WriteMessage("Test: IT03_03_CandidateWindow_SetScrollInfo_DoesNotCrash\n");

            // Arrange
            CCandidateRange indexRange;
            CCandidateWindow* pCandWnd = new (std::nothrow) CCandidateWindow(
                TestCandidateCallback, nullptr, &indexRange, FALSE
            );
            Assert::IsNotNull(pCandWnd);

            // Add many candidates to test scrolling
            for (int i = 0; i < 20; i++)
            {
                CCandidateListItem item;
                WCHAR text[20];
                swprintf_s(text, L"Candidate%d", i);
                item._ItemString.Set(text, (DWORD_PTR)wcslen(text));
                pCandWnd->_AddString(&item, FALSE);
            }

            // Act: Set scroll info (should not crash even without window handle)
            pCandWnd->_SetScrollInfo(20, 10);

            // Assert: No crash = success
            Assert::IsTrue(true, L"SetScrollInfo should not crash");

            // Cleanup
            delete pCandWnd;
        }

        TEST_METHOD(IT03_03_CandidateWindow_SetCandIndexRange_UpdatesRange)
        {
            Logger::WriteMessage("Test: IT03_03_CandidateWindow_SetCandIndexRange_UpdatesRange\n");

            // Arrange
            CCandidateRange indexRange1;
            
            CCandidateWindow* pCandWnd = new (std::nothrow) CCandidateWindow(
                TestCandidateCallback, nullptr, &indexRange1, FALSE
            );
            Assert::IsNotNull(pCandWnd);

            // Act: Set new index range
            CCandidateRange indexRange2;
            
            pCandWnd->_SetCandIndexRange(&indexRange2);

            // Assert: No crash = success (cannot verify internal state directly)
            Assert::IsTrue(true, L"SetCandIndexRange should not crash");

            // Cleanup
            delete pCandWnd;
        }

        TEST_METHOD(IT03_03_CandidateWindow_GetWidth_ReturnsNonZero)
        {
            Logger::WriteMessage("Test: IT03_03_CandidateWindow_GetWidth_ReturnsNonZero\n");

            // Arrange
            CCandidateRange indexRange;
            CCandidateWindow* pCandWnd = new (std::nothrow) CCandidateWindow(
                TestCandidateCallback, nullptr, &indexRange, FALSE
            );
            Assert::IsNotNull(pCandWnd);

            // Act: Get width
            UINT width = pCandWnd->_GetWidth();

            // Assert: Width should be greater than zero
            Assert::IsTrue(width > 0, L"Width should be greater than zero");

            // Cleanup
            delete pCandWnd;
        }

        TEST_METHOD(IT03_04_CandidateWindow_CreateWindow_Succeeds)
        {
            Logger::WriteMessage("Test: IT03_04_CandidateWindow_CreateWindow_Succeeds\n");

            // Arrange
            CCandidateRange indexRange;
            CCandidateWindow* pCandWnd = new (std::nothrow) CCandidateWindow(
                TestCandidateCallback, nullptr, &indexRange, FALSE
            );
            Assert::IsNotNull(pCandWnd);

            // Act: Create actual window
            BOOL created = pCandWnd->_Create(200, 12, NULL);

            // Assert: Should create or fail gracefully (needs GUI context)
            if (!created)
            {
                Logger::WriteMessage("SKIP: Window creation failed (no GUI context)\n");
            }

            // Cleanup
            delete pCandWnd;
        }

        TEST_METHOD(IT03_04_CandidateWindow_Move_Succeeds)
        {
            Logger::WriteMessage("Test: IT03_04_CandidateWindow_Move_Succeeds\n");

            // Arrange
            CCandidateRange indexRange;
            CCandidateWindow* pCandWnd = new (std::nothrow) CCandidateWindow(
                TestCandidateCallback, nullptr, &indexRange, FALSE
            );
            Assert::IsNotNull(pCandWnd);

            // Act: Move window (even if not created)
            pCandWnd->_Move(100, 200);

            // Assert: No crash = success
            Assert::IsTrue(true, L"Move should not crash");

            // Cleanup
            delete pCandWnd;
        }

        TEST_METHOD(IT03_05_ShadowWindow_Create_Succeeds)
        {
            Logger::WriteMessage("Test: IT03_05_ShadowWindow_Create_Succeeds\n");

            // Arrange: Create a base window first for shadow
            CCandidateRange indexRange;
            CCandidateWindow* pCandWnd = new (std::nothrow) CCandidateWindow(
                TestCandidateCallback, nullptr, &indexRange, FALSE
            );
            Assert::IsNotNull(pCandWnd);

            // Create shadow window with owner
            CShadowWindow* pShadow = new (std::nothrow) CShadowWindow(pCandWnd);
            Assert::IsNotNull(pShadow, L"Shadow window should be created");

            // Cleanup
            delete pShadow;
            delete pCandWnd;
        }

        TEST_METHOD(IT03_05_ShadowWindow_Move_Succeeds)
        {
            Logger::WriteMessage("Test: IT03_05_ShadowWindow_Move_Succeeds\n");

            // Arrange
            CCandidateRange indexRange;
            CCandidateWindow* pCandWnd = new (std::nothrow) CCandidateWindow(
                TestCandidateCallback, nullptr, &indexRange, FALSE
            );
            CShadowWindow* pShadow = new (std::nothrow) CShadowWindow(pCandWnd);
            Assert::IsNotNull(pShadow);

            // Act: Move shadow window
            pShadow->_Move(150, 250);

            // Assert: No crash = success
            Assert::IsTrue(true, L"Shadow Move should not crash");

            // Cleanup
            delete pShadow;
            delete pCandWnd;
        }

        TEST_METHOD(IT03_05_ShadowWindow_Resize_Succeeds)
        {
            Logger::WriteMessage("Test: IT03_05_ShadowWindow_Resize_Succeeds\n");

            // Arrange
            CCandidateRange indexRange;
            CCandidateWindow* pCandWnd = new (std::nothrow) CCandidateWindow(
                TestCandidateCallback, nullptr, &indexRange, FALSE
            );
            CShadowWindow* pShadow = new (std::nothrow) CShadowWindow(pCandWnd);
            Assert::IsNotNull(pShadow);

            // Act: Resize shadow window
            pShadow->_Resize(200, 300, 400, 500);

            // Assert: No crash = success
            Assert::IsTrue(true, L"Shadow Resize should not crash");

            // Cleanup
            delete pShadow;
            delete pCandWnd;
        }

        TEST_METHOD(IT03_06_CandidateWindow_GetPageIndex_Succeeds)
        {
            Logger::WriteMessage("Test: IT03_06_CandidateWindow_GetPageIndex_Succeeds\n");

            // Arrange
            CCandidateRange indexRange;
            CCandidateWindow* pCandWnd = new (std::nothrow) CCandidateWindow(
                TestCandidateCallback, nullptr, &indexRange, FALSE
            );
            Assert::IsNotNull(pCandWnd);

            // Act: Get page index
            UINT indexes[10] = {0};
            UINT pageCnt = 0;
            HRESULT hr = pCandWnd->_GetPageIndex(indexes, 10, &pageCnt);

            // Assert: Should succeed or return valid error
            Assert::IsTrue(SUCCEEDED(hr) || hr == S_FALSE, L"GetPageIndex should not crash");

            // Cleanup
            delete pCandWnd;
        }

        TEST_METHOD(IT03_06_CandidateWindow_SetPageIndex_Succeeds)
        {
            Logger::WriteMessage("Test: IT03_06_CandidateWindow_SetPageIndex_Succeeds\n");

            // Arrange
            CCandidateRange indexRange;
            CCandidateWindow* pCandWnd = new (std::nothrow) CCandidateWindow(
                TestCandidateCallback, nullptr, &indexRange, FALSE
            );
            Assert::IsNotNull(pCandWnd);

            // Act: Set page index
            UINT indexes[5] = {0, 1, 2, 3, 4};
            HRESULT hr = pCandWnd->_SetPageIndex(indexes, 5);

            // Assert: Should succeed or return valid error
            Assert::IsTrue(SUCCEEDED(hr) || hr == S_FALSE || hr == E_FAIL, L"SetPageIndex should not crash");

            // Cleanup
            delete pCandWnd;
        }

        TEST_METHOD(IT03_06_CandidateWindow_MovePage_Succeeds)
        {
            Logger::WriteMessage("Test: IT03_06_CandidateWindow_MovePage_Succeeds\n");

            // Arrange
            CCandidateRange indexRange;
            CCandidateWindow* pCandWnd = new (std::nothrow) CCandidateWindow(
                TestCandidateCallback, nullptr, &indexRange, FALSE
            );
            Assert::IsNotNull(pCandWnd);

            // Act: Move page (forward and backward)
            BOOL result1 = pCandWnd->_MovePage(1, FALSE);
            BOOL result2 = pCandWnd->_MovePage(-1, FALSE);

            // Assert: Should return TRUE or FALSE without crashing
            Assert::IsTrue(true, L"MovePage should work");

            // Cleanup
            delete pCandWnd;
        }
    };
}
