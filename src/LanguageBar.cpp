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
//#define DEBUG_PRINT

#include "Private.h"
#include "DIME.h"
#include "CompositionProcessorEngine.h"
#include "LanguageBar.h"
#include "Globals.h"
#include "Compartment.h"
#include "UIPresenter.h"
#include "Config.h"

#ifdef DIME_UNIT_TESTING
DimeLangNotifyFn g_dimeLangNotify = nullptr;
#endif

void CDIME::_LangShowText(const WCHAR* text, UINT len, UINT delay)
{
#ifdef DIME_UNIT_TESTING
	if (g_dimeLangNotify && text)
		g_dimeLangNotify(text, len, delay);
#endif
	if (_pUIPresenter && text)
	{
		CStringRange notify;
		_pUIPresenter->ShowNotifyText(&notify.Set(text, len), delay, CHN_ENG_NOTIFY_DELAY, NOTIFY_TYPE::NOTIFY_CHN_ENG);
	}
}

//+---------------------------------------------------------------------------
//
// CDIME::_UpdateLanguageBarOnSetFocus
//
//----------------------------------------------------------------------------

BOOL CDIME::_UpdateLanguageBarOnSetFocus(_In_ ITfDocumentMgr *pDocMgrFocus)
{
	debugPrint(L"_UpdateLanguageBarOnSetFocus()");
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

            if (pContext == nullptr) 
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

   
    SetLanguageBarStatus(TF_LBI_STATUS_DISABLED, needDisableButtons);
	return needDisableButtons;
}

//+---------------------------------------------------------------------------
//
// InitLanguageBar
//
//----------------------------------------------------------------------------

