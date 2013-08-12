//
//
// Derived from Microsoft Sample IME by Jeremy '13,7,17
//
//
//#define DEBUG_PRINT

#include "Private.h"
#include "TSFTTS.h"
#include "CompositionProcessorEngine.h"
#include "LanguageBar.h"
#include "Globals.h"
#include "Compartment.h"

//+---------------------------------------------------------------------------
//
// CTSFTTS::_UpdateLanguageBarOnSetFocus
//
//----------------------------------------------------------------------------

void CTSFTTS::_UpdateLanguageBarOnSetFocus(_In_ ITfDocumentMgr *pDocMgrFocus)
{
    BOOL needDisableButtons = FALSE;

    if (!pDocMgrFocus) 
    {
        needDisableButtons = TRUE;
    } 
    else 
    {
        IEnumTfContexts* pEnumContext = nullptr;

        if (FAILED(pDocMgrFocus->EnumContexts(&pEnumContext)) || !pEnumContext) 
        {
            needDisableButtons = TRUE;
        } 
        else 
        {
            ULONG fetched = 0;
            ITfContext* pContext = nullptr;

            if (FAILED(pEnumContext->Next(1, &pContext, &fetched)) || fetched != 1) 
            {
                needDisableButtons = TRUE;
            }

            if (!pContext) 
            {
                // context is not associated
                needDisableButtons = TRUE;
            } 
            else 
            {
                pContext->Release();
            }
        }

        if (pEnumContext) 
        {
            pEnumContext->Release();
        }
    }

    CCompositionProcessorEngine* pCompositionProcessorEngine = nullptr;
    pCompositionProcessorEngine = _pCompositionProcessorEngine;

    SetLanguageBarStatus(TF_LBI_STATUS_DISABLED, needDisableButtons);
}

//+---------------------------------------------------------------------------
//
// InitLanguageBar
//
//----------------------------------------------------------------------------

BOOL CTSFTTS::InitLanguageBar(_In_ CLangBarItemButton *pLangBarItemButton, _In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId, REFGUID guidCompartment)
{
    if (pLangBarItemButton)
    {
        if (pLangBarItemButton->_AddItem(pThreadMgr) == S_OK)
        {
            if (pLangBarItemButton->_RegisterCompartment(pThreadMgr, tfClientId, guidCompartment))
            {
                return TRUE;
            }
        }
    }
    return FALSE;
}

//+---------------------------------------------------------------------------
//
// SetupLanguageBar
//
//----------------------------------------------------------------------------

