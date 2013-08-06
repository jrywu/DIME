//
//
// Derived from Microsoft Sample IME by Jeremy '13,7,17
//
//
//#define DEBUG_PRINT

#include "Private.h"
#include "globals.h"
#include "TSFDayi.h"
#include "TSFDayiUIPresenter.h"
#include "CompositionProcessorEngine.h"
#include "Compartment.h"

//+---------------------------------------------------------------------------
//
// CreateInstance
//
//----------------------------------------------------------------------------

/* static */
HRESULT CTSFDayi::CreateInstance(_In_ IUnknown *pUnkOuter, REFIID riid, _Outptr_ void **ppvObj)
{
    CTSFDayi* pTSFDayi = nullptr;
    HRESULT hr = S_OK;

    if (ppvObj == nullptr)
    {
        return E_INVALIDARG;
    }

    *ppvObj = nullptr;

    if (nullptr != pUnkOuter)
    {
        return CLASS_E_NOAGGREGATION;
    }

    pTSFDayi = new (std::nothrow) CTSFDayi();
    if (pTSFDayi == nullptr)
    {
        return E_OUTOFMEMORY;
    }

    hr = pTSFDayi->QueryInterface(riid, ppvObj);

    pTSFDayi->Release();

    return hr;
}

//+---------------------------------------------------------------------------
//
// ctor
//
//----------------------------------------------------------------------------

CTSFDayi::CTSFDayi()
{
    DllAddRef();

    _pThreadMgr = nullptr;

    _threadMgrEventSinkCookie = TF_INVALID_COOKIE;

    _pTextEditSinkContext = nullptr;
    _textEditSinkCookie = TF_INVALID_COOKIE;

    _activeLanguageProfileNotifySinkCookie = TF_INVALID_COOKIE;

    _dwThreadFocusSinkCookie = TF_INVALID_COOKIE;

    _pComposition = nullptr;

    _pCompositionProcessorEngine = nullptr;

    _candidateMode = CANDIDATE_NONE;
    _pTSFDayiUIPresenter = nullptr;
    _isCandidateWithWildcard = FALSE;

    _pDocMgrLastFocused = nullptr;

    _pSIPIMEOnOffCompartment = nullptr;
    _dwSIPIMEOnOffCompartmentSinkCookie = 0;
    _msgWndHandle = nullptr;

    _pContext = nullptr;

    _refCount = 1;
	
	_phraseCandShowing = FALSE;
}

//+---------------------------------------------------------------------------
//
// dtor
//
//----------------------------------------------------------------------------

CTSFDayi::~CTSFDayi()
{
    if (_pTSFDayiUIPresenter)
    {
        delete _pTSFDayiUIPresenter;
        _pTSFDayiUIPresenter = nullptr;
    }
    DllRelease();
}

//+---------------------------------------------------------------------------
//
// QueryInterface
//
//----------------------------------------------------------------------------