BOOL CDIME::InitLanguageBar(_In_ CLangBarItemButton *pLangBarItemButton, _In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId, REFGUID guidCompartment)
{
	debugPrint(L"InitLanguageBar()");
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

void CDIME::SetupLanguageBar(_In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId, BOOL isSecureMode)
{
	debugPrint(L"SetupLanguageBar()");


	if (pThreadMgr == nullptr) return;

	DWORD dwEnable = 1;
	//to show IME, win8 only. 
	if (Global::isWindows8){
		CreateLanguageBarButton(dwEnable, GUID_LBI_INPUTMODE, Global::LangbarImeModeDescription, Global::ImeModeDescription, Global::ImeModeOnIcoIndex, Global::ImeModeOffIcoIndex, &_pLanguageBar_IMEModeW8, isSecureMode);
		InitLanguageBar(_pLanguageBar_IMEModeW8, pThreadMgr, tfClientId, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
		if (_pCompartmentKeyboardOpenEventSink == nullptr)
		{

			_pCompartmentKeyboardOpenEventSink = new (std::nothrow) CCompartmentEventSink(CompartmentCallback, this);
			if (_pCompartmentKeyboardOpenEventSink)
			{
				_pCompartmentKeyboardOpenEventSink->_Advise(pThreadMgr, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
			}
		}


	}

	if (_pLanguageBar_IMEMode == nullptr)
	{
		CreateLanguageBarButton(dwEnable, Global::DIMEGuidLangBarIMEMode, Global::LangbarImeModeDescription, Global::ImeModeDescription, Global::ImeModeOnIcoIndex, Global::ImeModeOffIcoIndex, &_pLanguageBar_IMEMode, isSecureMode);
		InitLanguageBar(_pLanguageBar_IMEMode, pThreadMgr, tfClientId, Global::DIMEGuidCompartmentIMEMode);
	}

	if (_pLanguageBar_DoubleSingleByte == nullptr)
	{
		CreateLanguageBarButton(dwEnable, Global::DIMEGuidLangBarDoubleSingleByte, Global::LangbarDoubleSingleByteDescription, Global::DoubleSingleByteDescription, Global::DoubleSingleByteOnIcoIndex, Global::DoubleSingleByteOffIcoIndex, &_pLanguageBar_DoubleSingleByte, isSecureMode);
		InitLanguageBar(_pLanguageBar_DoubleSingleByte, pThreadMgr, tfClientId, Global::DIMEGuidCompartmentDoubleSingleByte);
	}


	if (_pCompartmentConversion == nullptr)
		_pCompartmentConversion = new (std::nothrow) CCompartment(pThreadMgr, tfClientId, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION);

	if (_pCompartmentIMEModeEventSink == nullptr)
	{
		_pCompartmentIMEModeEventSink = new (std::nothrow) CCompartmentEventSink(CompartmentCallback, this);
		if (_pCompartmentIMEModeEventSink)
		{
			_pCompartmentIMEModeEventSink->_Advise(pThreadMgr, Global::DIMEGuidCompartmentIMEMode);
		}
	}

	if (_pCompartmentConversionEventSink == nullptr)
	{
		_pCompartmentConversionEventSink = new (std::nothrow) CCompartmentEventSink(CompartmentCallback, this);
		if (_pCompartmentConversionEventSink)
		{
			_pCompartmentConversionEventSink->_Advise(pThreadMgr, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION);
		}
	}

	if (_pCompartmentDoubleSingleByteEventSink == nullptr)
	{
		_pCompartmentDoubleSingleByteEventSink = new (std::nothrow) CCompartmentEventSink(CompartmentCallback, this);

		if (_pCompartmentDoubleSingleByteEventSink)
		{
			_pCompartmentDoubleSingleByteEventSink->_Advise(pThreadMgr, Global::DIMEGuidCompartmentDoubleSingleByte);
		}
	}

	
	return;
}

//+---------------------------------------------------------------------------
//
// CreateLanguageBarButton
//
//----------------------------------------------------------------------------

void CDIME::CreateLanguageBarButton(DWORD dwEnable, GUID guidLangBar, _In_z_ LPCWSTR pwszDescriptionValue, _In_z_ LPCWSTR pwszTooltipValue, DWORD dwOnIconIndex, DWORD dwOffIconIndex, _Outptr_result_maybenull_ CLangBarItemButton **ppLangBarItemButton, BOOL isSecureMode)
{
	debugPrint(L"CDIME::CreateLanguageBarButton()");
	dwEnable;

    if (ppLangBarItemButton)
    {
        *ppLangBarItemButton = new (std::nothrow) CLangBarItemButton(guidLangBar, pwszDescriptionValue, pwszTooltipValue, dwOnIconIndex, dwOffIconIndex, isSecureMode);
    }

    return;
}


//+---------------------------------------------------------------------------
//
// CDIME::SetLanguageBarStatus
//
//----------------------------------------------------------------------------

VOID CDIME::SetLanguageBarStatus(DWORD status, BOOL isSet)
{
	debugPrint(L"CDIME::SetLanguageBarStatus(), status = %d, isSet = %d", status, isSet);
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
	debugPrint(L"CLangBarItemButton()");

    DWORD bufLen = 0;

    DllAddRef();

    // initialize TF_LANGBARITEMINFO structure.
    _tfLangBarItemInfo.clsidService = Global::DIMECLSID;												    // This LangBarItem belongs to this TextService.
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

	if(_pCompartment)
	{
		_pCompartment->_GetCompartmentBOOL(isOn);
		_pCompartment->_SetCompartmentBOOL(isOn ? FALSE : TRUE);
	}
	
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
                       L"About DIME...",
                       (ULONG)wcslen( L"About DIME..."),
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

    if (_pCompartment == nullptr)
    {
        return E_FAIL;
    }
    if (phIcon == nullptr)
    {
        return E_FAIL;
    }
    *phIcon = nullptr;

    _pCompartment->_GetCompartmentBOOL(isOn);

    DWORD status = 0;
    GetStatus(&status);

	
    UINT _iconDpi = CConfig::GetDpiForHwnd(nullptr);
    if (isOn && !(status & TF_LBI_STATUS_DISABLED))
    {
        if (Global::dllInstanceHandle)
        {
            *phIcon = reinterpret_cast<HICON>(LoadImage(Global::dllInstanceHandle, MAKEINTRESOURCE(_onIconIndex), IMAGE_ICON, CConfig::GetSystemMetricsDpi(SM_CXSMICON, _iconDpi), CConfig::GetSystemMetricsDpi(SM_CYSMICON, _iconDpi), 0));
        }
    }
    else
    {
        if (Global::dllInstanceHandle)
        {
            *phIcon = reinterpret_cast<HICON>(LoadImage(Global::dllInstanceHandle, MAKEINTRESOURCE(_offIconIndex), IMAGE_ICON, CConfig::GetSystemMetricsDpi(SM_CXSMICON, _iconDpi), CConfig::GetSystemMetricsDpi(SM_CYSMICON, _iconDpi), 0));
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

    if (_isAddedToLanguageBar || pThreadMgr == nullptr)
    {
        return S_OK;
    }

    hr = pThreadMgr->QueryInterface(IID_ITfLangBarItemMgr, (void **)&pLangBarItemMgr);
    if (SUCCEEDED(hr) && pLangBarItemMgr)
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

    if (!_isAddedToLanguageBar || pThreadMgr == nullptr)
    {
        return S_OK;
    }

    hr = pThreadMgr->QueryInterface(IID_ITfLangBarItemMgr, (void **)&pLangBarItemMgr);
    if (SUCCEEDED(hr) && pLangBarItemMgr)
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
	debugPrint(L"CLangBarItemButton::_UnregisterCompartment()");
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

	if(fakeThis == nullptr) 
		return E_INVALIDARG;

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

void CDIME::InitializeDIMECompartment(_In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId)
{
	debugPrint(L"CDIME::CreateLanguageBarButton()");
	if(pThreadMgr == nullptr) return;
	// set initial mode
	if(Global::isWindows8){
		CCompartment CompartmentKeyboardOpen(pThreadMgr, tfClientId, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
		CompartmentKeyboardOpen._SetCompartmentBOOL(TRUE);
	}

	CCompartment CompartmentIMEMode(pThreadMgr, tfClientId, Global::DIMEGuidCompartmentIMEMode);
    CompartmentIMEMode._SetCompartmentBOOL(TRUE);

    CCompartment CompartmentDoubleSingleByte(pThreadMgr, tfClientId, Global::DIMEGuidCompartmentDoubleSingleByte);
    CompartmentDoubleSingleByte._SetCompartmentBOOL(FALSE);


    PrivateCompartmentsUpdated(pThreadMgr);
}
//+---------------------------------------------------------------------------
//
// CompartmentCallback
//
//----------------------------------------------------------------------------

// static
HRESULT CDIME::CompartmentCallback(_In_ void *pv, REFGUID guidCompartment)
{
	debugPrint(L"CDIME::CompartmentCallback()\n");
    CDIME* fakeThis = (CDIME*)pv;
    if (nullptr == fakeThis)
    {
        return E_INVALIDARG;
    }

    ITfThreadMgr* pThreadMgr = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER, IID_ITfThreadMgr, (void**)&pThreadMgr);
    if (FAILED(hr) || pThreadMgr == nullptr)
    {
        return E_FAIL;
    }

    if (IsEqualGUID(guidCompartment, Global::DIMEGuidCompartmentDoubleSingleByte) )
    {
        fakeThis->PrivateCompartmentsUpdated(pThreadMgr);
    }
    else if (IsEqualGUID(guidCompartment, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION) ||
        IsEqualGUID(guidCompartment, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_SENTENCE))
    {
        fakeThis->ConversionModeCompartmentUpdated(pThreadMgr);
    }
    else if (IsEqualGUID(guidCompartment, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE)||
			 IsEqualGUID(guidCompartment, Global::DIMEGuidCompartmentIMEMode))
    {
		fakeThis->KeyboardOpenCompartmentUpdated(pThreadMgr, guidCompartment);
    }

    pThreadMgr->Release();
    pThreadMgr = nullptr;

    return S_OK;
}

void CDIME::ShowAllLanguageBarIcons()
{
	debugPrint(L"CDIME::ShowAllLanguageBarIcons()\n");
    SetLanguageBarStatus(TF_LBI_STATUS_HIDDEN, FALSE);
}

void CDIME::HideAllLanguageBarIcons()
{
	debugPrint(L"CDIME::HideAllLanguageBarIcons()\n");
    SetLanguageBarStatus(TF_LBI_STATUS_HIDDEN, TRUE);
}

//+---------------------------------------------------------------------------
//
// UpdatePrivateCompartments
//
//----------------------------------------------------------------------------

void CDIME::ConversionModeCompartmentUpdated(_In_ ITfThreadMgr *pThreadMgr, BOOL *setKeyboardOpenClose)
{
#if defined(DIME_USE_RUST_ENGINE)
	_ConversionModeCompartmentUpdatedRust(pThreadMgr, setKeyboardOpenClose);
#else
	_ConversionModeCompartmentUpdatedCpp(pThreadMgr, setKeyboardOpenClose);
#endif
}

void CDIME::_ConversionModeCompartmentUpdatedCpp(_In_ ITfThreadMgr *pThreadMgr, BOOL *setKeyboardOpenClose)
{
	debugPrint(L"CDIME::ConversionModeCompartmentUpdated()\n");
    if (_pCompartmentConversion == nullptr)
    {
        return;
    }

    DWORD conversionMode = 0;
    if (FAILED(_pCompartmentConversion->_GetCompartmentDWORD(conversionMode)))
    {
        return;
    }

    BOOL isDouble = FALSE;
    CCompartment CompartmentDoubleSingleByte(pThreadMgr, _tfClientId, Global::DIMEGuidCompartmentDoubleSingleByte);
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
	CCompartment CompartmentIMEMode(pThreadMgr, _tfClientId, Global::DIMEGuidCompartmentIMEMode);
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

void CDIME::PrivateCompartmentsUpdated(_In_ ITfThreadMgr *pThreadMgr)
{
#if defined(DIME_USE_RUST_ENGINE)
	_PrivateCompartmentsUpdatedRust(pThreadMgr);
#else
	_PrivateCompartmentsUpdatedCpp(pThreadMgr);
#endif
}

void CDIME::_PrivateCompartmentsUpdatedCpp(_In_ ITfThreadMgr *pThreadMgr)
{
	debugPrint(L"CDIME::PrivateCompartmentsUpdated()");
    if (_pCompartmentConversion == nullptr)
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
    CCompartment CompartmentDoubleSingleByte(pThreadMgr, _tfClientId, Global::DIMEGuidCompartmentDoubleSingleByte);
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
		if(isDouble) _OnSwitchedToFullShapeCpp();
		else _OnSwitchedToHalfShapeCpp();
    }
}

//+---------------------------------------------------------------------------
//
// KeyboardOpenCompartmentUpdated
//
//----------------------------------------------------------------------------

void CDIME::KeyboardOpenCompartmentUpdated(_In_ ITfThreadMgr *pThreadMgr, _In_ REFGUID guidCompartment)
{
#if defined(DIME_USE_RUST_ENGINE)
	_KeyboardOpenCompartmentUpdatedRust(pThreadMgr, guidCompartment);
#else
	_KeyboardOpenCompartmentUpdatedCpp(pThreadMgr, guidCompartment);
#endif
}

void CDIME::_KeyboardOpenCompartmentUpdatedCpp(_In_ ITfThreadMgr *pThreadMgr, _In_ REFGUID guidCompartment)
{
	debugPrint(L"CDIME::KeyboardOpenCompartmentUpdated()");
    if (_pCompartmentConversion == nullptr)
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
    CCompartment CompartmentIMEMode(pThreadMgr, _tfClientId, Global::DIMEGuidCompartmentIMEMode);
	if(IsEqualGUID(guidCompartment, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE ))// Global::isWindows8 && check GUID_COMPARTMENT_KEYBOARD_OPENCLOSE in Windows 8.
	{
		
		CCompartment CompartmentKeyboardOpen(pThreadMgr, _tfClientId, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
		if (SUCCEEDED(CompartmentKeyboardOpen._GetCompartmentBOOL(isOpen)))
		{
			debugPrint(L"CDIME::KeyboardOpenCompartmentUpdated() GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, keyboard isOpen = %d", isOpen);
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
   			if(isOpen) _OnKeyboardOpenCpp();
			else _OnKeyboardClosedCpp();
			_showChnEngNotifyCpp(isOpen);
		}
	}
	isOpen = FALSE;
	if (IsEqualGUID(guidCompartment, Global::DIMEGuidCompartmentIMEMode) && SUCCEEDED(CompartmentIMEMode._GetCompartmentBOOL(isOpen)))
	{
		debugPrint(L"CDIME::_KeyboardOpenCompartmentUpdated() DIMEGuidCompartmentIMEMode, keyboard isOpen = %d", isOpen);
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
			if(isOpen) _OnKeyboardOpenCpp();
			else _OnKeyboardClosedCpp();
			_showChnEngNotifyCpp(isOpen);
		}
	
	}
	



   
}


void CDIME::OnKeyboardClosed()
{
#if defined(DIME_USE_RUST_ENGINE)
	_OnKeyboardClosedRust();
#else
	_OnKeyboardClosedCpp();
#endif
}

void CDIME::_OnKeyboardClosedCpp()
{
	debugPrint(L"CDIME::OnKeyboardClosed()\n");
	// switching to English (native) mode delete the phrase candidate window before exiting.
	_isChinese = FALSE;
	if(_IsComposing()) 
	{
		_EndComposition(_pContext);
	}
	_DeleteCandidateList(TRUE,_pContext);
}

void CDIME::OnKeyboardOpen()
{
#if defined(DIME_USE_RUST_ENGINE)
	_OnKeyboardOpenRust();
#else
	_OnKeyboardOpenCpp();
#endif
}

void CDIME::_OnKeyboardOpenCpp()
{
	debugPrint(L"CDIME::OnKeyboardOpen()\n");
	_isChinese = TRUE;
	// switching to Chinese mode
}

void CDIME::OnSwitchedToFullShape()
{
#if defined(DIME_USE_RUST_ENGINE)
	_OnSwitchedToFullShapeRust();
#else
	_OnSwitchedToFullShapeCpp();
#endif
}

void CDIME::_OnSwitchedToFullShapeCpp()
{
	debugPrint(L"CDIME::OnSwitchedToFullShape()\n");
	_isFullShape = TRUE;
	if(_IsComposing()) 
	{
		_EndComposition(_pContext);
	}
	_DeleteCandidateList(FALSE,_pContext);
	if (CConfig::GetDoubleSingleByteMode() != DOUBLE_SINGLE_BYTE_MODE::DOUBLE_SINGLE_BYTE_SHIFT_SPACE)
		CConfig::SetDoubleSingleByteMode(DOUBLE_SINGLE_BYTE_MODE::DOUBLE_SINGLE_BYTE_ALWAYS_DOUBLE);
}

void CDIME::OnSwitchedToHalfShape()
{
#if defined(DIME_USE_RUST_ENGINE)
	_OnSwitchedToHalfShapeRust();
#else
	_OnSwitchedToHalfShapeCpp();
#endif
}

void CDIME::_OnSwitchedToHalfShapeCpp()
{
	debugPrint(L"CDIME::OnSwitchedToHalfShape()\n");
	_isFullShape = FALSE;
	if(_IsComposing()) 
	{
		_EndComposition(_pContext);
	}
	_DeleteCandidateList(TRUE,_pContext);
	if (CConfig::GetDoubleSingleByteMode() != DOUBLE_SINGLE_BYTE_MODE::DOUBLE_SINGLE_BYTE_SHIFT_SPACE)
		CConfig::SetDoubleSingleByteMode(DOUBLE_SINGLE_BYTE_MODE::DOUBLE_SINGLE_BYTE_ALWAYS_SINGLE);
}

void CDIME::showChnEngNotify(BOOL isChinese, UINT delayShow)
{
#if defined(DIME_USE_RUST_ENGINE)
	_showChnEngNotifyRust(isChinese, delayShow);
#else
	_showChnEngNotifyCpp(isChinese, delayShow);
#endif
}
void CDIME::_showChnEngNotifyCpp(BOOL isChinese, UINT delayShow)
{
    // Remove _IsStoreAppMode for Win 11 explorer, notepad and paint using XAML inputs.
    if (CConfig::GetShowNotifyDesktop())
        _LangShowText(isChinese ? L"中文" : L"英數", 2, delayShow);
}
void CDIME::showFullHalfShapeNotify(BOOL isFullShape, UINT delayShow)
{
#if defined(DIME_USE_RUST_ENGINE)
	_showFullHalfShapeNotifyRust(isFullShape, delayShow);
#else
	_showFullHalfShapeNotifyCpp(isFullShape, delayShow);
#endif
}
void CDIME::_showFullHalfShapeNotifyCpp(BOOL isFullShape, UINT delayShow)
{
    // Always show — no compartment for full/half shape, notify is the only visual feedback
    _LangShowText(isFullShape ? L"全形" : L"半形", 2, delayShow);
}
void CDIME::showHanConvertNotify(BOOL isSimplified, UINT delayShow)
{
#if defined(DIME_USE_RUST_ENGINE)
	_showHanConvertNotifyRust(isSimplified, delayShow);
#else
	_showHanConvertNotifyCpp(isSimplified, delayShow);
#endif
}
void CDIME::_showHanConvertNotifyCpp(BOOL isSimplified, UINT delayShow)
{
    // Always show — no compartment for han convert, notify is the only visual feedback
    _LangShowText(isSimplified ? L"簡" : L"繁", 1, delayShow);
}

#if defined(DIME_USE_RUST_ENGINE) || (defined(DIME_UNIT_TESTING) && defined(DIME_RUST_AVAILABLE))
#include "dime_ffi.h"

static_assert(TF_CONVERSIONMODE_NATIVE == 0x0001, "TF_CONVERSIONMODE_NATIVE");
static_assert(TF_CONVERSIONMODE_FULLSHAPE == 0x0008, "TF_CONVERSIONMODE_FULLSHAPE");
static_assert(TF_MOD_LSHIFT == 0x0100, "TF_MOD_LSHIFT");
static_assert(TF_MOD_RSHIFT == 0x0020, "TF_MOD_RSHIFT");
static_assert((int)IME_SHIFT_MODE::IME_BOTH_SHIFT == 0, "IME_BOTH_SHIFT");
static_assert((int)DOUBLE_SINGLE_BYTE_MODE::DOUBLE_SINGLE_BYTE_SHIFT_SPACE == 0, "SHIFT_SPACE");
static_assert(CHN_ENG_NOTIFY_DELAY == 1500, "CHN_ENG_NOTIFY_DELAY");

struct DimeLangCtx
{
	CDIME* dime;
	CCompositionProcessorEngine* engine;
	ITfThreadMgr* thread;
	TfClientId client;
};

struct CLangBarHost
{
	static DimeLangCtx* Ctx(DimeLangHost* h)
	{
		return (h && h->ctx) ? static_cast<DimeLangCtx*>(h->ctx) : nullptr;
	}

	static int HasConversion(DimeLangHost* h)
	{
		DimeLangCtx* c = Ctx(h);
		return (c && c->dime && c->dime->_pCompartmentConversion) ? 1 : 0;
	}

	static int GetConversion(DimeLangHost* h, uint32_t* out)
	{
		DimeLangCtx* c = Ctx(h);
		if (!c || !c->dime || !c->dime->_pCompartmentConversion) return 0;
		DWORD mode = 0;
		HRESULT hr = c->dime->_pCompartmentConversion->_GetCompartmentDWORD(mode);
		if (FAILED(hr)) return 0;
		if (out) *out = mode;
		return 1;
	}

	static void SetConversion(DimeLangHost* h, uint32_t mode)
	{
		DimeLangCtx* c = Ctx(h);
		if (c && c->dime && c->dime->_pCompartmentConversion)
			c->dime->_pCompartmentConversion->_SetCompartmentDWORD(mode);
	}

	static HRESULT ReadBool(DimeLangCtx* c, int32_t id, BOOL& flag)
	{
		// A null thread matches CCompartment: the get returns S_OK and leaves the flag false.
		if (!c) return E_FAIL;
		if (id == 0)
		{
			CCompartment box(c->thread, c->client, Global::DIMEGuidCompartmentDoubleSingleByte);
			return box._GetCompartmentBOOL(flag);
		}
		if (id == 1)
		{
			CCompartment box(c->thread, c->client, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
			return box._GetCompartmentBOOL(flag);
		}
		if (id == 2)
		{
			CCompartment box(c->thread, c->client, Global::DIMEGuidCompartmentIMEMode);
			return box._GetCompartmentBOOL(flag);
		}
		return E_FAIL;
	}

	static int GetBool(DimeLangHost* h, int32_t id, int32_t* out)
	{
		BOOL flag = FALSE;
		HRESULT hr = ReadBool(Ctx(h), id, flag);
		if (FAILED(hr)) return 0;
		if (out) *out = flag ? 1 : 0;
		return 1;
	}

	static void SetBool(DimeLangHost* h, int32_t id, int32_t value)
	{
		DimeLangCtx* c = Ctx(h);
		if (!c || !c->thread) return;
		BOOL flag = value ? TRUE : FALSE;
		if (id == 0)
		{
			CCompartment box(c->thread, c->client, Global::DIMEGuidCompartmentDoubleSingleByte);
			box._SetCompartmentBOOL(flag);
		}
		else if (id == 1)
		{
			CCompartment box(c->thread, c->client, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
			box._SetCompartmentBOOL(flag);
		}
		else if (id == 2)
		{
			CCompartment box(c->thread, c->client, Global::DIMEGuidCompartmentIMEMode);
			box._SetCompartmentBOOL(flag);
		}
	}

	static int Windows8(DimeLangHost*)
	{
		return Global::isWindows8 ? 1 : 0;
	}

	static void SetChinese(DimeLangHost* h, int32_t on)
	{
		DimeLangCtx* c = Ctx(h);
		if (c && c->dime) c->dime->_isChinese = on ? TRUE : FALSE;
	}

	static void SetFullShape(DimeLangHost* h, int32_t on)
	{
		DimeLangCtx* c = Ctx(h);
		if (c && c->dime) c->dime->_isFullShape = on ? TRUE : FALSE;
	}

	static void EndComposition(DimeLangHost* h)
	{
		DimeLangCtx* c = Ctx(h);
		if (c && c->dime && c->dime->_IsComposing())
			c->dime->_EndComposition(c->dime->_pContext);
	}

	static void DeleteCandidate(DimeLangHost* h, int32_t force)
	{
		DimeLangCtx* c = Ctx(h);
		if (c && c->dime)
			c->dime->_DeleteCandidateList(force ? TRUE : FALSE, c->dime->_pContext);
	}

	static int WidthMode(DimeLangHost*)
	{
		return (int)CConfig::GetDoubleSingleByteMode();
	}

	static void SetWidthMode(DimeLangHost*, int32_t mode)
	{
		CConfig::SetDoubleSingleByteMode((DOUBLE_SINGLE_BYTE_MODE)mode);
	}

	static int NotifyDesktop(DimeLangHost*)
	{
		return CConfig::GetShowNotifyDesktop() ? 1 : 0;
	}

	static void ShowText(DimeLangHost* h, const uint16_t* text, size_t len, uint32_t delay)
	{
		DimeLangCtx* c = Ctx(h);
		if (c && c->dime)
			c->dime->_LangShowText((const WCHAR*)text, (UINT)len, delay);
	}

	static int ShiftMode(DimeLangHost*)
	{
		return (int)CConfig::GetIMEShiftMode();
	}

	static int ShiftKeyOnly(DimeLangHost* h)
	{
		DimeLangCtx* c = Ctx(h);
		if (!c || !c->engine) return 0;
		return c->engine->CheckShiftKeyOnly(&c->engine->_PreservedKey_IMEMode.TSFPreservedKeyTable) ? 1 : 0;
	}

	static uint32_t Modifiers(DimeLangHost*)
	{
		return Global::ModifiersValue;
	}

	static int DoHan(DimeLangHost*)
	{
		return CConfig::GetDoHanConvert() ? 1 : 0;
	}

	static void SetDoHan(DimeLangHost*, int32_t on)
	{
		CConfig::SetDoHanConvert(on ? TRUE : FALSE);
	}

	static void WriteConfigCb(DimeLangHost*)
	{
		CConfig::WriteConfig(Global::imeMode);
	}

	static void SetupHan(DimeLangHost* h)
	{
		DimeLangCtx* c = Ctx(h);
		if (c && c->engine) c->engine->SetupHanCovertTable();
	}

	static void InjectSpace(DimeLangHost* h)
	{
		DimeLangCtx* c = Ctx(h);
		if (!c || !c->thread || !c->engine) return;
		ITfDocumentMgr* doc = nullptr;
		ITfContext* context = nullptr;
		if (SUCCEEDED(c->thread->GetFocus(&doc)) && doc)
		{
			if (SUCCEEDED(doc->GetTop(&context)) && context)
			{
				_KEYSTROKE_STATE keyState;
				keyState.Category = KEYSTROKE_CATEGORY::CATEGORY_COMPOSING;
				keyState.Function = KEYSTROKE_FUNCTION::FUNCTION_SHIFT_ENGLISH_INPUT;
				if (c->engine->_pTextService)
				{
					CKeyHandlerEditSession* session = new (std::nothrow) CKeyHandlerEditSession(
						c->engine->_pTextService, context, VK_SPACE, L' ', keyState);
					if (session)
					{
						HRESULT hrSession = S_OK;
						context->RequestEditSession(c->engine->_pTextService->_GetClientId(), session,
							TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &hrSession);
						session->Release();
					}
				}
				context->Release();
			}
			doc->Release();
		}
	}

	static void ShowConfig(DimeLangHost* h)
	{
		DimeLangCtx* c = Ctx(h);
		if (c && c->engine && c->engine->_pTextService)
			c->engine->_pTextService->Show(GetForegroundWindow(), 0, GUID_NULL);
	}

	static int HasTextService(DimeLangHost* h)
	{
		DimeLangCtx* c = Ctx(h);
		return (c && c->engine && c->engine->_pTextService) ? 1 : 0;
	}

	static const DimeLangHostVtbl Vtbl;
};

const DimeLangHostVtbl CLangBarHost::Vtbl = {
	HasConversion,
	GetConversion,
	SetConversion,
	GetBool,
	SetBool,
	Windows8,
	SetChinese,
	SetFullShape,
	EndComposition,
	DeleteCandidate,
	WidthMode,
	SetWidthMode,
	NotifyDesktop,
	ShowText,
	ShiftMode,
	ShiftKeyOnly,
	Modifiers,
	DoHan,
	SetDoHan,
	WriteConfigCb,
	SetupHan,
	InjectSpace,
	ShowConfig,
	HasTextService,
};

static void BindLang(DimeLangCtx* ctx, DimeLangHost* ffi)
{
	ffi->vtbl = &CLangBarHost::Vtbl;
	ffi->ctx = ctx;
}

static DimeLangCtx MakeCtx(CDIME* dime, ITfThreadMgr* thread, TfClientId client)
{
	DimeLangCtx ctx;
	ctx.dime = dime;
	ctx.engine = dime ? dime->GetCompositionProcessorEngine() : nullptr;
	ctx.thread = thread;
	ctx.client = client;
	return ctx;
}

void CDIME::_ConversionModeCompartmentUpdatedRust(_In_ ITfThreadMgr* pThreadMgr, BOOL* setKeyboardOpenClose)
{
	DimeLangCtx ctx = MakeCtx(this, pThreadMgr, _tfClientId);
	DimeLangHost host;
	BindLang(&ctx, &host);
	int has = setKeyboardOpenClose != nullptr;
	int val = (has && *setKeyboardOpenClose) ? 1 : 0;
	dime_langbar_conversion(&host, has, val);
}

void CDIME::_PrivateCompartmentsUpdatedRust(_In_ ITfThreadMgr* pThreadMgr)
{
	DimeLangCtx ctx = MakeCtx(this, pThreadMgr, _tfClientId);
	DimeLangHost host;
	BindLang(&ctx, &host);
	dime_langbar_private(&host);
}

void CDIME::_KeyboardOpenCompartmentUpdatedRust(_In_ ITfThreadMgr* pThreadMgr, _In_ REFGUID guidCompartment)
{
	int which = -1;
	if (IsEqualGUID(guidCompartment, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE)) which = 0;
	else if (IsEqualGUID(guidCompartment, Global::DIMEGuidCompartmentIMEMode)) which = 1;
	DimeLangCtx ctx = MakeCtx(this, pThreadMgr, _tfClientId);
	DimeLangHost host;
	BindLang(&ctx, &host);
	dime_langbar_keyboard(&host, which);
}

void CDIME::_OnKeyboardClosedRust()
{
	DimeLangCtx ctx = MakeCtx(this, _pThreadMgr, _tfClientId);
	DimeLangHost host;
	BindLang(&ctx, &host);
	dime_langbar_keyboard_side(&host, 0);
}

void CDIME::_OnKeyboardOpenRust()
{
	DimeLangCtx ctx = MakeCtx(this, _pThreadMgr, _tfClientId);
	DimeLangHost host;
	BindLang(&ctx, &host);
	dime_langbar_keyboard_side(&host, 1);
}

void CDIME::_OnSwitchedToFullShapeRust()
{
	DimeLangCtx ctx = MakeCtx(this, _pThreadMgr, _tfClientId);
	DimeLangHost host;
	BindLang(&ctx, &host);
	dime_langbar_shape(&host, 1);
}

void CDIME::_OnSwitchedToHalfShapeRust()
{
	DimeLangCtx ctx = MakeCtx(this, _pThreadMgr, _tfClientId);
	DimeLangHost host;
	BindLang(&ctx, &host);
	dime_langbar_shape(&host, 0);
}

void CDIME::_showChnEngNotifyRust(BOOL isChinese, UINT delayShow)
{
	DimeLangCtx ctx = MakeCtx(this, _pThreadMgr, _tfClientId);
	DimeLangHost host;
	BindLang(&ctx, &host);
	dime_langbar_show(&host, 0, isChinese ? 1 : 0, delayShow);
}

void CDIME::_showFullHalfShapeNotifyRust(BOOL isFullShape, UINT delayShow)
{
	DimeLangCtx ctx = MakeCtx(this, _pThreadMgr, _tfClientId);
	DimeLangHost host;
	BindLang(&ctx, &host);
	dime_langbar_show(&host, 1, isFullShape ? 1 : 0, delayShow);
}

void CDIME::_showHanConvertNotifyRust(BOOL isSimplified, UINT delayShow)
{
	DimeLangCtx ctx = MakeCtx(this, _pThreadMgr, _tfClientId);
	DimeLangHost host;
	BindLang(&ctx, &host);
	dime_langbar_show(&host, 2, isSimplified ? 1 : 0, delayShow);
}

void CCompositionProcessorEngine::_OnPreservedKeyRust(REFGUID rguid, _Out_ BOOL* pIsEaten, _In_ ITfThreadMgr* pThreadMgr, TfClientId tfClientId)
{
	int id = -1;
	if (IsEqualGUID(rguid, _PreservedKey_IMEMode.Guid)) id = 0;
	else if (IsEqualGUID(rguid, _PreservedKey_DoubleSingleByte.Guid)) id = 1;
	else if (IsEqualGUID(rguid, _PreservedKey_Config.Guid)) id = 2;
	else if (IsEqualGUID(rguid, _PreservedKey_HanConvert.Guid)) id = 3;
	DimeLangCtx ctx;
	ctx.dime = _pTextService;
	ctx.engine = this;
	ctx.thread = pThreadMgr;
	ctx.client = tfClientId;
	DimeLangHost host;
	BindLang(&ctx, &host);
	if (pIsEaten)
		*pIsEaten = dime_langbar_preserved(&host, id) ? TRUE : FALSE;
}
#endif