void CTSFTTS::SetupLanguageBar(_In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId, BOOL isSecureMode)
{
	debugPrint(L"SetupLanguageBar()");
    DWORD dwEnable = 1;
	//win8 only to show IME
	if(Global::isWindows8){
		CreateLanguageBarButton(dwEnable, GUID_LBI_INPUTMODE, Global::LangbarImeModeDescription, Global::ImeModeDescription, Global::ImeModeOnIcoIndex, Global::ImeModeOffIcoIndex, &_pLanguageBar_IMEModeW8, isSecureMode);
		InitLanguageBar(_pLanguageBar_IMEModeW8, pThreadMgr, tfClientId, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
		_pCompartmentKeyboardOpenEventSink = new (std::nothrow) CCompartmentEventSink(CompartmentCallback, this);
		
	}
	
    CreateLanguageBarButton(dwEnable, Global::TSFTTSGuidLangBarIMEMode, Global::LangbarImeModeDescription, Global::ImeModeDescription, Global::ImeModeOnIcoIndex, Global::ImeModeOffIcoIndex, &_pLanguageBar_IMEMode, isSecureMode);
    CreateLanguageBarButton(dwEnable, Global::TSFTTSGuidLangBarDoubleSingleByte, Global::LangbarDoubleSingleByteDescription, Global::DoubleSingleByteDescription, Global::DoubleSingleByteOnIcoIndex, Global::DoubleSingleByteOffIcoIndex, &_pLanguageBar_DoubleSingleByte, isSecureMode);

	
    
	InitLanguageBar(_pLanguageBar_IMEMode, pThreadMgr, tfClientId,  Global::TSFTTSGuidCompartmentIMEMode);
    InitLanguageBar(_pLanguageBar_DoubleSingleByte, pThreadMgr, tfClientId, Global::TSFTTSGuidCompartmentDoubleSingleByte);


    _pCompartmentConversion = new (std::nothrow) CCompartment(pThreadMgr, tfClientId, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION);
	
	_pCompartmentIMEModeEventSink = new (std::nothrow) CCompartmentEventSink(CompartmentCallback, this);
    _pCompartmentConversionEventSink = new (std::nothrow) CCompartmentEventSink(CompartmentCallback, this);
    _pCompartmentDoubleSingleByteEventSink = new (std::nothrow) CCompartmentEventSink(CompartmentCallback, this);

	if (_pCompartmentIMEModeEventSink)
    {
        _pCompartmentIMEModeEventSink->_Advise(pThreadMgr, Global::TSFTTSGuidCompartmentIMEMode);
    }
	if (_pCompartmentKeyboardOpenEventSink && Global::isWindows8)
    {
        _pCompartmentKeyboardOpenEventSink->_Advise(pThreadMgr, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
    }
    if (_pCompartmentConversionEventSink)
    {
        _pCompartmentConversionEventSink->_Advise(pThreadMgr, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION);
    }
    if (_pCompartmentDoubleSingleByteEventSink)
    {
        _pCompartmentDoubleSingleByteEventSink->_Advise(pThreadMgr, Global::TSFTTSGuidCompartmentDoubleSingleByte);
    }
   
    return;
}

//+---------------------------------------------------------------------------
//
// CreateLanguageBarButton
//
//----------------------------------------------------------------------------

void CTSFTTS::CreateLanguageBarButton(DWORD dwEnable, GUID guidLangBar, _In_z_ LPCWSTR pwszDescriptionValue, _In_z_ LPCWSTR pwszTooltipValue, DWORD dwOnIconIndex, DWORD dwOffIconIndex, _Outptr_result_maybenull_ CLangBarItemButton **ppLangBarItemButton, BOOL isSecureMode)
{
	debugPrint(L"CTSFTTS::CreateLanguageBarButton()");
	dwEnable;

    if (ppLangBarItemButton)
    {
        *ppLangBarItemButton = new (std::nothrow) CLangBarItemButton(guidLangBar, pwszDescriptionValue, pwszTooltipValue, dwOnIconIndex, dwOffIconIndex, isSecureMode);
    }

    return;
}


//+---------------------------------------------------------------------------
//
// CTSFTTS::SetLanguageBarStatus
//
//----------------------------------------------------------------------------

VOID CTSFTTS::SetLanguageBarStatus(DWORD status, BOOL isSet)
{
	debugPrint(L"CTSFTTS::SetLanguageBarStatus(), status = %d, isSet = %d", status, isSet);
    if (_pLanguageBar_IMEMode) {
        _pLanguageBar_IMEMode->SetStatus(status, isSet);
    }
	if (_pLanguageBar_IMEModeW8 &&Global::isWindows8) {
        _pLanguageBar_IMEModeW8->SetStatus(status, isSet);
    }
    if (_pLanguageBar_DoubleSingleByte) {
        _pLanguageBar_DoubleSingleByte->SetStatus(status, isSet);
    }

}

//+---------------------------------------------------------------------------
//
// CLangBarItemButton::ctor
//
//----------------------------------------------------------------------------

CLangBarItemButton::CLangBarItemButton(REFGUID guidLangBar, LPCWSTR description, LPCWSTR tooltip, DWORD onIconIndex, DWORD offIconIndex, BOOL isSecureMode)
{
    DWORD bufLen = 0;

    DllAddRef();

    // initialize TF_LANGBARITEMINFO structure.
    _tfLangBarItemInfo.clsidService = Global::TSFTTSCLSID;												    // This LangBarItem belongs to this TextService.
    _tfLangBarItemInfo.guidItem = guidLangBar;															        // GUID of this LangBarItem.
    _tfLangBarItemInfo.dwStyle = TF_LBI_STYLE_BTN_BUTTON;														// This LangBar is a button type.
    _tfLangBarItemInfo.ulSort = 0;																			    // The position of this LangBar Item is not specified.
    StringCchCopy(_tfLangBarItemInfo.szDescription, ARRAYSIZE(_tfLangBarItemInfo.szDescription), description);  // Set the description of this LangBar Item.

    // Initialize the sink pointer to NULL.
    _pLangBarItemSink = nullptr;

    // Initialize ICON index and file name.
    _onIconIndex = onIconIndex;
    _offIconIndex = offIconIndex;

    // Initialize compartment.
    _pCompartment = nullptr;
    _pCompartmentEventSink = nullptr;

    _isAddedToLanguageBar = FALSE;
    _isSecureMode = isSecureMode;
    _status = 0;

    _refCount = 1;

    // Initialize Tooltip
    _pTooltipText = nullptr;
    if (tooltip)
    {
		size_t len = 0;
		if (StringCchLength(tooltip, STRSAFE_MAX_CCH, &len) != S_OK)
        {
            len = 0; 
        }
        bufLen = static_cast<DWORD>(len) + 1;
        _pTooltipText = (LPCWSTR) new (std::nothrow) WCHAR[ bufLen ];
        if (_pTooltipText)
        {
            StringCchCopy((LPWSTR)_pTooltipText, bufLen, tooltip);
        }
    }   
}

//+---------------------------------------------------------------------------
//
// CLangBarItemButton::dtor
//
//----------------------------------------------------------------------------

CLangBarItemButton::~CLangBarItemButton()
{
    DllRelease();
    CleanUp();
}

//+---------------------------------------------------------------------------
//
// CLangBarItemButton::CleanUp
//
//----------------------------------------------------------------------------

void CLangBarItemButton::CleanUp()
{
    if (_pTooltipText)
    {
        delete [] _pTooltipText;
        _pTooltipText = nullptr;
    }

    ITfThreadMgr* pThreadMgr = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_ThreadMgr, 
        NULL, 
        CLSCTX_INPROC_SERVER, 
        IID_ITfThreadMgr, 
        (void**)&pThreadMgr);
    if (SUCCEEDED(hr))
    {
        _UnregisterCompartment(pThreadMgr);

        _RemoveItem(pThreadMgr);
        pThreadMgr->Release();
        pThreadMgr = nullptr;
    }

    if (_pCompartment)
    {
        delete _pCompartment;
        _pCompartment = nullptr;
    }

    if (_pCompartmentEventSink)
    {
        delete _pCompartmentEventSink;
        _pCompartmentEventSink = nullptr;
    }
}

//+---------------------------------------------------------------------------
//
// CLangBarItemButton::QueryInterface
//
//----------------------------------------------------------------------------

STDAPI CLangBarItemButton::QueryInterface(REFIID riid, _Outptr_ void **ppvObj)
{
    if (ppvObj == nullptr)
    {
        return E_INVALIDARG;
    }

    *ppvObj = nullptr;

    if (IsEqualIID(riid, IID_IUnknown) ||
        IsEqualIID(riid, IID_ITfLangBarItem) ||
        IsEqualIID(riid, IID_ITfLangBarItemButton))
    {
        *ppvObj = (ITfLangBarItemButton *)this;
    }
    else if (IsEqualIID(riid, IID_ITfSource))
    {
        *ppvObj = (ITfSource *)this;
    }

    if (*ppvObj)
    {
        AddRef();
        return S_OK;
    }

    return E_NOINTERFACE;
}


//+---------------------------------------------------------------------------
//
// CLangBarItemButton::AddRef
//
//----------------------------------------------------------------------------

STDAPI_(ULONG) CLangBarItemButton::AddRef()
{
    return ++_refCount;
}

//+---------------------------------------------------------------------------
//
// CLangBarItemButton::Release
//
//----------------------------------------------------------------------------

STDAPI_(ULONG) CLangBarItemButton::Release()
{
    LONG cr = --_refCount;

    assert(_refCount >= 0);

    if (_refCount == 0)
    {
        delete this;
    }

    return cr;
}

//+---------------------------------------------------------------------------
//
// GetInfo
//
//----------------------------------------------------------------------------

STDAPI CLangBarItemButton::GetInfo(_Out_ TF_LANGBARITEMINFO *pInfo)
{
    _tfLangBarItemInfo.dwStyle |= TF_LBI_STYLE_SHOWNINTRAY;
    *pInfo = _tfLangBarItemInfo;
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// GetStatus
//
//----------------------------------------------------------------------------

STDAPI CLangBarItemButton::GetStatus(_Out_ DWORD *pdwStatus)
{
    if (pdwStatus == nullptr)
    {
        E_INVALIDARG;
    }

    *pdwStatus = _status;
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// SetStatus
//
//----------------------------------------------------------------------------

void CLangBarItemButton::SetStatus(DWORD dwStatus, BOOL fSet)
{
    BOOL isChange = FALSE;

    if (fSet) 
    {
        if (!(_status & dwStatus)) 
        {
            _status |= dwStatus;
            isChange = TRUE;
        }
    } 
    else 
    {
        if (_status & dwStatus) 
        {
            _status &= ~dwStatus;
            isChange = TRUE;
        }
    }

    if (isChange && _pLangBarItemSink) 
    {
        _pLangBarItemSink->OnUpdate(TF_LBI_STATUS | TF_LBI_ICON);
    }

    return;
}

//+---------------------------------------------------------------------------
//
// Show
//
//----------------------------------------------------------------------------

STDAPI CLangBarItemButton::Show(BOOL fShow)
{
	fShow;
    if (_pLangBarItemSink)
    {
        _pLangBarItemSink->OnUpdate(TF_LBI_STATUS);
    }
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// GetTooltipString
//
//----------------------------------------------------------------------------

STDAPI CLangBarItemButton::GetTooltipString(_Out_ BSTR *pbstrToolTip)
{
    *pbstrToolTip = SysAllocString(_pTooltipText);

    return (*pbstrToolTip == nullptr) ? E_OUTOFMEMORY : S_OK;
}

//+---------------------------------------------------------------------------
//
// OnClick
//
//----------------------------------------------------------------------------

STDAPI CLangBarItemButton::OnClick(TfLBIClick click, POINT pt, _In_ const RECT *prcArea)
{
    click;pt;
    prcArea;

	
    BOOL isOn = FALSE;

    _pCompartment->_GetCompartmentBOOL(isOn);
    _pCompartment->_SetCompartmentBOOL(isOn ? FALSE : TRUE);
	
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// InitMenu
//
//----------------------------------------------------------------------------
//#define MENUITEM_INDEX_OPENCLOSE 2
STDAPI CLangBarItemButton::InitMenu(_In_ ITfMenu *pMenu)
{
	pMenu;
	/*
    DWORD dwFlags = 0;
    dwFlags |= TF_LBMENUF_CHECKED;

    pMenu->AddMenuItem(MENUITEM_INDEX_OPENCLOSE,
                       dwFlags,
                       NULL,
                       NULL,
                       L"About TSFTTS...",
                       (ULONG)wcslen( L"About TSFTTS..."),
                       NULL);


	*/
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// OnMenuSelect
//
//----------------------------------------------------------------------------

STDAPI CLangBarItemButton::OnMenuSelect(UINT wID)
{
    wID;

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// GetIcon
//
//----------------------------------------------------------------------------

STDAPI CLangBarItemButton::GetIcon(_Out_ HICON *phIcon)
{
    BOOL isOn = FALSE;

    if (!_pCompartment)
    {
        return E_FAIL;
    }
    if (!phIcon)
    {
        return E_FAIL;
    }
    *phIcon = nullptr;

    _pCompartment->_GetCompartmentBOOL(isOn);

    DWORD status = 0;
    GetStatus(&status);

	// If IME is working on the UAC mode, the size of ICON should be 24 x 24.
    int desiredSize = 16;
    if (_isSecureMode) // detect UAC mode
    {
        desiredSize = _isSecureMode ? 24 : 16;
    }

    if (isOn && !(status & TF_LBI_STATUS_DISABLED))
    {
        if (Global::dllInstanceHandle)
        {
            *phIcon = reinterpret_cast<HICON>(LoadImage(Global::dllInstanceHandle, MAKEINTRESOURCE(_onIconIndex), IMAGE_ICON, desiredSize, desiredSize, 0));
        }
    }
    else
    {
        if (Global::dllInstanceHandle)
        {
            *phIcon = reinterpret_cast<HICON>(LoadImage(Global::dllInstanceHandle, MAKEINTRESOURCE(_offIconIndex), IMAGE_ICON, desiredSize, desiredSize, 0));
        }
    }

    return (*phIcon != NULL) ? S_OK : E_FAIL;
}

//+---------------------------------------------------------------------------
//
// GetText
//
//----------------------------------------------------------------------------

STDAPI CLangBarItemButton::GetText(_Out_ BSTR *pbstrText)
{
    *pbstrText = SysAllocString(_tfLangBarItemInfo.szDescription);

    return (*pbstrText == nullptr) ? E_OUTOFMEMORY : S_OK;
}

//+---------------------------------------------------------------------------
//
// AdviseSink
//
//----------------------------------------------------------------------------

STDAPI CLangBarItemButton::AdviseSink(__RPC__in REFIID riid, __RPC__in_opt IUnknown *punk, __RPC__out DWORD *pdwCookie)
{
    // We allow only ITfLangBarItemSink interface.
    if (!IsEqualIID(IID_ITfLangBarItemSink, riid))
    {
        return CONNECT_E_CANNOTCONNECT;
    }

    // We support only one sink once.
    if (_pLangBarItemSink != nullptr)
    {
        return CONNECT_E_ADVISELIMIT;
    }

    // Query the ITfLangBarItemSink interface and store it into _pLangBarItemSink.
    if (punk == nullptr)
    {
        return E_INVALIDARG;
    }
    if (punk->QueryInterface(IID_ITfLangBarItemSink, (void **)&_pLangBarItemSink) != S_OK)
    {
        _pLangBarItemSink = nullptr;
        return E_NOINTERFACE;
    }

    // return our cookie.
    *pdwCookie = _cookie;
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// UnadviseSink
//
//----------------------------------------------------------------------------

STDAPI CLangBarItemButton::UnadviseSink(DWORD dwCookie)
{
    // Check the given cookie.
    if (dwCookie != _cookie)
    {
        return CONNECT_E_NOCONNECTION;
    }

    // If there is nno connected sink, we just fail.
    if (_pLangBarItemSink == nullptr)
    {
        return CONNECT_E_NOCONNECTION;
    }

    _pLangBarItemSink->Release();
    _pLangBarItemSink = nullptr;

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _AddItem
//
//----------------------------------------------------------------------------

HRESULT CLangBarItemButton::_AddItem(_In_ ITfThreadMgr *pThreadMgr)
{
    HRESULT hr = S_OK;
    ITfLangBarItemMgr* pLangBarItemMgr = nullptr;

    if (_isAddedToLanguageBar)
    {
        return S_OK;
    }

    hr = pThreadMgr->QueryInterface(IID_ITfLangBarItemMgr, (void **)&pLangBarItemMgr);
    if (SUCCEEDED(hr))
    {
        hr = pLangBarItemMgr->AddItem(this);
        if (SUCCEEDED(hr))
        {
            _isAddedToLanguageBar = TRUE;
        }
        pLangBarItemMgr->Release();
    }

    return hr;
}

//+---------------------------------------------------------------------------
//
// _RemoveItem
//
//----------------------------------------------------------------------------

HRESULT CLangBarItemButton::_RemoveItem(_In_ ITfThreadMgr *pThreadMgr)
{
    HRESULT hr = S_OK;
    ITfLangBarItemMgr* pLangBarItemMgr = nullptr;

    if (!_isAddedToLanguageBar)
    {
        return S_OK;
    }

    hr = pThreadMgr->QueryInterface(IID_ITfLangBarItemMgr, (void **)&pLangBarItemMgr);
    if (SUCCEEDED(hr))
    {
        hr = pLangBarItemMgr->RemoveItem(this);
        if (SUCCEEDED(hr))
        {
            _isAddedToLanguageBar = FALSE;
        }
        pLangBarItemMgr->Release();
    }

    return hr;
}

//+---------------------------------------------------------------------------
//
// _RegisterCompartment
//
//----------------------------------------------------------------------------

BOOL CLangBarItemButton::_RegisterCompartment(_In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId, REFGUID guidCompartment)
{
	debugPrint(L"CLangBarItemButton::_RegisterCompartment()");
    _pCompartment = new (std::nothrow) CCompartment(pThreadMgr, tfClientId, guidCompartment);
    if (_pCompartment)
    {
        // Advice ITfCompartmentEventSink
        _pCompartmentEventSink = new (std::nothrow) CCompartmentEventSink(_CompartmentCallback, this);
        if (_pCompartmentEventSink)
        {
            _pCompartmentEventSink->_Advise(pThreadMgr, guidCompartment);
        }
        else
        {
            delete _pCompartment;
            _pCompartment = nullptr;
        }
    }

    return _pCompartment ? TRUE : FALSE;
}

//+---------------------------------------------------------------------------
//
// _UnregisterCompartment
//
//----------------------------------------------------------------------------

BOOL CLangBarItemButton::_UnregisterCompartment(_In_ ITfThreadMgr *pThreadMgr)
{
	debugPrint(L"CLangBarItemButton::_UnregisterCompartmen()");
	pThreadMgr;
    if (_pCompartment)
    {
        // Unadvice ITfCompartmentEventSink
        if (_pCompartmentEventSink)
        {
            _pCompartmentEventSink->_Unadvise();
        }

        // clear ITfCompartment
        _pCompartment->_ClearCompartment();
    }

    return TRUE;
}

//+---------------------------------------------------------------------------
//
// _CompartmentCallback
//
//----------------------------------------------------------------------------

// static
HRESULT CLangBarItemButton::_CompartmentCallback(_In_ void *pv, REFGUID guidCompartment)
{
    CLangBarItemButton* fakeThis = (CLangBarItemButton*)pv;

    GUID guid = GUID_NULL;
    fakeThis->_pCompartment->_GetGUID(&guid);

    if (IsEqualGUID(guid, guidCompartment))
    {
        if (fakeThis->_pLangBarItemSink)
        {
            fakeThis->_pLangBarItemSink->OnUpdate(TF_LBI_STATUS | TF_LBI_ICON);
        }
    }

    return S_OK;
}

void CTSFTTS::InitializeTSFTTSCompartment(_In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId)
{
	// set initial mode
	if(Global::isWindows8){
		CCompartment CompartmentKeyboardOpen(pThreadMgr, tfClientId, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
		CompartmentKeyboardOpen._SetCompartmentBOOL(TRUE);
	}

	CCompartment CompartmentIMEMode(pThreadMgr, tfClientId, Global::TSFTTSGuidCompartmentIMEMode);
    CompartmentIMEMode._SetCompartmentBOOL(TRUE);

    CCompartment CompartmentDoubleSingleByte(pThreadMgr, tfClientId, Global::TSFTTSGuidCompartmentDoubleSingleByte);
    CompartmentDoubleSingleByte._SetCompartmentBOOL(FALSE);


    PrivateCompartmentsUpdated(pThreadMgr);
}
//+---------------------------------------------------------------------------
//
// CompartmentCallback
//
//----------------------------------------------------------------------------

// static
HRESULT CTSFTTS::CompartmentCallback(_In_ void *pv, REFGUID guidCompartment)
{
    CTSFTTS* fakeThis = (CTSFTTS*)pv;
    if (nullptr == fakeThis)
    {
        return E_INVALIDARG;
    }

    ITfThreadMgr* pThreadMgr = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER, IID_ITfThreadMgr, (void**)&pThreadMgr);
    if (FAILED(hr))
    {
        return E_FAIL;
    }

    if (IsEqualGUID(guidCompartment, Global::TSFTTSGuidCompartmentDoubleSingleByte) )
    {
        fakeThis->PrivateCompartmentsUpdated(pThreadMgr);
    }
    else if (IsEqualGUID(guidCompartment, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION) ||
        IsEqualGUID(guidCompartment, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_SENTENCE))
    {
        fakeThis->ConversionModeCompartmentUpdated(pThreadMgr);
    }
    else if (IsEqualGUID(guidCompartment, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE)||
			 IsEqualGUID(guidCompartment, Global::TSFTTSGuidCompartmentIMEMode))
    {
		fakeThis->KeyboardOpenCompartmentUpdated(pThreadMgr, guidCompartment);
    }

    pThreadMgr->Release();
    pThreadMgr = nullptr;

    return S_OK;
}

void CTSFTTS::ShowAllLanguageBarIcons()
{
	debugPrint(L"CTSFTTS::ShowAllLanguageBarIcons()\n");
    SetLanguageBarStatus(TF_LBI_STATUS_HIDDEN, FALSE);
}

void CTSFTTS::HideAllLanguageBarIcons()
{
	debugPrint(L"CTSFTTS::HideAllLanguageBarIcons()\n");
    SetLanguageBarStatus(TF_LBI_STATUS_HIDDEN, TRUE);
}

//+---------------------------------------------------------------------------
//
// UpdatePrivateCompartments
//
//----------------------------------------------------------------------------

void CTSFTTS::ConversionModeCompartmentUpdated(_In_ ITfThreadMgr *pThreadMgr, BOOL *setKeyboardOpenClose)
{
	debugPrint(L"CTSFTTS::ConversionModeCompartmentUpdated()\n");
    if (!_pCompartmentConversion)
    {
        return;
    }

    DWORD conversionMode = 0;
    if (FAILED(_pCompartmentConversion->_GetCompartmentDWORD(conversionMode)))
    {
        return;
    }

    BOOL isDouble = FALSE;
    CCompartment CompartmentDoubleSingleByte(pThreadMgr, _tfClientId, Global::TSFTTSGuidCompartmentDoubleSingleByte);
    if (SUCCEEDED(CompartmentDoubleSingleByte._GetCompartmentBOOL(isDouble)))
    {
        if (!isDouble && (conversionMode & TF_CONVERSIONMODE_FULLSHAPE))
        {
            CompartmentDoubleSingleByte._SetCompartmentBOOL(TRUE);
        }
        else if (isDouble && !(conversionMode & TF_CONVERSIONMODE_FULLSHAPE))
        {
            CompartmentDoubleSingleByte._SetCompartmentBOOL(FALSE);
        }
		
    }
  
   
	BOOL fOpen = FALSE;
	if(Global::isWindows8){
		CCompartment CompartmentKeyboardOpen(pThreadMgr, _tfClientId, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
		if (setKeyboardOpenClose != nullptr )
		{
			CompartmentKeyboardOpen._SetCompartmentBOOL(*setKeyboardOpenClose);
		}
		else if(SUCCEEDED(CompartmentKeyboardOpen._GetCompartmentBOOL(fOpen)))
		{
			if (fOpen && !(conversionMode & TF_CONVERSIONMODE_NATIVE))
			{
				CompartmentKeyboardOpen._SetCompartmentBOOL(FALSE);
			}
			else if (!fOpen && (conversionMode & TF_CONVERSIONMODE_NATIVE))
			{
				CompartmentKeyboardOpen._SetCompartmentBOOL(TRUE);
			}
		}
	}
	fOpen = FALSE;
	CCompartment CompartmentIMEMode(pThreadMgr, _tfClientId, Global::TSFTTSGuidCompartmentIMEMode);
	if (setKeyboardOpenClose != nullptr )
	{
		CompartmentIMEMode._SetCompartmentBOOL(*setKeyboardOpenClose);
	}
	else if(SUCCEEDED(CompartmentIMEMode._GetCompartmentBOOL(fOpen)))
	{
		if (fOpen && !(conversionMode & TF_CONVERSIONMODE_NATIVE))
		{
			CompartmentIMEMode._SetCompartmentBOOL(FALSE);
		}
		else if (!fOpen && (conversionMode & TF_CONVERSIONMODE_NATIVE))
		{
			CompartmentIMEMode._SetCompartmentBOOL(TRUE);
		}
	}
	

    
	

}

//+---------------------------------------------------------------------------
//
// PrivateCompartmentsUpdated()
//
//----------------------------------------------------------------------------

void CTSFTTS::PrivateCompartmentsUpdated(_In_ ITfThreadMgr *pThreadMgr)
{
	debugPrint(L"CTSFTTS::PrivateCompartmentsUpdated()");
    if (!_pCompartmentConversion)
    {
        return;
    }

    DWORD conversionMode = 0;
    DWORD conversionModePrev = 0;
    if (FAILED(_pCompartmentConversion->_GetCompartmentDWORD(conversionMode)))
    {
        return;
    }

    conversionModePrev = conversionMode;

    BOOL isDouble = FALSE;
    CCompartment CompartmentDoubleSingleByte(pThreadMgr, _tfClientId, Global::TSFTTSGuidCompartmentDoubleSingleByte);
    if (SUCCEEDED(CompartmentDoubleSingleByte._GetCompartmentBOOL(isDouble)))
    {
        if (!isDouble && (conversionMode & TF_CONVERSIONMODE_FULLSHAPE))
        {
            conversionMode &= ~TF_CONVERSIONMODE_FULLSHAPE;
        }
        else if (isDouble && !(conversionMode & TF_CONVERSIONMODE_FULLSHAPE))
        {
            conversionMode |= TF_CONVERSIONMODE_FULLSHAPE;
        }
    }
	
    if (conversionMode != conversionModePrev)
    {
        _pCompartmentConversion->_SetCompartmentDWORD(conversionMode);
		if(isDouble) OnSwitchedToFullShape();
		else OnSwitchedToHalfShape();
    }
}

//+---------------------------------------------------------------------------
//
// KeyboardOpenCompartmentUpdated
//
//----------------------------------------------------------------------------

void CTSFTTS::KeyboardOpenCompartmentUpdated(_In_ ITfThreadMgr *pThreadMgr, _In_ REFGUID guidCompartment)
{
	debugPrint(L"CTSFTTS::KeyboardOpenCompartmentUpdated()\n");
    if (!_pCompartmentConversion)
    {
        return;
    }

    DWORD conversionMode = 0;
    DWORD conversionModePrev = 0;
    if (FAILED(_pCompartmentConversion->_GetCompartmentDWORD(conversionMode)))
    {
        return;
    }

    conversionModePrev = conversionMode;

    BOOL isOpen = FALSE;  
    CCompartment CompartmentIMEMode(pThreadMgr, _tfClientId, Global::TSFTTSGuidCompartmentIMEMode);
	if(IsEqualGUID(guidCompartment, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE ))// Global::isWindows8 && check GUID_COMPARTMENT_KEYBOARD_OPENCLOSE in Windows 8.
	{
		
		CCompartment CompartmentKeyboardOpen(pThreadMgr, _tfClientId, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
		if (SUCCEEDED(CompartmentKeyboardOpen._GetCompartmentBOOL(isOpen)))
		{
			if (isOpen && !(conversionMode & TF_CONVERSIONMODE_NATIVE))
			{
				conversionMode |= TF_CONVERSIONMODE_NATIVE;
			}
			else if (!isOpen && (conversionMode & TF_CONVERSIONMODE_NATIVE))
			{
				conversionMode &= ~TF_CONVERSIONMODE_NATIVE;
			}
		}
		if (conversionMode != conversionModePrev)
		{
		     _pCompartmentConversion->_SetCompartmentDWORD(conversionMode);
   			if(isOpen) OnKeyboardOpen();
			else OnKeyboardClosed();
		}
	}
	isOpen = FALSE;
	if (IsEqualGUID(guidCompartment, Global::TSFTTSGuidCompartmentIMEMode) && SUCCEEDED(CompartmentIMEMode._GetCompartmentBOOL(isOpen)))
	{
		if (isOpen && !(conversionMode & TF_CONVERSIONMODE_NATIVE))
		{
			conversionMode |= TF_CONVERSIONMODE_NATIVE;
			
		}
		else if (!isOpen && (conversionMode & TF_CONVERSIONMODE_NATIVE))
		{
			conversionMode &= ~TF_CONVERSIONMODE_NATIVE;
			
		}
		if (conversionMode != conversionModePrev)
		{
			_pCompartmentConversion->_SetCompartmentDWORD(conversionMode);
			if(isOpen) OnKeyboardOpen();
			else OnKeyboardClosed();
		}
	
	}
	



   
}

void CTSFTTS::OnKeyboardClosed()
{
	debugPrint(L"CTSFTTS::OnKeyboardClosed()\n");
	// switching to English (native) mode delete the phrase candidate window before exting.
	if(_IsComposing()) 
		_EndComposition(_pContext);
	_DeleteCandidateList(FALSE, NULL);
	CStringRange notifyText;
	if(CConfig::GetShowNotifyDesktop())
		ShowNotifyText(&notifyText.Set(L"英文", 2));
}

void CTSFTTS::OnKeyboardOpen()
{
	debugPrint(L"CTSFTTS::OnKeyboardOpen()\n");
	// switching to Chinese mode
	CConfig::LoadConfig();
	CStringRange notifyText;
	if(CConfig::GetShowNotifyDesktop())
		ShowNotifyText(&notifyText.Set(L"中文", 2));	
}

void CTSFTTS::OnSwitchedToFullShape()
{
	debugPrint(L"CTSFTTS::OnSwitchedToFullShape()\n");
	if(_IsComposing()) 
		_EndComposition(_pContext);
	_DeleteCandidateList(FALSE, NULL);
	CStringRange notifyText;
	if(CConfig::GetShowNotifyDesktop())
		ShowNotifyText(&notifyText.Set(L"全形", 2));
}

void CTSFTTS::OnSwitchedToHalfShape()
{
	debugPrint(L"CTSFTTS::OnSwitchedToHalfShape()\n");
	if(_IsComposing()) 
		_EndComposition(_pContext);
	_DeleteCandidateList(FALSE, NULL);
	CStringRange notifyText;
	if(CConfig::GetShowNotifyDesktop())
		ShowNotifyText(&notifyText.Set(L"半形", 2));
}