STDAPI CTSFDayi::QueryInterface(REFIID riid, _Outptr_ void **ppvObj)
{
    if (ppvObj == nullptr)
    {
        return E_INVALIDARG;
    }

    *ppvObj = nullptr;

    if (IsEqualIID(riid, IID_IUnknown) ||
        IsEqualIID(riid, IID_ITfTextInputProcessor))
    {
        *ppvObj = (ITfTextInputProcessor *)this;
    }
    else if (IsEqualIID(riid, IID_ITfTextInputProcessorEx))
    {
        *ppvObj = (ITfTextInputProcessorEx *)this;
    }
    else if (IsEqualIID(riid, IID_ITfThreadMgrEventSink))
    {
        *ppvObj = (ITfThreadMgrEventSink *)this;
    }
    else if (IsEqualIID(riid, IID_ITfTextEditSink))
    {
        *ppvObj = (ITfTextEditSink *)this;
    }
    else if (IsEqualIID(riid, IID_ITfKeyEventSink))
    {
        *ppvObj = (ITfKeyEventSink *)this;
    }
    else if (IsEqualIID(riid, IID_ITfActiveLanguageProfileNotifySink))
    {
        *ppvObj = (ITfActiveLanguageProfileNotifySink *)this;
    }
    else if (IsEqualIID(riid, IID_ITfCompositionSink))
    {
        *ppvObj = (ITfKeyEventSink *)this;
    }
    else if (IsEqualIID(riid, IID_ITfDisplayAttributeProvider))
    {
        *ppvObj = (ITfDisplayAttributeProvider *)this;
    }
    else if (IsEqualIID(riid, IID_ITfThreadFocusSink))
    {
        *ppvObj = (ITfThreadFocusSink *)this;
    }
    else if (IsEqualIID(riid, IID_ITfFunctionProvider))
    {
        *ppvObj = (ITfFunctionProvider *)this;
    }
    //else if (IsEqualIID(riid, IID_ITfFunction))
    //{
    //    *ppvObj = (ITfFunction *)this;
    //}
    else if (IsEqualIID(riid, IID_ITfFnGetPreferredTouchKeyboardLayout))
    {
        *ppvObj = (ITfFnGetPreferredTouchKeyboardLayout *)this;
    }
	else if (IsEqualIID(riid, IID_ITfFnConfigure))
    {
        *ppvObj = (ITfFnConfigure *)this;
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
// AddRef
//
//----------------------------------------------------------------------------

STDAPI_(ULONG) CTSFDayi::AddRef()
{
    return ++_refCount;
}

//+---------------------------------------------------------------------------
//
// Release
//
//----------------------------------------------------------------------------

STDAPI_(ULONG) CTSFDayi::Release()
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
// ITfTextInputProcessorEx::ActivateEx
//
//----------------------------------------------------------------------------

STDAPI CTSFDayi::ActivateEx(ITfThreadMgr *pThreadMgr, TfClientId tfClientId, DWORD dwFlags)
{
    _pThreadMgr = pThreadMgr;
    _pThreadMgr->AddRef();

    _tfClientId = tfClientId;
    _dwActivateFlags = dwFlags;

    if (!_InitThreadMgrEventSink())
    {
        goto ExitError;
    }

    ITfDocumentMgr* pDocMgrFocus = nullptr;
    if (SUCCEEDED(_pThreadMgr->GetFocus(&pDocMgrFocus)) && (pDocMgrFocus != nullptr))
    {
        _InitTextEditSink(pDocMgrFocus);
        pDocMgrFocus->Release();
    }

    if (!_InitKeyEventSink())
    {
        goto ExitError;
    }

    if (!_InitActiveLanguageProfileNotifySink())
    {
        goto ExitError;
    }

    if (!_InitThreadFocusSink())
    {
        goto ExitError;
    }

    if (!_InitDisplayAttributeGuidAtom())
    {
        goto ExitError;
    }

    if (!_InitFunctionProviderSink())
    {
        goto ExitError;
    }

    if (!_AddTextProcessorEngine())
    {
        goto ExitError;
    }

    return S_OK;

ExitError:
    Deactivate();
    return E_FAIL;
}

//+---------------------------------------------------------------------------
//
// ITfTextInputProcessorEx::Deactivate
//
//----------------------------------------------------------------------------

STDAPI CTSFDayi::Deactivate()
{
	debugPrint(L"CTSFDayi::Deactivate");
    if (_pCompositionProcessorEngine)
    {
        delete _pCompositionProcessorEngine;
        _pCompositionProcessorEngine = nullptr;
    }

    ITfContext* pContext = _pContext;
    if (_pContext)
    {   
        pContext->AddRef();
        _EndComposition(_pContext);
    }

    if (_pTSFDayiUIPresenter)
    {
        delete _pTSFDayiUIPresenter;
        _pTSFDayiUIPresenter = nullptr;

        if (pContext)
        {
            pContext->Release();
        }

        _candidateMode = CANDIDATE_NONE;
        _isCandidateWithWildcard = FALSE;
    }

    _UninitFunctionProviderSink();

    _UninitThreadFocusSink();

    _UninitActiveLanguageProfileNotifySink();

    _UninitKeyEventSink();

    _UninitThreadMgrEventSink();

	if(Global::isWindows8){
		CCompartment CompartmentKeyboardOpen(_pThreadMgr, _tfClientId, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
		CompartmentKeyboardOpen._ClearCompartment();
	}

	CCompartment CompartmentIMEMode(_pThreadMgr, _tfClientId, Global::TSFDayiGuidCompartmentIMEMode);
    CompartmentIMEMode._ClearCompartment();

    CCompartment CompartmentDoubleSingleByte(_pThreadMgr, _tfClientId, Global::TSFDayiGuidCompartmentDoubleSingleByte);
    CompartmentDoubleSingleByte._ClearCompartment();

    
    if (_pThreadMgr != nullptr)
    {
        _pThreadMgr->Release();
    }

    _tfClientId = TF_CLIENTID_NULL;

    if (_pDocMgrLastFocused)
    {
        _pDocMgrLastFocused->Release();
		_pDocMgrLastFocused = nullptr;
    }

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfFunctionProvider::GetType
//
//----------------------------------------------------------------------------
HRESULT CTSFDayi::GetType(__RPC__out GUID *pguid)
{
    HRESULT hr = E_INVALIDARG;
    if (pguid)
    {
        *pguid = Global::TSFDayiCLSID;
        hr = S_OK;
    }
    return hr;
}

//+---------------------------------------------------------------------------
//
// ITfFunctionProvider::::GetDescription
//
//----------------------------------------------------------------------------
HRESULT CTSFDayi::GetDescription(__RPC__deref_out_opt BSTR *pbstrDesc)
{
    HRESULT hr = E_INVALIDARG;
    if (pbstrDesc != nullptr)
    {
        *pbstrDesc = nullptr;
        hr = E_NOTIMPL;
    }
    return hr;
}

//+---------------------------------------------------------------------------
//
// ITfFunctionProvider::::GetFunction
//
//----------------------------------------------------------------------------
HRESULT CTSFDayi::GetFunction(__RPC__in REFGUID rguid, __RPC__in REFIID riid, __RPC__deref_out_opt IUnknown **ppunk)
{
    HRESULT hr = E_NOINTERFACE;

    if ((IsEqualGUID(rguid, GUID_NULL)) 
        && (IsEqualGUID(riid, __uuidof(ITfFnSearchCandidateProvider))))
    {
        hr = _pITfFnSearchCandidateProvider->QueryInterface(riid, (void**)ppunk);
    }
    else if (IsEqualGUID(rguid, GUID_NULL))
    {
        hr = QueryInterface(riid, (void **)ppunk);
    }

    return hr;
}

//+---------------------------------------------------------------------------
//
// ITfFunction::GetDisplayName
//
//----------------------------------------------------------------------------
HRESULT CTSFDayi::GetDisplayName(_Out_ BSTR *pbstrDisplayName)
{
	BSTR bstrName;

	if(pbstrDisplayName == NULL)
	{
		return E_INVALIDARG;
	}

	*pbstrDisplayName = NULL;

	bstrName = SysAllocString(L"TSFDayi 1.0");

	if(bstrName == NULL)
	{
		return E_OUTOFMEMORY;
	}

	*pbstrDisplayName = bstrName;

	return S_OK;

}

//+---------------------------------------------------------------------------
//
// ITfFnGetPreferredTouchKeyboardLayout::GetLayout
// The tkblayout will be Optimized layout.
//----------------------------------------------------------------------------
HRESULT CTSFDayi::GetLayout(_Out_ TKBLayoutType *ptkblayoutType, _Out_ WORD *pwPreferredLayoutId)
{
    HRESULT hr = E_INVALIDARG;
    if ((ptkblayoutType != nullptr) && (pwPreferredLayoutId != nullptr))
    {
        *ptkblayoutType = TKBLT_CLASSIC;
        *pwPreferredLayoutId = TKBL_CLASSIC_TRADITIONAL_CHINESE_DAYI;
        hr = S_OK;
    }
    return hr;
}

//+---------------------------------------------------------------------------
//
// ITfFnConfigure::Show
//
//----------------------------------------------------------------------------

HRESULT CTSFDayi::Show(_In_ HWND hwndParent, _In_ LANGID langid, _In_ REFGUID rguidProfile)
{
	langid;
	rguidProfile;
	MessageBox(hwndParent, L"config call", L"TSFDayi", NULL);
	return S_OK;
}
//+---------------------------------------------------------------------------
//
// ITfFnShowHelp::Show
//
//----------------------------------------------------------------------------
HRESULT CTSFDayi::Show(_In_ HWND hwndParent)
{
        
	MessageBox(hwndParent, L"Show help call", L"TSFDayi", NULL);
        return S_OK;
}


void CTSFDayi::OnKeyboardClosed()
{
	debugPrint(L"CTSFDayi::OnKeyboardClosed()\n");
	// switching to English (native) mode delete the phrase candidate window before exting.
	if(_IsComposing()) 
		_EndComposition(_pContext);
	_DeleteCandidateList(FALSE, NULL);
	CStringRange notifyText;
	ShowNotifyText(&notifyText.Set(L"英文", 2));
}

void CTSFDayi::OnKeyboardOpen()
{
	debugPrint(L"CTSFDayi::OnKeyboardOpen()\n");
	// switching to Chinese mode
	CStringRange notifyText;
	ShowNotifyText(&notifyText.Set(L"中文", 2));
	
}

void CTSFDayi::OnSwitchedToFullShape()
{
	debugPrint(L"CTSFDayi::OnSwitchedToFullShape()\n");
	if(_IsComposing()) 
		_EndComposition(_pContext);
	_DeleteCandidateList(FALSE, NULL);
	CStringRange notifyText;
	ShowNotifyText(&notifyText.Set(L"全形", 2));
	
}

void CTSFDayi::OnSwitchedToHalfShape()
{
	debugPrint(L"CTSFDayi::OnSwitchedToHalfShape()\n");
	if(_IsComposing()) 
		_EndComposition(_pContext);
	_DeleteCandidateList(FALSE, NULL);
	CStringRange notifyText;
	ShowNotifyText(&notifyText.Set(L"半形", 2));
	
}


HRESULT CTSFDayi::ShowNotifyText(CStringRange *pNotifyText)
{
	
	HRESULT hr = S_OK;

	ITfThreadMgr* pThreadMgr = nullptr;
    ITfDocumentMgr* pDocumentMgr = nullptr;
    ITfContext* pContext = nullptr;
	
	if(_pTSFDayiUIPresenter == nullptr)
    {
		_pTSFDayiUIPresenter = new (std::nothrow) CTSFDayiUIPresenter(this, _pCompositionProcessorEngine);
		if (!_pTSFDayiUIPresenter)
        {
            return E_OUTOFMEMORY;
        }	
    }
	pThreadMgr = _GetThreadMgr();
    if (nullptr == pThreadMgr)
    {
        goto Exit;
    }

    hr = pThreadMgr->GetFocus(&pDocumentMgr);
    if (FAILED(hr))
    {
        goto Exit;
    }

    hr = pDocumentMgr->GetTop(&pContext);
    if (FAILED(hr))
    {
        pDocumentMgr->Release();
        goto Exit;
    }

    _pTSFDayiUIPresenter->ShowNotifyText(pContext, pNotifyText);
Exit:
	return hr;
}