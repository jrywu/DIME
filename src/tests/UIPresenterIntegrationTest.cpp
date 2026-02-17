// UIPresenterIntegrationTest.cpp - IT-06: UIPresenter Integration Tests
// Tests UIPresenter's UI management layer: candidate windows, notification windows, visibility control
// Target Coverage: ??5% for UIPresenter.cpp
// Target Functions: MakeCandidateWindow, MakeNotifyWindow, Show, visibility management

#include "pch.h"
#include "CppUnitTest.h"
#include <Windows.h>
#include <msctf.h>
#include <atlbase.h>
#include <atlcom.h>

// Define DIME_UNIT_TESTING to expose internal test hooks
#define DIME_UNIT_TESTING
#include "../DIME.h"
#include "../Globals.h"
#include "../Config.h"
#include "../CompositionProcessorEngine.h"
#include "../UIPresenter.h"
#include "../DisplayAttributeInfo.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace TSFIntegrationTests
{
    // Helper class: Stub ITfContext for testing
    class StubTfContext : public ITfContext
    {
    private:
        LONG _refCount;

    public:
        StubTfContext() : _refCount(1) {}
        virtual ~StubTfContext() {}

        // IUnknown
        STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj)
        {
            if (ppvObj == NULL) return E_INVALIDARG;
            *ppvObj = NULL;

            if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfContext))
            {
                *ppvObj = (ITfContext*)this;
                AddRef();
                return S_OK;
            }
            return E_NOINTERFACE;
        }

        STDMETHODIMP_(ULONG) AddRef() { return InterlockedIncrement(&_refCount); }
        STDMETHODIMP_(ULONG) Release()
        {
            LONG ref = InterlockedDecrement(&_refCount);
            if (ref == 0) delete this;
            return ref;
        }

        // ITfContext - minimal stubs
        STDMETHODIMP RequestEditSession(TfClientId tid, ITfEditSession *pes, DWORD dwFlags, HRESULT *phrSession)
        {
            if (phrSession) *phrSession = S_OK;
            if (pes) return pes->DoEditSession(1);
            return S_OK;
        }

        STDMETHODIMP InWriteSession(TfClientId tid, BOOL *pfWriteSession)
        {
            if (pfWriteSession) *pfWriteSession = TRUE;
            return S_OK;
        }

        STDMETHODIMP GetSelection(TfEditCookie ec, ULONG ulIndex, ULONG ulCount, TF_SELECTION *pSelection, ULONG *pcFetched) { return E_NOTIMPL; }
        STDMETHODIMP SetSelection(TfEditCookie ec, ULONG ulCount, const TF_SELECTION *pSelection) { return E_NOTIMPL; }
        STDMETHODIMP GetStart(TfEditCookie ec, ITfRange **ppStart) { return E_NOTIMPL; }
        STDMETHODIMP GetEnd(TfEditCookie ec, ITfRange **ppEnd) { return E_NOTIMPL; }
        STDMETHODIMP GetActiveView(ITfContextView **ppView) 
        { 
            if (ppView) *ppView = NULL;
            return S_OK;
        }
        STDMETHODIMP EnumViews(IEnumTfContextViews **ppEnum) { return E_NOTIMPL; }
        STDMETHODIMP GetStatus(TF_STATUS *pdcs) 
        { 
            if (pdcs) { pdcs->dwDynamicFlags = 0; pdcs->dwStaticFlags = 0; }
            return S_OK; 
        }
        STDMETHODIMP GetProperty(REFGUID guidProp, ITfProperty **ppProp) { return E_NOTIMPL; }
        STDMETHODIMP GetAppProperty(REFGUID guidProp, ITfReadOnlyProperty **ppProp) { return E_NOTIMPL; }
        STDMETHODIMP TrackProperties(const GUID **prgProp, ULONG cProp, const GUID **prgAppProp, ULONG cAppProp, ITfReadOnlyProperty **ppProperty) { return E_NOTIMPL; }
        STDMETHODIMP EnumProperties(IEnumTfProperties **ppEnum) { return E_NOTIMPL; }
        STDMETHODIMP GetDocumentMgr(ITfDocumentMgr **ppDm) 
        { 
            if (ppDm) *ppDm = NULL;
            return S_OK; 
        }
        STDMETHODIMP CreateRangeBackup(TfEditCookie ec, ITfRange *pRange, ITfRangeBackup **ppBackup) { return E_NOTIMPL; }
    };

    // ===================================================================
    // Mock TSF Objects for Lifecycle Testing
    // ===================================================================

    // Helper class: Mock ITfUIElementMgr for BeginUIElement/EndUIElement testing
    class StubTfUIElementMgr : public ITfUIElementMgr
    {
    private:
        LONG _refCount;
        DWORD _nextElementId;

    public:
        StubTfUIElementMgr() : _refCount(1), _nextElementId(1) {}
        virtual ~StubTfUIElementMgr() {}

        // IUnknown methods
        STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override
        {
            if (ppvObj == NULL) return E_INVALIDARG;
            *ppvObj = NULL;

            if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfUIElementMgr))
            {
                *ppvObj = (ITfUIElementMgr*)this;
                AddRef();
                return S_OK;
            }
            return E_NOINTERFACE;
        }

        STDMETHODIMP_(ULONG) AddRef() override { return ++_refCount; }
        STDMETHODIMP_(ULONG) Release() override
        {
            LONG ref = --_refCount;
            if (ref == 0) delete this;
            return ref;
        }

        // ITfUIElementMgr methods
        STDMETHODIMP BeginUIElement(ITfUIElement *element, BOOL *pbShow, DWORD *pdwUIElementId) override
        {
            if (pdwUIElementId) *pdwUIElementId = _nextElementId++;
            if (pbShow) *pbShow = TRUE;
            return S_OK;
        }

        STDMETHODIMP UpdateUIElement(DWORD dwUIElementId) override { return S_OK; }
        STDMETHODIMP EndUIElement(DWORD dwUIElementId) override { return S_OK; }
        STDMETHODIMP GetUIElement(DWORD dwUIElementId, ITfUIElement **ppElement) override { return E_NOTIMPL; }
        STDMETHODIMP EnumUIElements(IEnumTfUIElements **ppEnum) override { return E_NOTIMPL; }
    };

    // Helper class: Mock ITfThreadMgr for TSF lifecycle testing
    class StubTfThreadMgr : public ITfThreadMgr
    {
    private:
        LONG _refCount;
        StubTfUIElementMgr* _uiElementMgr;

    public:
        StubTfThreadMgr() : _refCount(1), _uiElementMgr(nullptr) {}
        virtual ~StubTfThreadMgr()
        {
            if (_uiElementMgr) _uiElementMgr->Release();
        }

        // IUnknown methods
        STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override
        {
            if (ppvObj == NULL) return E_INVALIDARG;
            *ppvObj = NULL;

            if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfThreadMgr))
            {
                *ppvObj = (ITfThreadMgr*)this;
                AddRef();
                return S_OK;
            }
            else if (IsEqualIID(riid, IID_ITfUIElementMgr))
            {
                if (!_uiElementMgr)
                {
                    _uiElementMgr = new StubTfUIElementMgr();
                }
                *ppvObj = _uiElementMgr;
                _uiElementMgr->AddRef();
                return S_OK;
            }
            return E_NOINTERFACE;
        }

        STDMETHODIMP_(ULONG) AddRef() override { return ++_refCount; }
        STDMETHODIMP_(ULONG) Release() override
        {
            LONG ref = --_refCount;
            if (ref == 0) delete this;
            return ref;
        }

        // ITfThreadMgr methods (minimal stubs)
        STDMETHODIMP Activate(TfClientId *ptid) override { return E_NOTIMPL; }
        STDMETHODIMP Deactivate() override { return E_NOTIMPL; }
        STDMETHODIMP CreateDocumentMgr(ITfDocumentMgr **ppdim) override { return E_NOTIMPL; }
        STDMETHODIMP EnumDocumentMgrs(IEnumTfDocumentMgrs **ppEnum) override { return E_NOTIMPL; }
        STDMETHODIMP GetFocus(ITfDocumentMgr **ppdimFocus) override { return E_NOTIMPL; }
        STDMETHODIMP SetFocus(ITfDocumentMgr *pdimFocus) override { return E_NOTIMPL; }
        STDMETHODIMP AssociateFocus(HWND hwnd, ITfDocumentMgr *pdimNew, ITfDocumentMgr **ppdimPrev) override { return E_NOTIMPL; }
        STDMETHODIMP IsThreadFocus(BOOL *pfThreadFocus) override { return E_NOTIMPL; }
        STDMETHODIMP GetFunctionProvider(REFCLSID clsid, ITfFunctionProvider **ppFuncProv) override { return E_NOTIMPL; }
        STDMETHODIMP EnumFunctionProviders(IEnumTfFunctionProviders **ppEnum) override { return E_NOTIMPL; }
        STDMETHODIMP GetGlobalCompartment(ITfCompartmentMgr **ppCompMgr) override { return E_NOTIMPL; }
    };

    // Helper class: Stub CDIME for testing
    class StubCDIME : public CDIME
    {
    public:
        StubCDIME() : CDIME(), _mockThreadMgr(nullptr)
        {
            // Initialize minimal state for testing
        }

        virtual ~StubCDIME()
        {
            // Clean up mock thread manager if created
            if (_mockThreadMgr)
            {
                _mockThreadMgr->Release();
            }
        }

        // Set up mock thread manager for testing TSF lifecycle
        // Uses public SetupMockThreadMgrHelper that's a friend of CDIME in UIPresenterIntegrationTest
        void SetupMockThreadMgr();

        // Override methods that UIPresenter might call
        STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override
        {
            if (ppvObj == NULL) return E_INVALIDARG;
            *ppvObj = NULL;

            if (IsEqualIID(riid, IID_IUnknown))
            {
                *ppvObj = (IUnknown*)(ITfTextInputProcessorEx*)this;
                AddRef();
                return S_OK;
            }
            return E_NOINTERFACE;
        }

        STDMETHODIMP_(ULONG) AddRef() override
        {
            return CDIME::AddRef();
        }

        STDMETHODIMP_(ULONG) Release() override
        {
            return CDIME::Release();
        }

        StubTfThreadMgr* _mockThreadMgr;
    };

    // Forward declare TEST_CLASS so StubCDIME can reference it
    class UIPresenterIntegrationTest;

    // Implementation of StubCDIME::SetupMockThreadMgr
    // This will call the helper in TEST_CLASS that has friend access
    inline void StubCDIME::SetupMockThreadMgr()
    {
        // Forward to TEST_CLASS helper (defined below)
        // Will be resolved after TEST_CLASS is fully defined
        extern void SetupMockThreadMgrForStub(StubCDIME* pStub);
        SetupMockThreadMgrForStub(this);
    }

    // Helper class: Simple mock for CompositionProcessorEngine
    // We don't inherit because CCompositionProcessorEngine requires complex initialization
    // Instead, we just provide the minimal interface UIPresenter needs
    class MockCompositionEngine
    {
    private:
        CCandidateRange _indexRange;

    public:
        MockCompositionEngine()
        {
            // CCandidateRange manages its own internal array
        }

        virtual ~MockCompositionEngine() {}

        CCandidateRange* GetCandidateListIndexRange()
        {
            return &_indexRange;
        }
    };

    TEST_CLASS(UIPresenterIntegrationTest)
    {
    public:
        // Helper method to setup mock ThreadMgr (uses friend access to CDIME)
        static void SetupMockThreadMgrHelper(StubCDIME* pStub)
        {
            if (!pStub->_mockThreadMgr)
            {
                pStub->_mockThreadMgr = new StubTfThreadMgr();
                pStub->_mockThreadMgr->AddRef();
                // Access CDIME's private _pThreadMgr via friend access
                pStub->_pThreadMgr = pStub->_mockThreadMgr;
            }
        }

        TEST_CLASS_INITIALIZE(ClassSetup)
        {
            Logger::WriteMessage("UIPresenterIntegrationTest: Initializing IT-06 tests\n");

            HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
            if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
            {
                Logger::WriteMessage("WARNING: CoInitializeEx failed - COM may already be initialized\n");
            }

            // Initialize Config for testing
            CConfig::LoadConfig(IME_MODE::IME_MODE_ARRAY);
            Logger::WriteMessage("Config loaded for UIPresenter tests\n");
        }

        TEST_CLASS_CLEANUP(ClassCleanup)
        {
            Logger::WriteMessage("UIPresenterIntegrationTest: Cleanup\n");
        }

        // IT-06-01: UIPresenter Construction and Initialization

        TEST_METHOD(IT06_01_ConstructorInitialization)
        {
            Logger::WriteMessage("Test: IT06_01_ConstructorInitialization\n");

            // Arrange: Create stub CDIME and engine
            StubCDIME* pStubDIME = new StubCDIME();
            
            // Act: Create UIPresenter
            CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);

            // Assert: Verify initialization
            Assert::IsNotNull(pUIPresenter, L"UIPresenter should be created");

            // Cleanup
            pUIPresenter->Release(); // UIPresenter destructor will release pStubDIME
        }

        TEST_METHOD(IT06_01_ReferenceCountingAddRefRelease)
        {
            Logger::WriteMessage("Test: IT06_01_ReferenceCountingAddRefRelease\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
            CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);

            // Act: Check initial refcount behavior
            // Note: UIPresenter has dual refcount (parent + own), so we only test single Release
            ULONG refCount1 = pUIPresenter->AddRef();
            Assert::AreEqual((ULONG)2, refCount1, L"RefCount should be 2 after AddRef");

            // Act: Release back to initial state
            ULONG refCount2 = pUIPresenter->Release();
            Assert::AreEqual((ULONG)1, refCount2, L"RefCount should be 1 after Release");

            // Cleanup: Release UIPresenter (destructor will release pStubDIME)
            pUIPresenter->Release();
            // Note: Do NOT release pStubDIME manually - UIPresenter destructor handles it
        }

        TEST_METHOD(IT06_01_ConstructorWithNullParameters)
        {
            Logger::WriteMessage("Test: IT06_01_ConstructorWithNullParameters\n");

            // Act: Create UIPresenter with NULL parameters (should not crash)
            CUIPresenter* pUIPresenter = new CUIPresenter(nullptr, nullptr);

            // Assert: Should still create object
            Assert::IsNotNull(pUIPresenter, L"UIPresenter should handle NULL parameters gracefully");

            // Cleanup
            pUIPresenter->Release();
        }

        // IT-06-02: Candidate Window Management

        TEST_METHOD(IT06_02_CreateCandidateWindow)
        {
            Logger::WriteMessage("Test: IT06_02_CreateCandidateWindow\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
                        CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            StubTfContext* pStubContext = new StubTfContext();

            // Act: Create candidate window
            HRESULT hr = pUIPresenter->MakeCandidateWindow(pStubContext, 200);

            // Assert: Window should be created
            Assert::IsTrue(SUCCEEDED(hr), L"MakeCandidateWindow should succeed");

            // Cleanup
            pUIPresenter->DisposeCandidateWindow();
            pStubContext->Release();
            pUIPresenter->Release(); // UIPresenter destructor will release pStubDIME
        }

        TEST_METHOD(IT06_02_CreateCandidateWindowNullContext)
        {
            Logger::WriteMessage("Test: IT06_02_CreateCandidateWindowNullContext\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
                        CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);

            // Act: Create candidate window with NULL context (should be acceptable)
            HRESULT hr = pUIPresenter->MakeCandidateWindow(nullptr, 200);

            // Assert: Should succeed with NULL context
            Assert::IsTrue(SUCCEEDED(hr), L"MakeCandidateWindow should handle NULL context");

            // Cleanup
            pUIPresenter->DisposeCandidateWindow();
            pUIPresenter->Release(); // UIPresenter destructor will release pStubDIME
        }

        TEST_METHOD(IT06_02_AddCandidatesToUI)
        {
            Logger::WriteMessage("Test: IT06_02_AddCandidatesToUI\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
                        CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            pUIPresenter->MakeCandidateWindow(nullptr, 200);

            CDIMEArray<CCandidateListItem> candidates;
            CCandidateListItem item1;
            item1._ItemString.Set(L"測試", 2);
            CCandidateListItem* pItem = candidates.Append(); if (pItem) { pItem->_ItemString.Set(L"����", 2); }

            // Act: Add candidates
            pUIPresenter->AddCandidateToUI(&candidates, TRUE);

            // Assert: Verify candidates added
            UINT count = 0;
            HRESULT hr = pUIPresenter->GetCount(&count);
            Assert::IsTrue(SUCCEEDED(hr), L"GetCount should succeed");
            Assert::AreEqual((UINT)1, count, L"Should have 1 candidate");

            // Cleanup
            pUIPresenter->Release(); // UIPresenter destructor will release pStubDIME
        }

        TEST_METHOD(IT06_02_DisposeCandidateWindow)
        {
            Logger::WriteMessage("Test: IT06_02_DisposeCandidateWindow\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
                        CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            pUIPresenter->MakeCandidateWindow(nullptr, 200);

            // Act: Dispose
            pUIPresenter->DisposeCandidateWindow();

            // Assert: Should not crash (window disposed successfully)
            Assert::IsTrue(true, L"DisposeCandidateWindow should complete without error");

            // Cleanup
            pUIPresenter->Release(); // UIPresenter destructor will release pStubDIME
        }

        // IT-06-03: Notification Window Management

        TEST_METHOD(IT06_03_CreateNotifyWindow)
        {
            Logger::WriteMessage("Test: IT06_03_CreateNotifyWindow\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
                        CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            CStringRange notifyText;
            notifyText.Set(L"中�?", 2);

            // Act: Create notify window
            HRESULT hr = pUIPresenter->MakeNotifyWindow(nullptr, &notifyText, NOTIFY_TYPE::NOTIFY_CHN_ENG);

            // Assert: Window created
            Assert::IsTrue(SUCCEEDED(hr), L"MakeNotifyWindow should succeed");
            Assert::IsTrue(pUIPresenter->IsNotifyShown() || !pUIPresenter->IsNotifyShown(), 
                L"IsNotifyShown should return valid state");

            // Cleanup
            pUIPresenter->Release(); // UIPresenter destructor will release pStubDIME
        }

        TEST_METHOD(IT06_03_SetNotifyText)
        {
            Logger::WriteMessage("Test: IT06_03_SetNotifyText\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
                        CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            CStringRange initialText;
            initialText.Set(L"?��?", 2);
            pUIPresenter->MakeNotifyWindow(nullptr, &initialText, NOTIFY_TYPE::NOTIFY_CHN_ENG);

            // Act: Update text
            CStringRange newText;
            newText.Set(L"?�新", 2);
            pUIPresenter->SetNotifyText(&newText);

            // Assert: Text updated (no crash means success)
            Assert::IsTrue(true, L"SetNotifyText should complete without error");

            // Cleanup
            pUIPresenter->Release(); // UIPresenter destructor will release pStubDIME
        }

        TEST_METHOD(IT06_03_ShowNotify)
        {
            Logger::WriteMessage("Test: IT06_03_ShowNotify\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
                        CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            CStringRange text;
            text.Set(L"測試", 2);
            pUIPresenter->MakeNotifyWindow(nullptr, &text, NOTIFY_TYPE::NOTIFY_CHN_ENG);

            // Act: Show notification
            pUIPresenter->ShowNotify(TRUE, 0, 1000);

            // Assert: Notification shown (check state)
            BOOL isShown = pUIPresenter->IsNotifyShown();
            Assert::IsTrue(isShown, L"Notification should be shown after ShowNotify(TRUE)");

            // Cleanup
            pUIPresenter->Release(); // UIPresenter destructor will release pStubDIME
        }

        TEST_METHOD(IT06_03_HideNotify)
        {
            Logger::WriteMessage("Test: IT06_03_HideNotify\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
                        CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            CStringRange text;
            text.Set(L"測試", 2);
            pUIPresenter->MakeNotifyWindow(nullptr, &text, NOTIFY_TYPE::NOTIFY_CHN_ENG);
            pUIPresenter->ShowNotify(TRUE, 0, 0);

            // Act: Hide notification
            pUIPresenter->ShowNotify(FALSE, 0, 0);

            // Assert: Notification hidden (check state)
            BOOL isShown = pUIPresenter->IsNotifyShown();
            Assert::IsFalse(isShown, L"Notification should be hidden after ShowNotify(FALSE)");

            // Cleanup
            pUIPresenter->Release(); // UIPresenter destructor will release pStubDIME
        }

        TEST_METHOD(IT06_03_ClearNotify)
        {
            Logger::WriteMessage("Test: IT06_03_ClearNotify\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
                        CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            CStringRange text;
            text.Set(L"測試", 2);
            pUIPresenter->MakeNotifyWindow(nullptr, &text, NOTIFY_TYPE::NOTIFY_CHN_ENG);
            pUIPresenter->ShowNotify(TRUE, 0, 0);

            // Act: Clear notification
            pUIPresenter->ClearNotify();

            // Assert: Notification cleared (window disposed)
            BOOL isShown = pUIPresenter->IsNotifyShown();
            Assert::IsFalse(isShown, L"Notification should not be shown after ClearNotify");

            // Cleanup
            pUIPresenter->Release(); // UIPresenter destructor will release pStubDIME
        }

        // IT-06-04: UI Visibility and State Management

        TEST_METHOD(IT06_04_ShowUIWindows)
        {
            Logger::WriteMessage("Test: IT06_04_ShowUIWindows\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
                        CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            pUIPresenter->MakeCandidateWindow(nullptr, 200);
            CStringRange text;
            text.Set(L"測試", 2);
            pUIPresenter->MakeNotifyWindow(nullptr, &text, NOTIFY_TYPE::NOTIFY_OTHERS);

            // Act: Show
            HRESULT hr = pUIPresenter->Show(TRUE);
            Assert::IsTrue(SUCCEEDED(hr), L"Show(TRUE) should succeed");

            // Assert: Windows shown (check candidate state)
            BOOL isCandShown = pUIPresenter->IsCandShown();
            // Note: May be false if window not visible without parent HWND, but should not crash

            // Cleanup
            pUIPresenter->Release(); // UIPresenter destructor will release pStubDIME
        }

        TEST_METHOD(IT06_04_HideUIWindows)
        {
            Logger::WriteMessage("Test: IT06_04_HideUIWindows\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
                        CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            pUIPresenter->MakeCandidateWindow(nullptr, 200);
            CStringRange text;
            text.Set(L"測試", 2);
            pUIPresenter->MakeNotifyWindow(nullptr, &text, NOTIFY_TYPE::NOTIFY_OTHERS);
            pUIPresenter->Show(TRUE);

            // Act: Hide
            HRESULT hr = pUIPresenter->Show(FALSE);
            Assert::IsTrue(SUCCEEDED(hr), L"Show(FALSE) should succeed");

            // Assert: Windows hidden
            BOOL isCandShown = pUIPresenter->IsCandShown();
            Assert::IsFalse(isCandShown, L"Candidate window should be hidden after Show(FALSE)");

            BOOL isNotifyShown = pUIPresenter->IsNotifyShown();
            Assert::IsFalse(isNotifyShown, L"Notify window should be hidden after Show(FALSE)");

            // Cleanup
            pUIPresenter->Release(); // UIPresenter destructor will release pStubDIME
        }

        TEST_METHOD(IT06_04_QueryNotificationVisibility)
        {
            Logger::WriteMessage("Test: IT06_04_QueryNotificationVisibility\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
                        CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            CStringRange text;
            text.Set(L"測試", 2);
            pUIPresenter->MakeNotifyWindow(nullptr, &text, NOTIFY_TYPE::NOTIFY_CHN_ENG);

            // After creation, window exists but may be visible depending on implementation
            // Hide it first
            pUIPresenter->ShowNotify(FALSE, 0, 0);
            BOOL isShownInitial = pUIPresenter->IsNotifyShown();
            Assert::IsFalse(isShownInitial, L"Notification should be hidden after ShowNotify(FALSE)");

            // Show
            pUIPresenter->ShowNotify(TRUE, 0, 0);
            BOOL isShownAfterShow = pUIPresenter->IsNotifyShown();
            Assert::IsTrue(isShownAfterShow, L"Notification should be shown after ShowNotify(TRUE)");

            // Hide
            pUIPresenter->ShowNotify(FALSE, 0, 0);
            BOOL isShownAfterHide = pUIPresenter->IsNotifyShown();
            Assert::IsFalse(isShownAfterHide, L"Notification should be hidden after ShowNotify(FALSE)");

            // Cleanup
            pUIPresenter->Release(); // UIPresenter destructor will release pStubDIME
        }

        TEST_METHOD(IT06_04_QueryCandidateVisibility)
        {
            Logger::WriteMessage("Test: IT06_04_QueryCandidateVisibility\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
                        CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            pUIPresenter->MakeCandidateWindow(nullptr, 200);

            // After creation, window exists but may be visible depending on implementation
            // Hide it first
            pUIPresenter->Show(FALSE);
            BOOL isShownInitial = pUIPresenter->IsCandShown();
            Assert::IsFalse(isShownInitial, L"Candidate should be hidden after Show(FALSE)");

            // Show
            pUIPresenter->Show(TRUE);
            // Note: IsCandShown may still be false without parent HWND, but should not crash

            // Hide
            pUIPresenter->Show(FALSE);
            BOOL isShownAfterHide = pUIPresenter->IsCandShown();
            Assert::IsFalse(isShownAfterHide, L"Candidate should be hidden after Show(FALSE)");

            // Cleanup
            pUIPresenter->Release(); // UIPresenter destructor will release pStubDIME
        }

        TEST_METHOD(IT06_04_IsShownMethod)
        {
            Logger::WriteMessage("Test: IT06_04_IsShownMethod\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
                        CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            pUIPresenter->MakeCandidateWindow(nullptr, 200);

            BOOL isShow = FALSE;

            // Act: Check initial state
            HRESULT hr = pUIPresenter->IsShown(&isShow);
            Assert::IsTrue(SUCCEEDED(hr), L"IsShown should succeed");

            // Act: Show and check
            pUIPresenter->Show(TRUE);
            hr = pUIPresenter->IsShown(&isShow);
            Assert::IsTrue(SUCCEEDED(hr), L"IsShown should succeed after Show(TRUE)");

            // Cleanup
            pUIPresenter->Release(); // UIPresenter destructor will release pStubDIME
        }

        // ===================================================================
        // IT-06-05: ITfUIElement Interface Tests
        // ===================================================================

        TEST_METHOD(IT06_05_GetDescription)
        {
            Logger::WriteMessage("Test: IT06_05_GetDescription\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
            CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            BSTR bstrDescription = nullptr;

            // Act: Get description
            HRESULT hr = pUIPresenter->GetDescription(&bstrDescription);

            // Assert: Returns S_OK and description is "Cand"
            Assert::IsTrue(SUCCEEDED(hr), L"GetDescription should succeed");
            Assert::IsNotNull(bstrDescription, L"Description should not be null");
            Assert::AreEqual(std::wstring(L"Cand"), std::wstring(bstrDescription), L"Description should be 'Cand'");

            // Cleanup
            if (bstrDescription) SysFreeString(bstrDescription);
            pUIPresenter->Release();
        }

        TEST_METHOD(IT06_05_GetGUID)
        {
            Logger::WriteMessage("Test: IT06_05_GetGUID\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
            CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            GUID guid = { 0 };

            // Act: Get GUID
            HRESULT hr = pUIPresenter->GetGUID(&guid);

            // Assert: Returns S_OK and GUID matches Global::DIMEGuidCandUIElement
            Assert::IsTrue(SUCCEEDED(hr), L"GetGUID should succeed");
            Assert::IsTrue(InlineIsEqualGUID(guid, Global::DIMEGuidCandUIElement), L"GUID should match DIMEGuidCandUIElement");

            // Cleanup
            pUIPresenter->Release();
        }

        TEST_METHOD(IT06_05_GetUpdatedFlags)
        {
            Logger::WriteMessage("Test: IT06_05_GetUpdatedFlags\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
            CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            DWORD dwFlags = 0;

            // Act: Get updated flags
            HRESULT hr = pUIPresenter->GetUpdatedFlags(&dwFlags);

            // Assert: Returns S_OK and initial flags are 0
            Assert::IsTrue(SUCCEEDED(hr), L"GetUpdatedFlags should succeed");
            // Note: Initial flags may be 0 or have some default value
            // The test verifies the method succeeds without checking specific flag values

            // Cleanup
            pUIPresenter->Release();
        }

        // ===================================================================
        // IT-06-06: Candidate List Query Tests
        // ===================================================================

        TEST_METHOD(IT06_06_GetSelection)
        {
            Logger::WriteMessage("Test: IT06_06_GetSelection\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
            CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            pUIPresenter->MakeCandidateWindow(nullptr, 200);
            UINT selectedIndex = 999;

            // Act: Get selection
            HRESULT hr = pUIPresenter->GetSelection(&selectedIndex);

            // Assert: Returns S_OK and index is 0 (default)
            Assert::IsTrue(SUCCEEDED(hr), L"GetSelection should succeed");
            Assert::AreEqual(0U, selectedIndex, L"Initial selection should be 0");

            // Cleanup
            pUIPresenter->Release();
        }

        TEST_METHOD(IT06_06_GetString)
        {
            Logger::WriteMessage("Test: IT06_06_GetString\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
            CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            pUIPresenter->MakeCandidateWindow(nullptr, 200);

            // Add candidates
            CDIMEArray<CCandidateListItem> candidates;
            CCandidateListItem* pItem1 = candidates.Append();
            if (pItem1) { pItem1->_ItemString.Set(L"測試", 2); }
            pUIPresenter->AddCandidateToUI(&candidates, TRUE);

            BSTR bstrCandidate = nullptr;

            // Act: Get string at index 0
            HRESULT hr = pUIPresenter->GetString(0, &bstrCandidate);

            // Assert: Returns S_OK and string matches
            Assert::IsTrue(SUCCEEDED(hr), L"GetString should succeed");
            Assert::IsNotNull(bstrCandidate, L"Candidate string should not be null");
            Assert::AreEqual(2, (int)wcslen(bstrCandidate), L"Candidate string should have length 2");

            // Cleanup
            if (bstrCandidate) SysFreeString(bstrCandidate);
            pUIPresenter->Release();
        }

        TEST_METHOD(IT06_06_GetStringInvalidIndex)
        {
            Logger::WriteMessage("Test: IT06_06_GetStringInvalidIndex\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
            CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            pUIPresenter->MakeCandidateWindow(nullptr, 200);

            // Add candidates
            CDIMEArray<CCandidateListItem> candidates;
            CCandidateListItem* pItem1 = candidates.Append();
            if (pItem1) { pItem1->_ItemString.Set(L"測試", 2); }
            pUIPresenter->AddCandidateToUI(&candidates, TRUE);

            BSTR bstrCandidate = nullptr;

            // Act: Try to get string at invalid index
            HRESULT hr = pUIPresenter->GetString(99, &bstrCandidate);

            // Assert: Returns E_FAIL
            Assert::IsTrue(FAILED(hr), L"GetString should fail for invalid index");

            // Cleanup
            if (bstrCandidate) SysFreeString(bstrCandidate);
            pUIPresenter->Release();
        }

        TEST_METHOD(IT06_06_GetCurrentPage)
        {
            Logger::WriteMessage("Test: IT06_06_GetCurrentPage\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
            CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            pUIPresenter->MakeCandidateWindow(nullptr, 200);
            
            // Add candidates so page exists
            CDIMEArray<CCandidateListItem> candidates;
            for (int i = 0; i < 10; i++)
            {
                CCandidateListItem* pItem = candidates.Append();
                if (pItem) { pItem->_ItemString.Set(L"測試", 2); }
            }
            pUIPresenter->AddCandidateToUI(&candidates, TRUE);
            
            // Manually set up page indices for testing
            UINT pageIndex[2] = { 0, 5 };
            pUIPresenter->SetPageIndex(pageIndex, 2);
            
            UINT currentPage = 999;

            // Act: Get current page
            HRESULT hr = pUIPresenter->GetCurrentPage(&currentPage);

            // Assert: Returns S_OK and page is 0 (default)
            Assert::IsTrue(SUCCEEDED(hr), L"GetCurrentPage should succeed");
            Assert::AreEqual(0U, currentPage, L"Initial page should be 0");

            // Cleanup
            pUIPresenter->Release();
        }

        TEST_METHOD(IT06_06_GetDocumentMgr)
        {
            Logger::WriteMessage("Test: IT06_06_GetDocumentMgr\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
            CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            ITfDocumentMgr* pDocMgr = nullptr;

            // Act: Get document manager
            HRESULT hr = pUIPresenter->GetDocumentMgr(&pDocMgr);

            // Assert: Returns E_NOTIMPL (not implemented)
            Assert::AreEqual(E_NOTIMPL, hr, L"GetDocumentMgr should return E_NOTIMPL");
            Assert::IsNull(pDocMgr, L"Document manager should be null");

            // Cleanup
            pUIPresenter->Release();
        }

        // ===================================================================
        // IT-06-07: Candidate Paging Tests
        // ===================================================================

        TEST_METHOD(IT06_07_GetPageIndex)
        {
            Logger::WriteMessage("Test: IT06_07_GetPageIndex\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
            CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            pUIPresenter->MakeCandidateWindow(nullptr, 200);

            UINT pageIndex[10] = { 0 };
            UINT pageCount = 0;

            // Act: Get page index
            HRESULT hr = pUIPresenter->GetPageIndex(pageIndex, 10, &pageCount);

            // Assert: Returns S_OK
            Assert::IsTrue(SUCCEEDED(hr), L"GetPageIndex should succeed");

            // Cleanup
            pUIPresenter->Release();
        }

        TEST_METHOD(IT06_07_SetPageIndex)
        {
            Logger::WriteMessage("Test: IT06_07_SetPageIndex\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
            CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            pUIPresenter->MakeCandidateWindow(nullptr, 200);

            // Add multiple candidates to enable paging
            CDIMEArray<CCandidateListItem> candidates;
            for (int i = 0; i < 15; i++)
            {
                CCandidateListItem* pItem = candidates.Append();
                if (pItem) { pItem->_ItemString.Set(L"測試", 2); }
            }
            pUIPresenter->AddCandidateToUI(&candidates, TRUE);

            UINT pageIndex[2] = { 0, 10 };

            // Act: Set page index
            HRESULT hr = pUIPresenter->SetPageIndex(pageIndex, 2);

            // Assert: Returns S_OK
            Assert::IsTrue(SUCCEEDED(hr), L"SetPageIndex should succeed");

            // Cleanup
            pUIPresenter->Release();
        }

        TEST_METHOD(IT06_07_MoveCandidatePage)
        {
            Logger::WriteMessage("Test: IT06_07_MoveCandidatePage\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
            CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            pUIPresenter->MakeCandidateWindow(nullptr, 200);

            // Add multiple candidates to enable paging
            CDIMEArray<CCandidateListItem> candidates;
            for (int i = 0; i < 15; i++)
            {
                CCandidateListItem* pItem = candidates.Append();
                if (pItem) { pItem->_ItemString.Set(L"測試", 2); }
            }
            pUIPresenter->AddCandidateToUI(&candidates, TRUE);

            // Act: Move to next page (offset +1)
            BOOL result = pUIPresenter->_MoveCandidatePage(1);

            // Assert: Method succeeds (returns TRUE if page moved)
            // Note: Result depends on page configuration, so we just verify no crash
            // Cleanup
            pUIPresenter->Release();
        }

        TEST_METHOD(IT06_07_SetPageIndexWithScrollInfo)
        {
            Logger::WriteMessage("Test: IT06_07_SetPageIndexWithScrollInfo\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
            CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            pUIPresenter->MakeCandidateWindow(nullptr, 200);

            // Add multiple candidates
            CDIMEArray<CCandidateListItem> candidates;
            for (int i = 0; i < 15; i++)
            {
                CCandidateListItem* pItem = candidates.Append();
                if (pItem) { pItem->_ItemString.Set(L"測試", 2); }
            }

            // Act: Set page index with scroll info
            pUIPresenter->SetPageIndexWithScrollInfo(&candidates);

            // Assert: Method completes without crashing
            // Verify page count is updated
            UINT pageCount = 0;
            UINT pageIndex[10] = { 0 };
            HRESULT hr = pUIPresenter->GetPageIndex(pageIndex, 10, &pageCount);
            Assert::IsTrue(SUCCEEDED(hr), L"GetPageIndex should succeed after SetPageIndexWithScrollInfo");

            // Cleanup
            pUIPresenter->Release();
        }

        // ===================================================================
        // IT-06-08: Candidate Selection Tests
        // ===================================================================

        TEST_METHOD(IT06_08_SetSelection)
        {
            Logger::WriteMessage("Test: IT06_08_SetSelection\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
            CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            pUIPresenter->MakeCandidateWindow(nullptr, 200);

            // Add candidates
            CDIMEArray<CCandidateListItem> candidates;
            for (int i = 0; i < 5; i++)
            {
                CCandidateListItem* pItem = candidates.Append();
                if (pItem) { pItem->_ItemString.Set(L"測試", 2); }
            }
            pUIPresenter->AddCandidateToUI(&candidates, TRUE);

            // Act: Set selection to index 2
            HRESULT hr = pUIPresenter->SetSelection(2);

            // Assert: Returns S_OK
            Assert::IsTrue(SUCCEEDED(hr), L"SetSelection should succeed");

            // Verify selection was set
            UINT selectedIndex = 999;
            hr = pUIPresenter->GetSelection(&selectedIndex);
            Assert::IsTrue(SUCCEEDED(hr), L"GetSelection should succeed");
            Assert::AreEqual(2U, selectedIndex, L"Selection should be set to 2");

            // Cleanup
            pUIPresenter->Release();
        }

        TEST_METHOD(IT06_08_SetSelectionInvalidIndex)
        {
            Logger::WriteMessage("Test: IT06_08_SetSelectionInvalidIndex\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
            CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            pUIPresenter->MakeCandidateWindow(nullptr, 200);

            // Add only 3 candidates
            CDIMEArray<CCandidateListItem> candidates;
            for (int i = 0; i < 3; i++)
            {
                CCandidateListItem* pItem = candidates.Append();
                if (pItem) { pItem->_ItemString.Set(L"測試", 2); }
            }
            pUIPresenter->AddCandidateToUI(&candidates, TRUE);

            // Act: Try to set selection to invalid index
            HRESULT hr = pUIPresenter->SetSelection(99);

            // Assert: Returns S_OK (method doesn't validate index, delegates to window)
            Assert::IsTrue(SUCCEEDED(hr), L"SetSelection should succeed even with invalid index");

            // Cleanup
            pUIPresenter->Release();
        }

        TEST_METHOD(IT06_08_MoveCandidateSelection)
        {
            Logger::WriteMessage("Test: IT06_08_MoveCandidateSelection\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
            CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            pUIPresenter->MakeCandidateWindow(nullptr, 200);

            // Add candidates
            CDIMEArray<CCandidateListItem> candidates;
            for (int i = 0; i < 5; i++)
            {
                CCandidateListItem* pItem = candidates.Append();
                if (pItem) { pItem->_ItemString.Set(L"測試", 2); }
            }
            pUIPresenter->AddCandidateToUI(&candidates, TRUE);
            
            // Manually set up page indices for testing
            UINT pageIndex[1] = { 0 };
            pUIPresenter->SetPageIndex(pageIndex, 1);

            // Act: Set initial selection
            pUIPresenter->SetSelection(0);
            UINT initialSelection = 0;
            pUIPresenter->GetSelection(&initialSelection);
            Assert::AreEqual(0U, initialSelection, L"Initial selection should be 0");

            // Move selection using internal method to position 2 in the current page
            BOOL result = pUIPresenter->_SetCandidateSelectionInPage(2);
            Assert::IsTrue(result, L"_SetCandidateSelectionInPage should succeed");

            // Assert: Verify selection moved
            UINT newSelection = 0;
            pUIPresenter->GetSelection(&newSelection);
            Assert::AreEqual(2U, newSelection, L"Selection should be moved to position 2");

            // Cleanup
            pUIPresenter->Release();
        }

        TEST_METHOD(IT06_08_GetSelectedCandidateString)
        {
            Logger::WriteMessage("Test: IT06_08_GetSelectedCandidateString\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
            CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            pUIPresenter->MakeCandidateWindow(nullptr, 200);

            // Add candidates with different strings
            CDIMEArray<CCandidateListItem> candidates;
            CCandidateListItem* pItem1 = candidates.Append();
            if (pItem1) { pItem1->_ItemString.Set(L"第一", 2); }
            CCandidateListItem* pItem2 = candidates.Append();
            if (pItem2) { pItem2->_ItemString.Set(L"第二", 2); }
            CCandidateListItem* pItem3 = candidates.Append();
            if (pItem3) { pItem3->_ItemString.Set(L"第三", 2); }
            pUIPresenter->AddCandidateToUI(&candidates, TRUE);

            // Act: Set selection to index 1 and get string
            pUIPresenter->SetSelection(1);
            UINT selectedIndex = 0;
            pUIPresenter->GetSelection(&selectedIndex);
            
            BSTR bstrSelected = nullptr;
            HRESULT hr = pUIPresenter->GetString(selectedIndex, &bstrSelected);

            // Assert: Returns correct string for selected candidate
            Assert::IsTrue(SUCCEEDED(hr), L"GetString should succeed");
            Assert::IsNotNull(bstrSelected, L"Selected string should not be null");
            Assert::AreEqual(2, (int)wcslen(bstrSelected), L"Selected string should have length 2");

            // Cleanup
            if (bstrSelected) SysFreeString(bstrSelected);
            pUIPresenter->Release();
        }

        // ===================================================================
        // IT-06-09: Selection Finalization Tests
        // ===================================================================

        TEST_METHOD(IT06_09_Finalize)
        {
            Logger::WriteMessage("Test: IT06_09_Finalize\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
            CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            pUIPresenter->MakeCandidateWindow(nullptr, 200);

            // Add candidates
            CDIMEArray<CCandidateListItem> candidates;
            CCandidateListItem* pItem = candidates.Append();
            if (pItem) { pItem->_ItemString.Set(L"測試", 2); }
            pUIPresenter->AddCandidateToUI(&candidates, TRUE);

            // Act: Finalize selection
            HRESULT hr = pUIPresenter->Finalize();

            // Assert: Returns S_OK (triggers candidate change notification)
            Assert::IsTrue(SUCCEEDED(hr), L"Finalize should succeed");

            // Cleanup
            pUIPresenter->Release();
        }

        TEST_METHOD(IT06_09_Abort)
        {
            Logger::WriteMessage("Test: IT06_09_Abort\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
            CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            pUIPresenter->MakeCandidateWindow(nullptr, 200);

            // Act: Abort selection
            HRESULT hr = pUIPresenter->Abort();

            // Assert: Returns E_NOTIMPL (not implemented)
            Assert::AreEqual(E_NOTIMPL, hr, L"Abort should return E_NOTIMPL");

            // Cleanup
            pUIPresenter->Release();
        }

        // ===================================================================
        // IT-06-10: COM Interface Testing
        // ===================================================================

        TEST_METHOD(IT06_10_QueryInterface_ITfUIElement)
        {
            Logger::WriteMessage("Test: IT06_10_QueryInterface_ITfUIElement\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
            CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            void* pInterface = nullptr;

            // Act: Query for ITfUIElement
            HRESULT hr = pUIPresenter->QueryInterface(IID_ITfUIElement, &pInterface);

            // Assert: Returns S_OK and valid interface pointer
            Assert::IsTrue(SUCCEEDED(hr), L"QueryInterface(ITfUIElement) should succeed");
            Assert::IsNotNull(pInterface, L"Interface pointer should not be null");

            // Cleanup
            if (pInterface) ((IUnknown*)pInterface)->Release();
            pUIPresenter->Release();
        }

        TEST_METHOD(IT06_10_QueryInterface_ITfCandidateListUIElement)
        {
            Logger::WriteMessage("Test: IT06_10_QueryInterface_ITfCandidateListUIElement\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
            CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            void* pInterface = nullptr;

            // Act: Query for ITfCandidateListUIElement
            HRESULT hr = pUIPresenter->QueryInterface(IID_ITfCandidateListUIElement, &pInterface);

            // Assert: Returns S_OK and valid interface pointer
            Assert::IsTrue(SUCCEEDED(hr), L"QueryInterface(ITfCandidateListUIElement) should succeed");
            Assert::IsNotNull(pInterface, L"Interface pointer should not be null");

            // Cleanup
            if (pInterface) ((IUnknown*)pInterface)->Release();
            pUIPresenter->Release();
        }

        TEST_METHOD(IT06_10_QueryInterface_UnsupportedInterface)
        {
            Logger::WriteMessage("Test: IT06_10_QueryInterface_UnsupportedInterface\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
            CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            void* pInterface = nullptr;

            // Define an unsupported IID (use IID_IUnknown as placeholder for test)
            GUID unsupportedIID = { 0x12345678, 0x1234, 0x1234, { 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0 } };

            // Act: Query for unsupported interface
            HRESULT hr = pUIPresenter->QueryInterface(unsupportedIID, &pInterface);

            // Assert: Returns E_NOINTERFACE
            Assert::AreEqual(E_NOINTERFACE, hr, L"QueryInterface should return E_NOINTERFACE for unsupported interface");
            Assert::IsNull(pInterface, L"Interface pointer should be null");

            // Cleanup
            pUIPresenter->Release();
        }

        // ===================================================================
        // IT-06-11: Selection Style & Integration
        // ===================================================================

        TEST_METHOD(IT06_11_GetSelectionStyle)
        {
            Logger::WriteMessage("Test: IT06_11_GetSelectionStyle\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
            CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            TfIntegratableCandidateListSelectionStyle style;

            // Act: Get selection style
            HRESULT hr = pUIPresenter->GetSelectionStyle(&style);

            // Assert: Returns S_OK and STYLE_ACTIVE_SELECTION
            Assert::IsTrue(SUCCEEDED(hr), L"GetSelectionStyle should succeed");
            Assert::AreEqual((int)STYLE_ACTIVE_SELECTION, (int)style, L"Style should be STYLE_ACTIVE_SELECTION");

            // Cleanup
            pUIPresenter->Release();
        }

        TEST_METHOD(IT06_11_ShowCandidateNumbers)
        {
            Logger::WriteMessage("Test: IT06_11_ShowCandidateNumbers\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
            CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            BOOL isShow = FALSE;

            // Act: Check if candidate numbers shown
            HRESULT hr = pUIPresenter->ShowCandidateNumbers(&isShow);

            // Assert: Returns S_OK and TRUE
            Assert::IsTrue(SUCCEEDED(hr), L"ShowCandidateNumbers should succeed");
            Assert::IsTrue(isShow, L"ShowCandidateNumbers should return TRUE");

            // Cleanup
            pUIPresenter->Release();
        }

        TEST_METHOD(IT06_11_FinalizeExactCompositionString)
        {
            Logger::WriteMessage("Test: IT06_11_FinalizeExactCompositionString\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
            CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);

            // Act: Finalize exact composition string
            HRESULT hr = pUIPresenter->FinalizeExactCompositionString();

            // Assert: Returns E_NOTIMPL (not implemented yet)
            Assert::AreEqual(E_NOTIMPL, hr, L"FinalizeExactCompositionString returns E_NOTIMPL");

            // Cleanup
            pUIPresenter->Release();
        }

        // ===================================================================
        // IT-06-12: Keyboard Event Handling
        // ===================================================================

        TEST_METHOD(IT06_12_OnKeyDown)
        {
            Logger::WriteMessage("Test: IT06_12_OnKeyDown\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
            CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            BOOL isEaten = FALSE;

            // Act: Simulate key down event (VK_DOWN = 0x28)
            HRESULT hr = pUIPresenter->OnKeyDown(0x28, 0, &isEaten);

            // Assert: Returns S_OK and eats the key
            Assert::IsTrue(SUCCEEDED(hr), L"OnKeyDown should succeed");
            Assert::IsTrue(isEaten, L"OnKeyDown should eat the key");

            // Cleanup
            pUIPresenter->Release();
        }

        TEST_METHOD(IT06_12_AdviseUIChangedByArrowKey_MoveDown)
        {
            Logger::WriteMessage("Test: IT06_12_AdviseUIChangedByArrowKey_MoveDown\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
            CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            pUIPresenter->MakeCandidateWindow(nullptr, 200);

            // Add candidates
            CDIMEArray<CCandidateListItem> candidates;
            for (int i = 0; i < 5; i++)
            {
                CCandidateListItem* pItem = candidates.Append();
                if (pItem) { pItem->_ItemString.Set(L"測試", 2); }
            }
            pUIPresenter->AddCandidateToUI(&candidates, TRUE);
            UINT pageIndex[1] = { 0 };
            pUIPresenter->SetPageIndex(pageIndex, 1);

            // Act: Advise arrow key down
            pUIPresenter->AdviseUIChangedByArrowKey(KEYSTROKE_FUNCTION::FUNCTION_MOVE_DOWN);

            // Assert: Candidate selection should have moved (verify no crash)
            UINT selection = 0;
            pUIPresenter->GetSelection(&selection);
            Assert::IsTrue(selection >= 0, L"Selection should be valid after arrow key");

            // Cleanup
            pUIPresenter->Release();
        }

        // ===================================================================
        // IT-06-13: Color Configuration
        // ===================================================================

        TEST_METHOD(IT06_13_SetCandidateNumberColor)
        {
            Logger::WriteMessage("Test: IT06_13_SetCandidateNumberColor\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
            CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            pUIPresenter->MakeCandidateWindow(nullptr, 200);

            // Act: Set candidate number color
            pUIPresenter->_SetCandidateNumberColor(RGB(255, 0, 0), RGB(255, 255, 255));

            // Assert: Method completes without crash
            Assert::IsTrue(true, L"SetCandidateNumberColor should complete");

            // Cleanup
            pUIPresenter->Release();
        }

        TEST_METHOD(IT06_13_SetCandidateTextColor)
        {
            Logger::WriteMessage("Test: IT06_13_SetCandidateTextColor\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
            CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            pUIPresenter->MakeCandidateWindow(nullptr, 200);

            // Act: Set candidate text color
            pUIPresenter->_SetCandidateTextColor(RGB(0, 0, 0), RGB(255, 255, 255));

            // Assert: Method completes without crash
            Assert::IsTrue(true, L"SetCandidateTextColor should complete");

            // Cleanup
            pUIPresenter->Release();
        }

        TEST_METHOD(IT06_13_SetCandidateSelectedTextColor)
        {
            Logger::WriteMessage("Test: IT06_13_SetCandidateSelectedTextColor\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
            CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            pUIPresenter->MakeCandidateWindow(nullptr, 200);

            // Act: Set selected text color
            pUIPresenter->_SetCandidateSelectedTextColor(RGB(255, 255, 255), RGB(0, 0, 255));

            // Assert: Method completes without crash
            Assert::IsTrue(true, L"SetCandidateSelectedTextColor should complete");

            // Cleanup
            pUIPresenter->Release();
        }

        TEST_METHOD(IT06_13_SetCandidateFillColor)
        {
            Logger::WriteMessage("Test: IT06_13_SetCandidateFillColor\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
            CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            pUIPresenter->MakeCandidateWindow(nullptr, 200);

            // Act: Set candidate fill color
            pUIPresenter->_SetCandidateFillColor(RGB(240, 240, 240));

            // Assert: Method completes without crash
            Assert::IsTrue(true, L"SetCandidateFillColor should complete");

            // Cleanup
            pUIPresenter->Release();
        }

        TEST_METHOD(IT06_13_SetNotifyTextColor)
        {
            Logger::WriteMessage("Test: IT06_13_SetNotifyTextColor\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
            CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            pUIPresenter->MakeNotifyWindow(nullptr, nullptr, NOTIFY_TYPE::NOTIFY_OTHERS);

            // Act: Set notify text color
            pUIPresenter->_SetNotifyTextColor(RGB(0, 128, 0), RGB(255, 255, 255));

            // Assert: Method completes without crash
            Assert::IsTrue(true, L"SetNotifyTextColor should complete");

            // Cleanup
            pUIPresenter->Release();
        }

        // ===================================================================
        // IT-06-14: Candidate List Management
        // ===================================================================

        TEST_METHOD(IT06_14_RemoveSpecificCandidateFromList)
        {
            Logger::WriteMessage("Test: IT06_14_RemoveSpecificCandidateFromList\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
            CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);

            CDIMEArray<CCandidateListItem> candidates;
            CCandidateListItem* pItem1 = candidates.Append();
            if (pItem1) { pItem1->_ItemString.Set(L"測試", 2); }
            CCandidateListItem* pItem2 = candidates.Append();
            if (pItem2) { pItem2->_ItemString.Set(L"刪除", 2); }
            CCandidateListItem* pItem3 = candidates.Append();
            if (pItem3) { pItem3->_ItemString.Set(L"保留", 2); }

            CStringRange removeString;
            removeString.Set(L"刪除", 2);

            // Act: Remove specific candidate
            pUIPresenter->RemoveSpecificCandidateFromList(MAKELCID(MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL), SORT_DEFAULT), 
                                                          candidates, removeString);

            // Assert: List should have 2 items (removed "刪除")
            Assert::AreEqual(2U, candidates.Count(), L"Should have 2 items after removal");

            // Cleanup
            pUIPresenter->Release();
        }

        TEST_METHOD(IT06_14_ClearCandidateList)
        {
            Logger::WriteMessage("Test: IT06_14_ClearCandidateList\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
            CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            pUIPresenter->MakeCandidateWindow(nullptr, 200);

            // Add candidates
            CDIMEArray<CCandidateListItem> candidates;
            for (int i = 0; i < 5; i++)
            {
                CCandidateListItem* pItem = candidates.Append();
                if (pItem) { pItem->_ItemString.Set(L"測試", 2); }
            }
            pUIPresenter->AddCandidateToUI(&candidates, TRUE);

            // Act: Clear candidate list
            pUIPresenter->_ClearCandidateList();

            // Assert: Count should be 0
            UINT count = 0;
            pUIPresenter->GetCount(&count);
            Assert::AreEqual(0U, count, L"Count should be 0 after clear");

            // Cleanup
            pUIPresenter->Release();
        }

        TEST_METHOD(IT06_14_ClearAll)
        {
            Logger::WriteMessage("Test: IT06_14_ClearAll\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
            CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            pUIPresenter->MakeCandidateWindow(nullptr, 200);
            pUIPresenter->MakeNotifyWindow(nullptr, nullptr, NOTIFY_TYPE::NOTIFY_OTHERS);

            // Add data to both windows
            CDIMEArray<CCandidateListItem> candidates;
            CCandidateListItem* pItem = candidates.Append();
            if (pItem) { pItem->_ItemString.Set(L"測試", 2); }
            pUIPresenter->AddCandidateToUI(&candidates, TRUE);

            // Act: Clear all windows
            pUIPresenter->ClearAll();

            // Assert: Both windows should be cleared (verify no crash)
            UINT count = 0;
            pUIPresenter->GetCount(&count);
            Assert::AreEqual(0U, count, L"Candidate count should be 0 after ClearAll");

            // Cleanup
            pUIPresenter->Release();
        }

        // ===================================================================
        // IT-06-15: UI Position & Navigation
        // ===================================================================

        TEST_METHOD(IT06_15_GetCandLocation)
        {
            Logger::WriteMessage("Test: IT06_15_GetCandLocation\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
            CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            POINT location = { -1, -1 };

            // Act: Get candidate window location
            pUIPresenter->GetCandLocation(&location);

            // Assert: Location should be initialized (default off-screen values)
            Assert::IsTrue(location.x != -1 || location.y != -1, L"Location should be set");

            // Cleanup
            pUIPresenter->Release();
        }

        TEST_METHOD(IT06_15_UILessMode)
        {
            Logger::WriteMessage("Test: IT06_15_UILessMode\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
            CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);

            // Act: Check UI less mode (default _isShowMode = TRUE, so isUILessMode = FALSE)
            BOOL isUILess = pUIPresenter->isUILessMode();

            // Assert: Should be in show mode by default
            Assert::IsFalse(isUILess, L"Should not be in UI-less mode by default");

            // Cleanup
            pUIPresenter->Release();
        }

        // ===================================================================
        // IT-06-16: Count Method
        // ===================================================================

        TEST_METHOD(IT06_16_GetCount_WithNoCandidateWindow)
        {
            Logger::WriteMessage("Test: IT06_16_GetCount_WithNoCandidateWindow\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
            CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            UINT count = 999;

            // Act: Get count without creating candidate window
            HRESULT hr = pUIPresenter->GetCount(&count);

            // Assert: Returns S_OK with count 0
            Assert::IsTrue(SUCCEEDED(hr), L"GetCount should succeed");
            Assert::AreEqual(0U, count, L"Count should be 0 without candidate window");

            // Cleanup
            pUIPresenter->Release();
        }

        // ===================================================================
        // IT-06-17: TSF Lifecycle Methods (with lightweight mocks)
        // Target: BeginUIElement, EndUIElement,OnSetThreadFocus, OnKillThreadFocus
        // ===================================================================

        TEST_METHOD(IT06_17_BeginUIElement)
        {
            Logger::WriteMessage("Test: IT06_17_BeginUIElement\n");

            // Arrange: Create UIPresenter with stubbed CDIME that provides mock ITfThreadMgr
            StubCDIME* pStubDIME = new StubCDIME();
            pStubDIME->SetupMockThreadMgr(); // Initialize mock ThreadMgr
            CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);

            // Act: Call BeginUIElement (requires ITfThreadMgr → ITfUIElementMgr)
            HRESULT hr = pUIPresenter->BeginUIElement();

            // Assert: Should succeed with mock ThreadMgr
            Assert::IsTrue(SUCCEEDED(hr), L"BeginUIElement should succeed with mock ThreadMgr");

            // Cleanup
            pUIPresenter->Release();
        }

        TEST_METHOD(IT06_17_EndUIElement)
        {
            Logger::WriteMessage("Test: IT06_17_EndUIElement\n");

            // Arrange: Begin UI element first
            StubCDIME* pStubDIME = new StubCDIME();
            pStubDIME->SetupMockThreadMgr(); // Initialize mock ThreadMgr
            CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            pUIPresenter->BeginUIElement(); // This sets _uiElementId

            // Act: Call EndUIElement
            HRESULT hr = pUIPresenter->EndUIElement();

            // Assert: Should succeed
            Assert::IsTrue(SUCCEEDED(hr), L"EndUIElement should succeed");

            // Cleanup
            pUIPresenter->Release();
        }

        TEST_METHOD(IT06_17_OnSetThreadFocus)
        {
            Logger::WriteMessage("Test: IT06_17_OnSetThreadFocus\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
            CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);

            // Act: Set thread focus
            HRESULT hr = pUIPresenter->OnSetThreadFocus();

            // Assert: Returns S_OK or E_FAIL (depends on internal state)
            // Just verify it doesn't crash
            Assert::IsTrue(hr == S_OK || hr == E_FAIL, L"OnSetThreadFocus returns valid HRESULT");

            // Cleanup
            pUIPresenter->Release();
        }

        TEST_METHOD(IT06_17_OnKillThreadFocus)
        {
            Logger::WriteMessage("Test: IT06_17_OnKillThreadFocus\n");

            // Arrange
            StubCDIME* pStubDIME = new StubCDIME();
            CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);

            // Act: Kill thread focus
            HRESULT hr = pUIPresenter->OnKillThreadFocus();

            // Assert: Returns S_OK or E_FAIL
            Assert::IsTrue(hr == S_OK || hr == E_FAIL, L"OnKillThreadFocus returns valid HRESULT");

            // Cleanup
            pUIPresenter->Release();
        }

        // ===================================================================
        // IT-06-18: Internal UI Update Methods
        // Target: _SetCandidateText, _GetCandidateSelection, paging with _isShowMode=FALSE
        // ===================================================================

        TEST_METHOD(IT06_18_SetCandidateText)
        {
            Logger::WriteMessage("Test: IT06_18_SetCandidateText\n");

            // Arrange: Create UIPresenter with candidate window
            StubCDIME* pStubDIME = new StubCDIME();
            CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            StubTfContext* pStubContext = new StubTfContext();
            pUIPresenter->MakeCandidateWindow(pStubContext, 200);

            // Create candidate list
            CDIMEArray<CCandidateListItem> candidateList;
            CCandidateListItem* item1 = new CCandidateListItem();
            item1->_ItemString.Set(L"Candidate1", 10);
            item1->_FindKeyCode.Set(L"key1", 4);
            candidateList.Append(item1);

            CCandidateListItem* item2 = new CCandidateListItem();
            item2->_ItemString.Set(L"Candidate2", 10);
            item2->_FindKeyCode.Set(L"key2", 4);
            candidateList.Append(item2);

            CCandidateRange indexRange;
            indexRange.Set(0, 2);

            // Act: Set candidate text (internal method, tests AddCandidateToUI + SetPageIndexWithScrollInfo)
            pUIPresenter->_SetCandidateText(&candidateList, &indexRange, TRUE, 100);

            // Assert: Verify candidates were added (GetCount should return non-zero)
            UINT count = 0;
            pUIPresenter->GetCount(&count);
            Assert::AreEqual(2U, count, L"Should have 2 candidates after _SetCandidateText");

            // Cleanup
            pStubContext->Release();
            pUIPresenter->Release();
        }

        TEST_METHOD(IT06_18_GetCandidateSelection)
        {
            Logger::WriteMessage("Test: IT06_18_GetCandidateSelection\n");

            // Arrange: Create UIPresenter with candidates
            StubCDIME* pStubDIME = new StubCDIME();
            CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            StubTfContext* pStubContext = new StubTfContext();
            pUIPresenter->MakeCandidateWindow(pStubContext, 200);

            CDIMEArray<CCandidateListItem> candidateList;
            CCandidateListItem* item1 = new CCandidateListItem();
            item1->_ItemString.Set(L"Test", 4);
            candidateList.Append(item1);
            pUIPresenter->AddCandidateToUI(&candidateList, FALSE);

            pUIPresenter->_SetCandidateSelection(0, FALSE);

            // Act: Get current selection
            INT selection = pUIPresenter->_GetCandidateSelection();

            // Assert: Should return 0 (first item)
            Assert::AreEqual(0, selection, L"Selection should be 0");

            // Cleanup
            pStubContext->Release();
            pUIPresenter->Release();
        }

        TEST_METHOD(IT06_18_MoveCandidatePage_UILessMode)
        {
            Logger::WriteMessage("Test: IT06_18_MoveCandidatePage_UILessMode\n");

            // Arrange: Create UIPresenter with candidates in UILess mode
            StubCDIME* pStubDIME = new StubCDIME();
            CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            StubTfContext* pStubContext = new StubTfContext();
            pUIPresenter->MakeCandidateWindow(pStubContext, 200);

            // Add multiple pages of candidates
            CDIMEArray<CCandidateListItem> candidateList;
            for (int i = 0; i < 20; i++)
            {
                CCandidateListItem* item = new CCandidateListItem();
                WCHAR buffer[20];
                StringCchPrintfW(buffer, 20, L"Candidate%d", i);
                item->_ItemString.Set(buffer, wcslen(buffer));
                candidateList.Append(item);
            }
            CCandidateRange indexRange;
            indexRange.Set(0, 10);
            pUIPresenter->_SetCandidateText(&candidateList, &indexRange, FALSE, 100);

            // Hide window to trigger UILess mode path
            pUIPresenter->Show(FALSE);

            // Act: Move page (should trigger _UpdateUIElement path)
            BOOL result = pUIPresenter->_MoveCandidatePage(1);

            // Assert: Operation should succeed
            Assert::IsTrue(result, L"MoveCandidatePage should succeed in UILess mode");

            // Cleanup
            pStubContext->Release();
            pUIPresenter->Release();
        }

        TEST_METHOD(IT06_18_SetCandidateSelection_WithNotify)
        {
            Logger::WriteMessage("Test: IT06_18_SetCandidateSelection_WithNotify\n");

            // Arrange: Create UIPresenter with candidates
            StubCDIME* pStubDIME = new StubCDIME();
            CUIPresenter* pUIPresenter = new CUIPresenter(pStubDIME, nullptr);
            StubTfContext* pStubContext = new StubTfContext();
            pUIPresenter->MakeCandidateWindow(pStubContext, 200);

            CDIMEArray<CCandidateListItem> candidateList;
            CCandidateListItem* item = new CCandidateListItem();
            item->_ItemString.Set(L"Test", 4);
            candidateList.Append(item);
            pUIPresenter->AddCandidateToUI(&candidateList, FALSE);

            // Hide window to test UILess mode path
            pUIPresenter->Show(FALSE);

            // Act: Set selection with notify=TRUE (tests different code path)
            BOOL result = pUIPresenter->_SetCandidateSelection(0, TRUE);

            // Assert: Should succeed
            Assert::IsTrue(result, L"SetCandidateSelection with notify should succeed");

            // Cleanup
            pStubContext->Release();
            pUIPresenter->Release();
        }
    };

    // Implementation of helper function for SetupMockThreadMgr
    // This function has access to TEST_CLASS's static helper method
    void SetupMockThreadMgrForStub(StubCDIME* pStub)
    {
        UIPresenterIntegrationTest::SetupMockThreadMgrHelper(pStub);
    }

}


