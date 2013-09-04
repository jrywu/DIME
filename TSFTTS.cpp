//
//
// Derived from Microsoft Sample IME by Jeremy '13,7,17
//
//
//#define DEBUG_PRINT

#include "Private.h"
#include "globals.h"
#include "TSFTTS.h"
#include "UIPresenter.h"
#include "CompositionProcessorEngine.h"
#include "Compartment.h"
#include "RegKey.h"
#include "TfInputProcessorProfile.h"
#include <Prsht.h>

//////////////////////////////////////////////////////////////////////
//
// CTSFTTS implementation.
//
//////////////////////////////////////////////////////////////////////+---------------------------------------------------------------------------
//
// _AddTextProcessorEngine
//
//----------------------------------------------------------------------------

DWORD CTSFTTS::_dwActivateFlags =0;

BOOL CTSFTTS::_AddTextProcessorEngine(LANGID inLangID, GUID inGuidProfile)
 {
	debugPrint(L"CTSFTTS::_AddTextProcessorEngine()");
    LANGID langid = inLangID;
    GUID guidProfile = inGuidProfile;

	if(langid ==0 || guidProfile == GUID_NULL)
	{
		 CLSID clsid = GUID_NULL;
		// Get default profile.
		CTfInputProcessorProfile profile;

		if (FAILED(profile.CreateInstance()))
		{
			return FALSE;
		}

		if (FAILED(profile.GetCurrentLanguage(&langid)))
		{
			return FALSE;
		}

		if((FAILED(profile.GetDefaultLanguageProfile(langid, GUID_TFCAT_TIP_KEYBOARD, &clsid, &guidProfile))))
		{
			return FALSE;
		}
		

	}

    if (_pCompositionProcessorEngine != nullptr)
    {
       
        if ((langid == _langid) && IsEqualGUID(guidProfile, _guidProfile))
        {
			debugPrint(L"CTSFTTS::_AddTextProcessorEngine() _pCompositionProcessorEngine with the coming guidProfile exist, return true.");
            return TRUE;
        }
		else
		{
			debugPrint(L"CTSFTTS::_AddTextProcessorEngine() _pCompositionProcessorEngine with the diff. guidProfile exist, recreate one.");
			if(_pUIPresenter) _pUIPresenter->ClearAll();

			_UninitFunctionProviderSink();  // reset the function provider sink to get updated UI less candidate provider
			_InitFunctionProviderSink();
			
		}
    }
	
    // Create composition processor engine
    if (_pCompositionProcessorEngine == nullptr)
    {
		debugPrint(L"CTSFTTS::_AddTextProcessorEngine() create new CompositionProcessorEngine .");
        _pCompositionProcessorEngine = new (std::nothrow) CCompositionProcessorEngine(this);
    }
    if (!_pCompositionProcessorEngine)
    {
        return FALSE;
    }


	if(_pUIPresenter == nullptr)
	{
		debugPrint(L"CTSFTTS::_AddTextProcessorEngine() create new UIPresenter .");
		_pUIPresenter = new (std::nothrow) CUIPresenter(this, _pCompositionProcessorEngine);
	}
	if (_pUIPresenter == nullptr)
	{
		return FALSE;    
	}

    // setup composition processor engine
    if (FALSE == SetupLanguageProfile(langid, guidProfile, _GetThreadMgr(), _GetClientId(), _IsSecureMode()))
    {
        return FALSE;
    }


    return TRUE;
}

//+---------------------------------------------------------------------------
//
// CreateInstance
//
//----------------------------------------------------------------------------

/* static */
HRESULT CTSFTTS::CreateInstance(_In_ IUnknown *pUnkOuter, REFIID riid, _Outptr_ void **ppvObj)
{
    CTSFTTS* pTSFTTS = nullptr;
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

    pTSFTTS = new (std::nothrow) CTSFTTS();
    if (pTSFTTS == nullptr)
    {
        return E_OUTOFMEMORY;
    }

    hr = pTSFTTS->QueryInterface(riid, ppvObj);

    pTSFTTS->Release();

    return hr;
}

//+---------------------------------------------------------------------------
//
// ctor
//
//----------------------------------------------------------------------------

