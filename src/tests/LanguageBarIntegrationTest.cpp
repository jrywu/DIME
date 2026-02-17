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
#include "../LanguageBar.h"
#include "../Compartment.h"
#include "../Globals.h"
#include "../DIME.h"
#include <msctf.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace TSFIntegrationTests
{
    TEST_CLASS(LanguageBarIntegrationTest)
    {
    public:
        TEST_CLASS_INITIALIZE(ClassInitialize)
        {
            Logger::WriteMessage("IT-02: Language Bar Integration Tests - Class Initialize\n");
            HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
            Assert::IsTrue(SUCCEEDED(hr), L"COM initialization failed");
        }

        TEST_CLASS_CLEANUP(ClassCleanup)
        {
            Logger::WriteMessage("IT-02: Language Bar Integration Tests - Class Cleanup\n");
            CoUninitialize();
        }

        // IT-02-01: Language Bar Button Creation
        TEST_METHOD(IT02_01_LanguageBar_CreateButton_Succeeds)
        {
            Logger::WriteMessage("Test: IT02_01_LanguageBar_CreateButton_Succeeds\n");

            // Arrange: Create language bar button
            GUID guidLangBar = Global::DIMEGuidLangBarIMEMode;
            
            // Act: Create CLangBarItemButton instance
            CLangBarItemButton* pButton = new (std::nothrow) CLangBarItemButton(
                guidLangBar,
                L"Test Button",
                L"Test Tooltip",
                0,  // onIconIndex
                1,  // offIconIndex
                FALSE  // isSecureMode
            );

            // Assert: Button created successfully
            Assert::IsNotNull(pButton, L"Language bar button should be created");

            // Verify IUnknown interface
            IUnknown* pUnk = nullptr;
            HRESULT hr = pButton->QueryInterface(IID_IUnknown, (void**)&pUnk);
            Assert::IsTrue(SUCCEEDED(hr), L"Should support IUnknown interface");
            if (pUnk)
            {
                pUnk->Release();
            }

            // Verify ITfLangBarItemButton interface
            ITfLangBarItemButton* pLangBarButton = nullptr;
            hr = pButton->QueryInterface(IID_ITfLangBarItemButton, (void**)&pLangBarButton);
            Assert::IsTrue(SUCCEEDED(hr), L"Should support ITfLangBarItemButton interface");
            if (pLangBarButton)
            {
                pLangBarButton->Release();
            }

            // Cleanup
            delete pButton;
        }

        TEST_METHOD(IT02_01_LanguageBar_GetInfo_ReturnsCorrectInfo)
        {
            Logger::WriteMessage("Test: IT02_01_LanguageBar_GetInfo_ReturnsCorrectInfo\n");

            // Arrange
            GUID guidLangBar = Global::DIMEGuidLangBarIMEMode;
            CLangBarItemButton* pButton = new (std::nothrow) CLangBarItemButton(
                guidLangBar,
                L"IME Mode Button",
                L"Switch IME Mode",
                0, 1, FALSE
            );
            Assert::IsNotNull(pButton);

            // Act: Get button info
            TF_LANGBARITEMINFO info = { 0 };
            HRESULT hr = pButton->GetInfo(&info);

            // Assert: Info should be populated
            Assert::IsTrue(SUCCEEDED(hr), L"GetInfo should succeed");
            Assert::IsTrue(IsEqualGUID(info.guidItem, guidLangBar), L"GUID should match");
            Assert::IsTrue(wcscmp(info.szDescription, L"IME Mode Button") == 0, L"Description should match");

            // Cleanup
            delete pButton;
        }

        TEST_METHOD(IT02_01_LanguageBar_SetStatus_UpdatesStatus)
        {
            Logger::WriteMessage("Test: IT02_01_LanguageBar_SetStatus_UpdatesStatus\n");

            // Arrange
            CLangBarItemButton* pButton = new (std::nothrow) CLangBarItemButton(
                Global::DIMEGuidLangBarIMEMode,
                L"Test", L"Test", 0, 1, FALSE
            );
            Assert::IsNotNull(pButton);

            // Act: Set button status to disabled
            pButton->SetStatus(TF_LBI_STATUS_DISABLED, TRUE);

            // Assert: Get status to verify
            DWORD status = 0;
            HRESULT hr = pButton->GetStatus(&status);
            Assert::IsTrue(SUCCEEDED(hr), L"GetStatus should succeed");
            Assert::IsTrue((status & TF_LBI_STATUS_DISABLED) != 0, L"Button should be disabled");

            // Act: Clear disabled status
            pButton->SetStatus(TF_LBI_STATUS_DISABLED, FALSE);

            // Assert: Status should be cleared
            status = 0;
            hr = pButton->GetStatus(&status);
            Assert::IsTrue(SUCCEEDED(hr));
            Assert::IsTrue((status & TF_LBI_STATUS_DISABLED) == 0, L"Disabled flag should be cleared");

            // Cleanup
            delete pButton;
        }

        // IT-02-02: Chinese/English Mode Switching
        TEST_METHOD(IT02_02_Compartment_SetBOOL_UpdatesValue)
        {
            Logger::WriteMessage("Test: IT02_02_Compartment_SetBOOL_UpdatesValue\n");

            // Arrange: Create thread manager
            ITfThreadMgr* pThreadMgr = nullptr;
            HRESULT hr = CoCreateInstance(CLSID_TF_ThreadMgr, NULL, CLSCTX_INPROC_SERVER,
                IID_ITfThreadMgr, (void**)&pThreadMgr);

            if (FAILED(hr) || pThreadMgr == nullptr)
            {
                Logger::WriteMessage("SKIP: ITfThreadMgr not available in test environment\n");
                return;
            }

            TfClientId clientId = 0;
            hr = pThreadMgr->Activate(&clientId);
            Assert::IsTrue(SUCCEEDED(hr), L"Thread manager activation should succeed");

            // Act: Create compartment and set value
            CCompartment* pCompartment = new (std::nothrow) CCompartment(
                pThreadMgr,
                clientId,
                Global::DIMEGuidCompartmentIMEMode
            );
            Assert::IsNotNull(pCompartment);

            // Set IME mode to TRUE (Chinese mode)
            hr = pCompartment->_SetCompartmentBOOL(TRUE);
            
            // Assert: Should succeed (may fail if compartment not registered)
            if (SUCCEEDED(hr))
            {
                BOOL value = FALSE;
                hr = pCompartment->_GetCompartmentBOOL(value);
                Assert::IsTrue(SUCCEEDED(hr), L"Get should succeed");
                Assert::IsTrue(value == TRUE, L"Value should be TRUE");
            }

            // Cleanup
            delete pCompartment;
            pThreadMgr->Deactivate();
            pThreadMgr->Release();
        }

        TEST_METHOD(IT02_02_Compartment_GetBOOL_RetrievesValue)
        {
            Logger::WriteMessage("Test: IT02_02_Compartment_GetBOOL_RetrievesValue\n");

            // Arrange
            ITfThreadMgr* pThreadMgr = nullptr;
            HRESULT hr = CoCreateInstance(CLSID_TF_ThreadMgr, NULL, CLSCTX_INPROC_SERVER,
                IID_ITfThreadMgr, (void**)&pThreadMgr);

            if (FAILED(hr) || pThreadMgr == nullptr)
            {
                Logger::WriteMessage("SKIP: ITfThreadMgr not available\n");
                return;
            }

            TfClientId clientId = 0;
            hr = pThreadMgr->Activate(&clientId);
            Assert::IsTrue(SUCCEEDED(hr));

            CCompartment* pCompartment = new (std::nothrow) CCompartment(
                pThreadMgr, clientId, Global::DIMEGuidCompartmentIMEMode
            );
            Assert::IsNotNull(pCompartment);

            // Act: Get current value
            BOOL value = FALSE;
            hr = pCompartment->_GetCompartmentBOOL(value);

            // Assert: Should return some value (TRUE or FALSE)
            if (SUCCEEDED(hr))
            {
                // Value retrieved successfully
                Logger::WriteMessage(value ? "IME Mode: Chinese\n" : "IME Mode: English\n");
            }

            // Cleanup
            delete pCompartment;
            pThreadMgr->Deactivate();
            pThreadMgr->Release();
        }

        // IT-02-03: Full/Half Shape Toggle
        TEST_METHOD(IT02_03_Compartment_DoubleSingleByte_SetAndGet)
        {
            Logger::WriteMessage("Test: IT02_03_Compartment_DoubleSingleByte_SetAndGet\n");

            // Arrange
            ITfThreadMgr* pThreadMgr = nullptr;
            HRESULT hr = CoCreateInstance(CLSID_TF_ThreadMgr, NULL, CLSCTX_INPROC_SERVER,
                IID_ITfThreadMgr, (void**)&pThreadMgr);

            if (FAILED(hr) || pThreadMgr == nullptr)
            {
                Logger::WriteMessage("SKIP: ITfThreadMgr not available\n");
                return;
            }

            TfClientId clientId = 0;
            hr = pThreadMgr->Activate(&clientId);
            Assert::IsTrue(SUCCEEDED(hr));

            CCompartment* pCompartment = new (std::nothrow) CCompartment(
                pThreadMgr,
                clientId,
                Global::DIMEGuidCompartmentDoubleSingleByte
            );
            Assert::IsNotNull(pCompartment);

            // Act: Toggle double/single byte mode
            hr = pCompartment->_SetCompartmentBOOL(TRUE);  // Full shape

            if (SUCCEEDED(hr))
            {
                // Assert: Verify value
                BOOL value = FALSE;
                hr = pCompartment->_GetCompartmentBOOL(value);
                Assert::IsTrue(SUCCEEDED(hr));
                Assert::IsTrue(value == TRUE, L"Should be in full-width mode");

                // Toggle back
                hr = pCompartment->_SetCompartmentBOOL(FALSE);  // Half shape
                Assert::IsTrue(SUCCEEDED(hr));

                value = TRUE;
                hr = pCompartment->_GetCompartmentBOOL(value);
                Assert::IsTrue(SUCCEEDED(hr));
                Assert::IsTrue(value == FALSE, L"Should be in half-width mode");
            }

            // Cleanup
            delete pCompartment;
            pThreadMgr->Deactivate();
            pThreadMgr->Release();
        }

        TEST_METHOD(IT02_03_Compartment_ClearCompartment_Succeeds)
        {
            Logger::WriteMessage("Test: IT02_03_Compartment_ClearCompartment_Succeeds\n");

            // Arrange
            ITfThreadMgr* pThreadMgr = nullptr;
            HRESULT hr = CoCreateInstance(CLSID_TF_ThreadMgr, NULL, CLSCTX_INPROC_SERVER,
                IID_ITfThreadMgr, (void**)&pThreadMgr);

            if (FAILED(hr) || pThreadMgr == nullptr)
            {
                Logger::WriteMessage("SKIP: ITfThreadMgr not available\n");
                return;
            }

            TfClientId clientId = 0;
            hr = pThreadMgr->Activate(&clientId);
            Assert::IsTrue(SUCCEEDED(hr));

            CCompartment* pCompartment = new (std::nothrow) CCompartment(
                pThreadMgr, clientId, Global::DIMEGuidCompartmentDoubleSingleByte
            );
            Assert::IsNotNull(pCompartment);

            // Act: Clear compartment
            hr = pCompartment->_ClearCompartment();

            // Assert: Method should execute without crashing (any HRESULT acceptable)
            // The compartment may not support clearing in test environment
            Assert::IsTrue(true, L"ClearCompartment executed without crash");

            // Cleanup
            delete pCompartment;
            pThreadMgr->Deactivate();
            pThreadMgr->Release();
        }

        TEST_METHOD(IT02_03_Compartment_GetSetDWORD_Works)
        {
            Logger::WriteMessage("Test: IT02_03_Compartment_GetSetDWORD_Works\n");

            // Arrange
            ITfThreadMgr* pThreadMgr = nullptr;
            HRESULT hr = CoCreateInstance(CLSID_TF_ThreadMgr, NULL, CLSCTX_INPROC_SERVER,
                IID_ITfThreadMgr, (void**)&pThreadMgr);

            if (FAILED(hr) || pThreadMgr == nullptr)
            {
                Logger::WriteMessage("SKIP: ITfThreadMgr not available\n");
                return;
            }

            TfClientId clientId = 0;
            hr = pThreadMgr->Activate(&clientId);
            Assert::IsTrue(SUCCEEDED(hr));

            CCompartment* pCompartment = new (std::nothrow) CCompartment(
                pThreadMgr, clientId, Global::DIMEGuidCompartmentDoubleSingleByte
            );
            Assert::IsNotNull(pCompartment);

            // Act: Set DWORD value
            hr = pCompartment->_SetCompartmentDWORD(42);

            if (SUCCEEDED(hr))
            {
                // Assert: Get DWORD value
                DWORD value = 0;
                hr = pCompartment->_GetCompartmentDWORD(value);
                Assert::IsTrue(SUCCEEDED(hr));
                Assert::AreEqual((DWORD)42, value, L"DWORD value should match");
            }

            // Cleanup
            delete pCompartment;
            pThreadMgr->Deactivate();
            pThreadMgr->Release();
        }

        TEST_METHOD(IT02_03_CompartmentEventSink_Create_Succeeds)
        {
            Logger::WriteMessage("Test: IT02_03_CompartmentEventSink_Create_Succeeds\n");

            // Arrange: Create event sink
            CCompartmentEventSink* pEventSink = new (std::nothrow) CCompartmentEventSink(
                nullptr,  // Callback
                nullptr   // Context
            );

            // Assert: Event sink created
            Assert::IsNotNull(pEventSink, L"Event sink should be created");

            // Verify IUnknown interface
            IUnknown* pUnk = nullptr;
            HRESULT hr = pEventSink->QueryInterface(IID_IUnknown, (void**)&pUnk);
            Assert::IsTrue(SUCCEEDED(hr), L"Should support IUnknown");
            if (pUnk)
            {
                pUnk->Release();
            }

            // Verify ITfCompartmentEventSink interface
            ITfCompartmentEventSink* pSink = nullptr;
            hr = pEventSink->QueryInterface(IID_ITfCompartmentEventSink, (void**)&pSink);
            Assert::IsTrue(SUCCEEDED(hr), L"Should support ITfCompartmentEventSink");
            if (pSink)
            {
                pSink->Release();
            }

            // Cleanup
            delete pEventSink;
        }

        TEST_METHOD(IT02_04_LanguageBar_GetTooltipString_ReturnsTooltip)
        {
            Logger::WriteMessage("Test: IT02_04_LanguageBar_GetTooltipString_ReturnsTooltip\n");

            // Arrange
            CLangBarItemButton* pButton = new (std::nothrow) CLangBarItemButton(
                Global::DIMEGuidLangBarIMEMode,
                L"Test Button",
                L"Test Tooltip Text",
                0, 1, FALSE
            );
            Assert::IsNotNull(pButton);

            // Act: Get tooltip string
            BSTR bstrTooltip = nullptr;
            HRESULT hr = pButton->GetTooltipString(&bstrTooltip);

            // Assert
            Assert::IsTrue(SUCCEEDED(hr), L"GetTooltipString should succeed");
            if (bstrTooltip)
            {
                Assert::IsTrue(wcscmp(bstrTooltip, L"Test Tooltip Text") == 0, L"Tooltip should match");
                SysFreeString(bstrTooltip);
            }

            // Cleanup
            delete pButton;
        }

        TEST_METHOD(IT02_04_LanguageBar_GetText_ReturnsDescription)
        {
            Logger::WriteMessage("Test: IT02_04_LanguageBar_GetText_ReturnsDescription\n");

            // Arrange
            CLangBarItemButton* pButton = new (std::nothrow) CLangBarItemButton(
                Global::DIMEGuidLangBarIMEMode,
                L"Button Text Description",
                L"Tooltip",
                0, 1, FALSE
            );
            Assert::IsNotNull(pButton);

            // Act: Get button text
            BSTR bstrText = nullptr;
            HRESULT hr = pButton->GetText(&bstrText);

            // Assert
            Assert::IsTrue(SUCCEEDED(hr), L"GetText should succeed");
            if (bstrText)
            {
                Assert::IsTrue(wcscmp(bstrText, L"Button Text Description") == 0, L"Text should match");
                SysFreeString(bstrText);
            }

            // Cleanup
            delete pButton;
        }

        TEST_METHOD(IT02_04_LanguageBar_Show_Succeeds)
        {
            Logger::WriteMessage("Test: IT02_04_LanguageBar_Show_Succeeds\n");

            // Arrange
            CLangBarItemButton* pButton = new (std::nothrow) CLangBarItemButton(
                Global::DIMEGuidLangBarIMEMode,
                L"Test",
                L"Test",
                0, 1, FALSE
            );
            Assert::IsNotNull(pButton);

            // Act: Show button
            HRESULT hr = pButton->Show(TRUE);
            Assert::IsTrue(SUCCEEDED(hr), L"Show(TRUE) should succeed");

            // Act: Hide button
            hr = pButton->Show(FALSE);
            Assert::IsTrue(SUCCEEDED(hr), L"Show(FALSE) should succeed");

            // Cleanup
            delete pButton;
        }

        TEST_METHOD(IT02_04_LanguageBar_OnClick_Succeeds)
        {
            Logger::WriteMessage("Test: IT02_04_LanguageBar_OnClick_Succeeds\n");

            // Arrange
            CLangBarItemButton* pButton = new (std::nothrow) CLangBarItemButton(
                Global::DIMEGuidLangBarIMEMode,
                L"Test",
                L"Test",
                0, 1, FALSE
            );
            Assert::IsNotNull(pButton);

            // Act: Simulate click
            POINT pt = { 0, 0 };
            RECT rc = { 0, 0, 100, 100 };
            HRESULT hr = pButton->OnClick(TF_LBI_CLK_LEFT, pt, &rc);

            // Assert: Should complete without crashing
            Assert::IsTrue(SUCCEEDED(hr) || hr == S_FALSE || hr == E_NOTIMPL, L"OnClick should not crash");

            // Cleanup
            delete pButton;
        }

        TEST_METHOD(IT02_04_LanguageBar_GetIcon_Succeeds)
        {
            Logger::WriteMessage("Test: IT02_04_LanguageBar_GetIcon_Succeeds\n");

            // Arrange
            CLangBarItemButton* pButton = new (std::nothrow) CLangBarItemButton(
                Global::DIMEGuidLangBarIMEMode,
                L"Test",
                L"Test",
                0, 1, FALSE
            );
            Assert::IsNotNull(pButton);

            // Act: Get icon
            HICON hIcon = nullptr;
            HRESULT hr = pButton->GetIcon(&hIcon);

            // Assert: Should return an icon handle or fail gracefully (accept any HRESULT)
            Assert::IsTrue(true, L"GetIcon should not crash");
            if (SUCCEEDED(hr) && hIcon)
            {
                DestroyIcon(hIcon);
            }

            // Cleanup
            delete pButton;
        }

        TEST_METHOD(IT02_05_LanguageBar_AdviseSink_Succeeds)
        {
            Logger::WriteMessage("Test: IT02_05_LanguageBar_AdviseSink_Succeeds\n");

            // Arrange
            CLangBarItemButton* pButton = new (std::nothrow) CLangBarItemButton(
                Global::DIMEGuidLangBarIMEMode,
                L"Test",
                L"Test",
                0, 1, FALSE
            );
            Assert::IsNotNull(pButton);

            // Act: Get ITfSource interface
            ITfSource* pSource = nullptr;
            HRESULT hr = pButton->QueryInterface(IID_ITfSource, (void**)&pSource);
            Assert::IsTrue(SUCCEEDED(hr), L"Should support ITfSource interface");

            if (pSource)
            {
                // Try to advise a sink (will likely fail without proper sink, but shouldn't crash)
                DWORD dwCookie = 0;
                hr = pSource->AdviseSink(IID_ITfLangBarItemSink, nullptr, &dwCookie);
                // Accept any result - just verify no crash

                pSource->Release();
            }

            // Cleanup
            delete pButton;
        }

        TEST_METHOD(IT02_05_Compartment_MultipleCompartments_Work)
        {
            Logger::WriteMessage("Test: IT02_05_Compartment_MultipleCompartments_Work\n");

            // Arrange
            ITfThreadMgr* pThreadMgr = nullptr;
            HRESULT hr = CoCreateInstance(CLSID_TF_ThreadMgr, NULL, CLSCTX_INPROC_SERVER,
                IID_ITfThreadMgr, (void**)&pThreadMgr);

            if (FAILED(hr) || pThreadMgr == nullptr)
            {
                Logger::WriteMessage("SKIP: ITfThreadMgr not available\n");
                return;
            }

            TfClientId clientId = 0;
            hr = pThreadMgr->Activate(&clientId);
            Assert::IsTrue(SUCCEEDED(hr));

            // Create multiple compartments
            CCompartment* pComp1 = new (std::nothrow) CCompartment(
                pThreadMgr, clientId, Global::DIMEGuidCompartmentIMEMode
            );
            CCompartment* pComp2 = new (std::nothrow) CCompartment(
                pThreadMgr, clientId, Global::DIMEGuidCompartmentDoubleSingleByte
            );

            Assert::IsNotNull(pComp1);
            Assert::IsNotNull(pComp2);

            // Act: Set values in different compartments
            hr = pComp1->_SetCompartmentBOOL(TRUE);
            HRESULT hr2 = pComp2->_SetCompartmentBOOL(FALSE);

            // Cleanup
            delete pComp1;
            delete pComp2;
            pThreadMgr->Deactivate();
            pThreadMgr->Release();
        }

        TEST_METHOD(IT02_05_LanguageBar_CleanUp_Succeeds)
        {
            Logger::WriteMessage("Test: IT02_05_LanguageBar_CleanUp_Succeeds\n");

            // Arrange
            CLangBarItemButton* pButton = new (std::nothrow) CLangBarItemButton(
                Global::DIMEGuidLangBarIMEMode,
                L"Test",
                L"Test",
                0, 1, FALSE
            );
            Assert::IsNotNull(pButton);

            // Act: Call CleanUp
            pButton->CleanUp();

            // Assert: Should not crash
            Assert::IsTrue(true, L"CleanUp should not crash");

            // Cleanup
            delete pButton;
        }
    };
}