CTSFTTS::CTSFTTS()
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
    _pUIPresenter = nullptr;
    _isCandidateWithWildcard = FALSE;

    _pDocMgrLastFocused = nullptr;

    _pSIPIMEOnOffCompartment = nullptr;
    _dwSIPIMEOnOffCompartmentSinkCookie = 0;
    _msgWndHandle = nullptr;

    _pContext = nullptr;

    _refCount = 1;
	

	_pLanguageBar_IMEModeW8 = nullptr;
    _pLanguageBar_IMEMode = nullptr;
    _pLanguageBar_DoubleSingleByte = nullptr;

	_langid = 1028; // CHT

    _pCompartmentConversion = nullptr;
	_pCompartmentIMEModeEventSink = nullptr;
    _pCompartmentKeyboardOpenEventSink = nullptr;
    _pCompartmentConversionEventSink = nullptr;
    _pCompartmentDoubleSingleByteEventSink = nullptr;
	
	for (UINT i =0 ; i<5 ; i++)
	{
		_pReverseConversion[i] = nullptr;
		_pITfReverseConversion[i] = nullptr;
	}

	_isChinese = FALSE;
	_isFullShape = FALSE;

}

//+---------------------------------------------------------------------------
//
// dtor
//
//----------------------------------------------------------------------------

CTSFTTS::~CTSFTTS()
{
	if (_pUIPresenter)
    {
        delete _pUIPresenter;
        _pUIPresenter = nullptr;
    }

	ReleaseReverseConversion();

    DllRelease();
}

//+---------------------------------------------------------------------------
//
// QueryInterface
//
//----------------------------------------------------------------------------

STDAPI CTSFTTS::QueryInterface(REFIID riid, _Outptr_ void **ppvObj)
{
	debugPrint(L"\nCTSFTTS::QueryInterface()");
    if (ppvObj == nullptr)
    {
        return E_INVALIDARG;
    }

    *ppvObj = nullptr;

    if (IsEqualIID(riid, IID_IUnknown) ||
        IsEqualIID(riid, IID_ITfTextInputProcessor))
    {
		debugPrint(L"CTSFTTS::QueryInterface() :IID_IUnknown, IID_ITfTextInputProcessor");
        *ppvObj = (ITfTextInputProcessor *)this;
    }
    else if (IsEqualIID(riid, IID_ITfTextInputProcessorEx))
    {
		debugPrint(L"CTSFTTS::QueryInterface() :IID_ITfTextInputProcessorEx");
        *ppvObj = (ITfTextInputProcessorEx *)this;
    }
    else if (IsEqualIID(riid, IID_ITfThreadMgrEventSink))
    {
		debugPrint(L"CTSFTTS::QueryInterface() :IID_ITfThreadMgrEventSink");
        *ppvObj = (ITfThreadMgrEventSink *)this;
    }
    else if (IsEqualIID(riid, IID_ITfTextEditSink))
    {
		debugPrint(L"CTSFTTS::QueryInterface() :IID_ITfTextEditSink");
        *ppvObj = (ITfTextEditSink *)this;
    }
    else if (IsEqualIID(riid, IID_ITfKeyEventSink))
    {
		debugPrint(L"CTSFTTS::QueryInterface() :IID_ITfKeyEventSink");
        *ppvObj = (ITfKeyEventSink *)this;
    }
    else if (IsEqualIID(riid, IID_ITfActiveLanguageProfileNotifySink))
    {
		debugPrint(L"CTSFTTS::QueryInterface() :IID_ITfActiveLanguageProfileNotifySink");
        *ppvObj = (ITfActiveLanguageProfileNotifySink *)this;
    }
    else if (IsEqualIID(riid, IID_ITfCompositionSink))
    {
		debugPrint(L"CTSFTTS::QueryInterface() :IID_ITfCompositionSink");
        *ppvObj = (ITfKeyEventSink *)this;
    }
    else if (IsEqualIID(riid, IID_ITfDisplayAttributeProvider))
    {
		debugPrint(L"CTSFTTS::QueryInterface() :IID_ITfDisplayAttributeProvider");
        *ppvObj = (ITfDisplayAttributeProvider *)this;
    }
    else if (IsEqualIID(riid, IID_ITfThreadFocusSink))
    {
		debugPrint(L"CTSFTTS::QueryInterface() :IID_ITfThreadFocusSink");
        *ppvObj = (ITfThreadFocusSink *)this;
    }
    else if (IsEqualIID(riid, IID_ITfFunctionProvider))
    {
		debugPrint(L"CTSFTTS::QueryInterface() :IID_ITfFunctionProvider");
        *ppvObj = (ITfFunctionProvider *)this;
    }
    else if (IsEqualIID(riid, IID_ITfFnGetPreferredTouchKeyboardLayout))
    {
		debugPrint(L"CTSFTTS::QueryInterface() :IID_ITfFnGetPreferredTouchKeyboardLayout");
        *ppvObj = (ITfFnGetPreferredTouchKeyboardLayout *)this;
    }
	else if (IsEqualIID(riid, IID_ITfFnConfigure))
    {
		debugPrint(L"CTSFTTS::QueryInterface() :IID_ITfFnConfigure");
        *ppvObj = (ITfFnConfigure *)this;
    }
	else if (IsEqualIID(riid, IID_ITfReverseConversionMgr ))
    {
		debugPrint(L"CTSFTTS::QueryInterface() :IID_ITfReverseConversionMgr");
        *ppvObj = (ITfReverseConversionMgr  *)this;
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

STDAPI_(ULONG) CTSFTTS::AddRef()
{
	debugPrint(L"CTSFTTS::AddRef(), _refCount = %d", _refCount+1); 
    return ++_refCount;
}

//+---------------------------------------------------------------------------
//
// Release
//
//----------------------------------------------------------------------------

STDAPI_(ULONG) CTSFTTS::Release()
{
	debugPrint(L"CTSFTTS::Release(), _refCount = %d", _refCount-1);
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

STDAPI CTSFTTS::ActivateEx(ITfThreadMgr *pThreadMgr, TfClientId tfClientId, DWORD dwFlags)
{
	debugPrint(L"CTSFTTS::ActivateEx(); ITfTextInputProcessorEx::ActivateEx()");
    _pThreadMgr = pThreadMgr;
    _pThreadMgr->AddRef();

    _tfClientId = tfClientId;
    _dwActivateFlags = dwFlags;


	debugPrint(L"CTSFTTS::ActivateEx(); _InitThreadMgrEventSink.");
    if (!_InitThreadMgrEventSink())
    {
		debugPrint(L"CTSFTTS::ActivateEx(); _InitThreadMgrEventSink failed.");
        goto ExitError;
    }

	
    ITfDocumentMgr* pDocMgrFocus = nullptr;
    if (SUCCEEDED(_pThreadMgr->GetFocus(&pDocMgrFocus)) && (pDocMgrFocus != nullptr))
    {
		debugPrint(L"CTSFTTS::ActivateEx(); _InitTextEditSink.");
        _InitTextEditSink(pDocMgrFocus);
        pDocMgrFocus->Release();
    }


	debugPrint(L"CTSFTTS::ActivateEx(); _InitKeyEventSink.");
    if (!_InitKeyEventSink())
    {
		debugPrint(L"CTSFTTS::ActivateEx(); _InitKeyEventSink failed.");
        goto ExitError;
    }

	debugPrint(L"CTSFTTS::ActivateEx(); _InitActiveLanguageProfileNotifySink.");
    if (!_InitActiveLanguageProfileNotifySink())
    {
		debugPrint(L"CTSFTTS::ActivateEx(); _InitActiveLanguageProfileNotifySink failed.");
        goto ExitError;
    }

	debugPrint(L"CTSFTTS::ActivateEx(); _InitThreadFocusSink.");
    if (!_InitThreadFocusSink())
    {
		debugPrint(L"CTSFTTS::ActivateEx(); _InitThreadFocusSink failed.");
        goto ExitError;
    }

	debugPrint(L"CTSFTTS::ActivateEx(); _InitDisplayAttributeGuidAtom.");
    if (!_InitDisplayAttributeGuidAtom())
    {
		debugPrint(L"CTSFTTS::ActivateEx(); _InitDisplayAttributeGuidAtom failed.");
        goto ExitError;
    }

	debugPrint(L"CTSFTTS::ActivateEx(); _InitFunctionProviderSink.");
    if (!_InitFunctionProviderSink())
    {
		debugPrint(L"CTSFTTS::ActivateEx(); _InitFunctionProviderSink failed.");
        goto ExitError;
    }

	debugPrint(L"CTSFTTS::ActivateEx(); _AddTextProcessorEngine.");
    if (!_AddTextProcessorEngine())
    {
		debugPrint(L"CTSFTTS::ActivateEx(); _AddTextProcessorEngine failed.");
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

STDAPI CTSFTTS::Deactivate()
{
	debugPrint(L"CTSFTTS::Deactivate()");
    if (_pCompositionProcessorEngine)
    {
        delete _pCompositionProcessorEngine;
		_pCompositionProcessorEngine = nullptr;

		//originall do this in deconstructor of compositionprocessorengine
		if (_pLanguageBar_IMEModeW8 && Global::isWindows8)
		{
			_pLanguageBar_IMEModeW8->CleanUp();
			_pLanguageBar_IMEModeW8->Release();
			_pLanguageBar_IMEModeW8 = nullptr;
		}


		if (_pLanguageBar_IMEMode)
		{
			_pLanguageBar_IMEMode->CleanUp();
			_pLanguageBar_IMEMode->Release();
			_pLanguageBar_IMEMode = nullptr;
		}
		if (_pLanguageBar_DoubleSingleByte)
		{
			_pLanguageBar_DoubleSingleByte->CleanUp();
			_pLanguageBar_DoubleSingleByte->Release();
			_pLanguageBar_DoubleSingleByte = nullptr;
		}

		if (_pCompartmentConversion)
		{
			delete _pCompartmentConversion;
			_pCompartmentConversion = nullptr;
		}
		if (_pCompartmentKeyboardOpenEventSink && Global::isWindows8)
		{
			_pCompartmentKeyboardOpenEventSink->_Unadvise();
			delete _pCompartmentKeyboardOpenEventSink;
			_pCompartmentKeyboardOpenEventSink = nullptr;
		}
		if (_pCompartmentIMEModeEventSink)
		{
			_pCompartmentIMEModeEventSink->_Unadvise();
			delete _pCompartmentIMEModeEventSink;
			_pCompartmentIMEModeEventSink = nullptr;
		}
		if (_pCompartmentConversionEventSink)
		{
			_pCompartmentConversionEventSink->_Unadvise();
			delete _pCompartmentConversionEventSink;
			_pCompartmentConversionEventSink = nullptr;
		}
		if (_pCompartmentDoubleSingleByteEventSink)
		{
			_pCompartmentDoubleSingleByteEventSink->_Unadvise();
			delete _pCompartmentDoubleSingleByteEventSink;
			_pCompartmentDoubleSingleByteEventSink = nullptr;
		}


	}

	if(_pITfReverseConversion[Global::imeMode])
		{
			_pITfReverseConversion[Global::imeMode]->Release();
			_pITfReverseConversion[Global::imeMode] = nullptr;
		}

    ITfContext* pContext = _pContext;
    if (_pContext)
    {   
        pContext->AddRef();
        _EndComposition(_pContext);
    }

    if (_pUIPresenter)
    {
        delete _pUIPresenter;
        _pUIPresenter = nullptr;

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

	_UnInitTextEditSink();

    _UninitThreadMgrEventSink();

	HideAllLanguageBarIcons();

	CCompartment CompartmentIMEMode(_pThreadMgr, _tfClientId, Global::TSFTTSGuidCompartmentIMEMode);
    CompartmentIMEMode._ClearCompartment();


    CCompartment CompartmentDoubleSingleByte(_pThreadMgr, _tfClientId, Global::TSFTTSGuidCompartmentDoubleSingleByte);
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
HRESULT CTSFTTS::GetType(__RPC__out GUID *pguid)
{
    HRESULT hr = E_INVALIDARG;
    if (pguid)
    {
        *pguid = Global::TSFTTSCLSID;
        hr = S_OK;
    }
    return hr;
}

//+---------------------------------------------------------------------------
//
// ITfFunctionProvider::::GetDescription
//
//----------------------------------------------------------------------------
HRESULT CTSFTTS::GetDescription(__RPC__deref_out_opt BSTR *pbstrDesc)
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
HRESULT CTSFTTS::GetFunction(__RPC__in REFGUID rguid, __RPC__in REFIID riid, __RPC__deref_out_opt IUnknown **ppunk)
{
	debugPrint(L"CTSFTTS::GetFunction()");
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
HRESULT CTSFTTS::GetDisplayName(_Out_ BSTR *pbstrDisplayName)
{
	debugPrint(L"CTSFTTS::GetDisplayName()");
	BSTR bstrName;

	if(pbstrDisplayName == NULL)
	{
		return E_INVALIDARG;
	}

	*pbstrDisplayName = NULL;

	
	bstrName = new (std::nothrow) WCHAR[64];
	if(Global::imeMode == IME_MODE_DAYI)
		LoadString(Global::dllInstanceHandle, IDS_DAYI_DESCRIPTION, bstrName, 64);
	if(Global::imeMode == IME_MODE_ARRAY)
		LoadString(Global::dllInstanceHandle, IDS_ARRAY_DESCRIPTION, bstrName, 64);
	if(Global::imeMode == IME_MODE_PHONETIC)
		LoadString(Global::dllInstanceHandle, IDS_PHONETIC_DESCRIPTION, bstrName, 64);
	if(Global::imeMode == IME_MODE_GENERIC)
		LoadString(Global::dllInstanceHandle, IDS_GENERIC_DESCRIPTION, bstrName, 64);

	if(bstrName == NULL)	return E_OUTOFMEMORY;
	
	*pbstrDisplayName = bstrName;

	return S_OK;

}

//+---------------------------------------------------------------------------
//
// ITfFnGetPreferredTouchKeyboardLayout::GetLayout
// The tkblayout will be Optimized layout.
//----------------------------------------------------------------------------
HRESULT CTSFTTS::GetLayout(_Out_ TKBLayoutType *ptkblayoutType, _Out_ WORD *pwPreferredLayoutId)
{
	debugPrint(L"CTSFTTS::GetLayout()");
    HRESULT hr = E_INVALIDARG;
    if ((ptkblayoutType != nullptr) && (pwPreferredLayoutId != nullptr))
    {
		if(Global::imeMode==IME_MODE_DAYI)
		{
			*ptkblayoutType = TKBLT_CLASSIC;
			*pwPreferredLayoutId = TKBL_CLASSIC_TRADITIONAL_CHINESE_DAYI;
		}
		else if(Global::imeMode==IME_MODE_PHONETIC)
		{
			*ptkblayoutType = TKBLT_CLASSIC;
			*pwPreferredLayoutId = TKBL_CLASSIC_TRADITIONAL_CHINESE_PHONETIC;
		}
		else
		{
			*ptkblayoutType = TKBLT_CLASSIC;
			*pwPreferredLayoutId = TKBL_UNDEFINED;
		}
        hr = S_OK;
    }
    return hr;
}


/*/+---------------------------------------------------------------------------
//
// ITfFnShowHelp::Show
//
//----------------------------------------------------------------------------
HRESULT CTSFTTS::Show(_In_ HWND hwndParent)
{  
	hwndParent;
	//MessageBox(hwndParent, L"Show help call", L"TSFTTS", NULL);
        return S_OK;
}*/




//+---------------------------------------------------------------------------
//
// CTSFTTS::CreateInstance 
//
//----------------------------------------------------------------------------

HRESULT CTSFTTS::CreateInstance(REFCLSID rclsid, REFIID riid, _Outptr_result_maybenull_ LPVOID* ppv, _Out_opt_ HINSTANCE* phInst, BOOL isComLessMode)
{
    HRESULT hr = S_OK;
    if (phInst == nullptr)
    {
        return E_INVALIDARG;
    }

    *phInst = nullptr;

    if (!isComLessMode)
    {
        hr = ::CoCreateInstance(rclsid, 
            NULL, 
            CLSCTX_INPROC_SERVER,
            riid,
            ppv);
    }
    else
    {
        hr = CTSFTTS::ComLessCreateInstance(rclsid, riid, ppv, phInst);
    }

    return hr;
	}

//+---------------------------------------------------------------------------
//
// CTSFTTS::ComLessCreateInstance
//
//----------------------------------------------------------------------------

HRESULT CTSFTTS::ComLessCreateInstance(REFGUID rclsid, REFIID riid, _Outptr_result_maybenull_ void **ppv, _Out_opt_ HINSTANCE *phInst)
{
    HRESULT hr = S_OK;
    HINSTANCE TSFTTSDllHandle = nullptr;
    WCHAR wchPath[MAX_PATH] = {'\0'};
    WCHAR szExpandedPath[MAX_PATH] = {'\0'};
    DWORD dwCnt = 0;
    *ppv = nullptr;

    hr = phInst ? S_OK : E_FAIL;
    if (SUCCEEDED(hr))
    {
        *phInst = nullptr;
        hr = CTSFTTS::GetComModuleName(rclsid, wchPath, ARRAYSIZE(wchPath));
        if (SUCCEEDED(hr))
        {
            dwCnt = ExpandEnvironmentStringsW(wchPath, szExpandedPath, ARRAYSIZE(szExpandedPath));
            hr = (0 < dwCnt && dwCnt <= ARRAYSIZE(szExpandedPath)) ? S_OK : E_FAIL;
            if (SUCCEEDED(hr))
            {
                TSFTTSDllHandle = LoadLibraryEx(szExpandedPath, NULL, 0);
                hr = TSFTTSDllHandle ? S_OK : E_FAIL;
                if (SUCCEEDED(hr))
                {
                    *phInst = TSFTTSDllHandle;
                    FARPROC pfn = GetProcAddress(TSFTTSDllHandle, "DllGetClassObject");
                    hr = pfn ? S_OK : E_FAIL;
                    if (SUCCEEDED(hr))
                    {
                        IClassFactory *pClassFactory = nullptr;
                        hr = ((HRESULT (STDAPICALLTYPE *)(REFCLSID rclsid, REFIID riid, LPVOID *ppv))(pfn))(rclsid, IID_IClassFactory, (void **)&pClassFactory);
                        if (SUCCEEDED(hr) && pClassFactory)
                        {
                            hr = pClassFactory->CreateInstance(NULL, riid, ppv);
                            pClassFactory->Release();
                        }
                    }
                }
            }
        }
    }

    if (!SUCCEEDED(hr) && phInst && *phInst)
    {
        FreeLibrary(*phInst);
        *phInst = 0;
    }
    return hr;
}

//+---------------------------------------------------------------------------
//
// CTSFTTS::GetComModuleName
//
//----------------------------------------------------------------------------

HRESULT CTSFTTS::GetComModuleName(REFGUID rclsid, _Out_writes_(cchPath)WCHAR* wchPath, DWORD cchPath)
{
    HRESULT hr = S_OK;

    CRegKey key;
    WCHAR wchClsid[CLSID_STRLEN + 1];
    hr = CLSIDToString(rclsid, wchClsid) ? S_OK : E_FAIL;
    if (SUCCEEDED(hr))
    {
        WCHAR wchKey[MAX_PATH];
        hr = StringCchPrintfW(wchKey, ARRAYSIZE(wchKey), L"CLSID\\%s\\InProcServer32", wchClsid);
        if (SUCCEEDED(hr))
        {
            hr = (key.Open(HKEY_CLASSES_ROOT, wchKey, KEY_READ) == ERROR_SUCCESS) ? S_OK : E_FAIL;
            if (SUCCEEDED(hr))
            {
                WCHAR wszModel[MAX_PATH];
                ULONG cch = ARRAYSIZE(wszModel);
                hr = (key.QueryStringValue(L"ThreadingModel", wszModel, &cch) == ERROR_SUCCESS) ? S_OK : E_FAIL;
                if (SUCCEEDED(hr))
                {
                    if (CompareStringOrdinal(wszModel, 
                        -1, 
                        L"Apartment", 
                        -1,
                        TRUE) == CSTR_EQUAL)
                    {
                        hr = (key.QueryStringValue(NULL, wchPath, &cchPath) == ERROR_SUCCESS) ? S_OK : E_FAIL;
                    }
                    else
                    {
                        hr = E_FAIL;
                    }
                }
            }
        }
    }

    return hr;
}

//+---------------------------------------------------------------------------
//
// SetupLanguageProfile
//
// Setup language profile for Composition Processor Engine.
// param
//     [in] LANGID langid = Specify language ID
//     [in] GUID guidLanguageProfile - Specify GUID language profile which GUID is as same as Text Service Framework language profile.
//     [in] ITfThreadMgr - pointer ITfThreadMgr.
//     [in] tfClientId - TfClientId value.
//     [in] isSecureMode - secure mode
// returns
//     If setup succeeded, returns true. Otherwise returns false.
// N.B. For reverse conversion, ITfThreadMgr is NULL, TfClientId is 0 and isSecureMode is ignored.
//+---------------------------------------------------------------------------

BOOL CTSFTTS::SetupLanguageProfile(LANGID langid, REFGUID guidLanguageProfile, _In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId, BOOL isSecureMode)
{
    _langid = langid;
	_guidProfile = guidLanguageProfile;

	debugPrint(L"CTSFTTS::SetupLanguageProfile()\n");

    BOOL ret = TRUE;
    if ((tfClientId == 0) && (pThreadMgr == nullptr))
    {
        ret = FALSE;
        goto Exit;
    }

    
	InitializeTSFTTSCompartment(pThreadMgr, tfClientId);
    SetupLanguageBar(pThreadMgr, tfClientId, isSecureMode);

	IME_MODE imeMode = _pCompositionProcessorEngine->GetImeModeFromGuidProfile(guidLanguageProfile);
	_pCompositionProcessorEngine->SetupPreserved(pThreadMgr, tfClientId);	
    _pCompositionProcessorEngine->SetupDictionaryFile(imeMode);
	_pCompositionProcessorEngine->SetupKeystroke(imeMode);
    _pCompositionProcessorEngine->SetupConfiguration();
    
    
Exit:
	debugPrint(L"CTSFTTS::SetupLanguageProfile()finished \n");
    return ret;
}

BOOL CTSFTTS::_IsUILessMode()
{
	if(_pUIPresenter)
		return _pUIPresenter->isUILessMode();
	else
		return FALSE;
}

void CTSFTTS::_LoadConfig(BOOL isForce)
{
	CConfig::LoadConfig();
	if(_pCompositionProcessorEngine) _pCompositionProcessorEngine->UpdateDictionaryFile();

	if(CConfig::GetReloadReverseConversion() || isForce)
	{
		if(_pITfReverseConversion[Global::imeMode])
		{
			_pITfReverseConversion[Global::imeMode]->Release();
			_pITfReverseConversion[Global::imeMode] = nullptr;
		}
		if(!IsEqualCLSID(CConfig::GetReverseConverstionCLSID(), CLSID_NULL) && !IsEqualCLSID(CConfig::GetReverseConversionGUIDProfile(), CLSID_NULL))
		{
			ITfReverseConversionMgr * pITfReverseConversionMgr;
			if(SUCCEEDED(CoCreateInstance(CConfig::GetReverseConverstionCLSID(), nullptr, CLSCTX_INPROC_SERVER, 
				IID_ITfReverseConversionMgr, (void**)&pITfReverseConversionMgr)))
			{
				if(SUCCEEDED( pITfReverseConversionMgr->GetReverseConversion(_langid, 
					CConfig::GetReverseConversionGUIDProfile(), NULL, &_pITfReverseConversion[Global::imeMode])))
				{   //test if the interface can really do reverse conversion
					BSTR bstr;
					bstr = SysAllocStringLen(L"¤@" , (UINT) 1);
					ITfReverseConversionList* reverseConversionList = nullptr;
					if(FAILED(_pITfReverseConversion[Global::imeMode]->DoReverseConversion(bstr, &reverseConversionList)) || reverseConversionList == nullptr)
					{
						_pITfReverseConversion[Global::imeMode] = nullptr;
					}
					else
					{
						reverseConversionList->Release();
					}
				}
				pITfReverseConversionMgr->Release();
			}
		}
		CConfig::SetReloadReverseConversion(FALSE);
		CConfig::WriteConfig();
	}
	if(_pCompositionProcessorEngine == nullptr)
		_AddTextProcessorEngine();



}