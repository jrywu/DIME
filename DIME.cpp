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
#include "globals.h"
#include "DIME.h"
#include "UIPresenter.h"
#include "CompositionProcessorEngine.h"
#include "Compartment.h"
#include "RegKey.h"
#include "TfInputProcessorProfile.h"
#include <Prsht.h>
#include <Psapi.h>
//////////////////////////////////////////////////////////////////////
//
// CDIME implementation.
//
//////////////////////////////////////////////////////////////////////+---------------------------------------------------------------------------
//
// _AddTextProcessorEngine
//
//----------------------------------------------------------------------------

DWORD CDIME::_dwActivateFlags =0;

BOOL CDIME::_AddTextProcessorEngine(LANGID inLangID, GUID inGuidProfile)
 {
	debugPrint(L"CDIME::_AddTextProcessorEngine()");
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
			debugPrint(L"CDIME::_AddTextProcessorEngine() _pCompositionProcessorEngine with the coming guidProfile exist, return true.");
            return TRUE;
        }
		else
		{

			debugPrint(L"CDIME::_AddTextProcessorEngine() _pCompositionProcessorEngine with the diff. guidProfile exist, recreate one." );
			//_LoadConfig(TRUE);
			//_lastKeyboardMode = CConfig::GetActivatedKeyboardMode();

			if(_pUIPresenter) _pUIPresenter->ClearAll();

			_UninitFunctionProviderSink();  // reset the function provider sink to get updated UI less candidate provider
			_InitFunctionProviderSink();
			
		}
    }

    // Create composition processor engine
    if (_pCompositionProcessorEngine == nullptr)
    {		
		_pCompositionProcessorEngine = new (std::nothrow) CCompositionProcessorEngine(this);

		//_LoadConfig(TRUE);
		//_lastKeyboardMode = CConfig::GetActivatedKeyboardMode();

		debugPrint(L"CDIME::_AddTextProcessorEngine() create new CompositionProcessorEngine . ");
        
    }
    if (!_pCompositionProcessorEngine)
    {
        return FALSE;
    }


	if(_pUIPresenter == nullptr)
	{
		debugPrint(L"CDIME::_AddTextProcessorEngine() create new UIPresenter .");
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
HRESULT CDIME::CreateInstance(_In_ IUnknown *pUnkOuter, REFIID riid, _Outptr_ void **ppvObj)
{
    CDIME* pDIME = nullptr;
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

    pDIME = new (std::nothrow) CDIME();
    if (pDIME == nullptr)
    {
        return E_OUTOFMEMORY;
    }

    hr = pDIME->QueryInterface(riid, ppvObj);

    pDIME->Release();

    return hr;
}

//+---------------------------------------------------------------------------
//
// ctor
//
//----------------------------------------------------------------------------

CDIME::CDIME()
{
	debugPrint(L"CDIME:CDIME() constructor");
    DllAddRef();

    _pThreadMgr = nullptr;

    _commitKeyCode[0] = L'\0';

    _threadMgrEventSinkCookie = TF_INVALID_COOKIE;

    _pTextEditSinkContext = nullptr;
    _textEditSinkCookie = TF_INVALID_COOKIE;

    _activeLanguageProfileNotifySinkCookie = TF_INVALID_COOKIE;

    _dwThreadFocusSinkCookie = TF_INVALID_COOKIE;

    _pComposition = nullptr;

    _pCompositionProcessorEngine = nullptr;

    _candidateMode = CANDIDATE_MODE::CANDIDATE_NONE;
    _pUIPresenter = nullptr;
    _isCandidateWithWildcard = FALSE;

    _pDocMgrLastFocused = nullptr;

    _pSIPIMEOnOffCompartment = nullptr;
    _dwSIPIMEOnOffCompartmentSinkCookie = 0;
    _msgWndHandle = nullptr;

    _pContext = nullptr;

    _refCount = 1;
	
    _commitKeyCode[0] = L'\0';
    _commitString[0] = L'\0';

    _gaDisplayAttributeConverted = 0;
    _gaDisplayAttributeInput = 0;
    _guidProfile = GUID_NULL;

    _tfClientId = 0;

    _pITfFnSearchCandidateProvider = nullptr;

    _pLangBarItem = nullptr;
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
	_lastKeyboardMode = FALSE;
	_isFullShape = FALSE;

	// Get Process Integrity level
	DWORD err = 0;
	_processIntegrityLevel = PROCESS_INTEGRITY_LEVEL::PROCESS_INTEGRITY_LEVEL_UNKNOWN;
	try {
		HANDLE hToken = NULL;
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
			throw GetLastError();

		DWORD cbBuf = 0;
		if (GetTokenInformation(hToken, TokenIntegrityLevel, NULL, 0, &cbBuf) != 0)
			throw GetLastError();
		TOKEN_MANDATORY_LABEL * pTml =
			reinterpret_cast<TOKEN_MANDATORY_LABEL*> (new char[cbBuf]);
		if (pTml &&
			GetTokenInformation(
			hToken,
			TokenIntegrityLevel,
			pTml,
			cbBuf,
			&cbBuf)) {
			CloseHandle(hToken);
			hToken = NULL;
			DWORD ridIl = *GetSidSubAuthority(pTml->Label.Sid, 0);
			if (ridIl < SECURITY_MANDATORY_LOW_RID)
				_processIntegrityLevel = PROCESS_INTEGRITY_LEVEL::PROCESS_INTEGRITY_LEVEL_UNKNOWN;
			else if (ridIl >= SECURITY_MANDATORY_LOW_RID &&
				ridIl < SECURITY_MANDATORY_MEDIUM_RID)
				_processIntegrityLevel = PROCESS_INTEGRITY_LEVEL::PROCESS_INTEGRITY_LEVEL_LOW;
			else if (ridIl >= SECURITY_MANDATORY_MEDIUM_RID &&
				ridIl < SECURITY_MANDATORY_HIGH_RID)
				_processIntegrityLevel = PROCESS_INTEGRITY_LEVEL::PROCESS_INTEGRITY_LEVEL_MEDIUM;
			else if (ridIl >= SECURITY_MANDATORY_HIGH_RID &&
				ridIl < SECURITY_MANDATORY_SYSTEM_RID)
				_processIntegrityLevel = PROCESS_INTEGRITY_LEVEL::PROCESS_INTEGRITY_LEVEL_HIGH;
			else if (ridIl >= SECURITY_MANDATORY_SYSTEM_RID)
				_processIntegrityLevel = PROCESS_INTEGRITY_LEVEL::PROCESS_INTEGRITY_LEVEL_SYSTEM;
			if (ridIl > SECURITY_MANDATORY_LOW_RID &&
				ridIl != SECURITY_MANDATORY_MEDIUM_RID &&
				ridIl != SECURITY_MANDATORY_HIGH_RID &&
				ridIl != SECURITY_MANDATORY_SYSTEM_RID)
				_processIntegrityLevel = PROCESS_INTEGRITY_LEVEL::PROCESS_INTEGRITY_LEVEL_LOW;
			delete[] reinterpret_cast<char*>(pTml);
			pTml = NULL;
		}
		else {
			throw GetLastError();
		}
	}
	catch (DWORD dwErr) {
		err = dwErr;
		debugPrint(L"Error %d", GetLastError());
	}
	catch (std::bad_alloc e) {
		err = ERROR_OUTOFMEMORY;
		debugPrint(L"Error %d", err);
	}
	//Retrieve current process name
	GetModuleBaseName(GetCurrentProcess(), NULL, _processName, MAX_PATH);

	_newlyActivated = FALSE;

}

//+---------------------------------------------------------------------------
//
// dtor
//
//----------------------------------------------------------------------------

CDIME::~CDIME()
{
	debugPrint(L"CDIME:~CDIME() destructor");
	if (_pUIPresenter)
    {
        delete _pUIPresenter;
        _pUIPresenter = nullptr;
    }

	ReleaseReverseConversion();

    // Release all compartment event sinks
    if (_pCompartmentKeyboardOpenEventSink) {
        _pCompartmentKeyboardOpenEventSink->_Unadvise();
    }
    if (_pCompartmentIMEModeEventSink) {
        _pCompartmentIMEModeEventSink->_Unadvise();
    }
    if (_pCompartmentConversionEventSink) {
        _pCompartmentConversionEventSink->_Unadvise();
    }
    if (_pCompartmentDoubleSingleByteEventSink) {
        _pCompartmentDoubleSingleByteEventSink->_Unadvise();
    }

    // Clean up other resources
    if (_pCompartmentConversion) {
        delete _pCompartmentConversion;
        _pCompartmentConversion = nullptr;
    }

    DllRelease();
}

//+---------------------------------------------------------------------------
//
// QueryInterface
//
//----------------------------------------------------------------------------

STDAPI CDIME::QueryInterface(REFIID riid, _Outptr_ void **ppvObj)
{
	debugPrint(L"\nCDIME::QueryInterface()");
    if (ppvObj == nullptr)
    {
        return E_INVALIDARG;
    }

    *ppvObj = nullptr;

    if (IsEqualIID(riid, IID_IUnknown) ||
        IsEqualIID(riid, IID_ITfTextInputProcessor))
    {
		debugPrint(L"CDIME::QueryInterface() :IID_IUnknown, IID_ITfTextInputProcessor");
        *ppvObj = (ITfTextInputProcessor *)this;
    }
    else if (IsEqualIID(riid, IID_ITfTextInputProcessorEx))
    {
		debugPrint(L"CDIME::QueryInterface() :IID_ITfTextInputProcessorEx");
        *ppvObj = (ITfTextInputProcessorEx *)this;
    }
    else if (IsEqualIID(riid, IID_ITfThreadMgrEventSink))
    {
		debugPrint(L"CDIME::QueryInterface() :IID_ITfThreadMgrEventSink");
        *ppvObj = (ITfThreadMgrEventSink *)this;
    }
    else if (IsEqualIID(riid, IID_ITfTextEditSink))
    {
		debugPrint(L"CDIME::QueryInterface() :IID_ITfTextEditSink");
        *ppvObj = (ITfTextEditSink *)this;
    }
    else if (IsEqualIID(riid, IID_ITfKeyEventSink))
    {
		debugPrint(L"CDIME::QueryInterface() :IID_ITfKeyEventSink");
        *ppvObj = (ITfKeyEventSink *)this;
    }
    else if (IsEqualIID(riid, IID_ITfActiveLanguageProfileNotifySink))
    {
		debugPrint(L"CDIME::QueryInterface() :IID_ITfActiveLanguageProfileNotifySink");
        *ppvObj = (ITfActiveLanguageProfileNotifySink *)this;
    }
    else if (IsEqualIID(riid, IID_ITfCompositionSink))
    {
		debugPrint(L"CDIME::QueryInterface() :IID_ITfCompositionSink");
        *ppvObj = (ITfKeyEventSink *)this;
    }
    else if (IsEqualIID(riid, IID_ITfDisplayAttributeProvider))
    {
		debugPrint(L"CDIME::QueryInterface() :IID_ITfDisplayAttributeProvider");
        *ppvObj = (ITfDisplayAttributeProvider *)this;
    }
    else if (IsEqualIID(riid, IID_ITfThreadFocusSink))
    {
		debugPrint(L"CDIME::QueryInterface() :IID_ITfThreadFocusSink");
        *ppvObj = (ITfThreadFocusSink *)this;
    }
    else if (IsEqualIID(riid, IID_ITfFunctionProvider))
    {
		debugPrint(L"CDIME::QueryInterface() :IID_ITfFunctionProvider");
        *ppvObj = (ITfFunctionProvider *)this;
    }
    else if (IsEqualIID(riid, IID_ITfFnGetPreferredTouchKeyboardLayout))
    {
		debugPrint(L"CDIME::QueryInterface() :IID_ITfFnGetPreferredTouchKeyboardLayout");
        *ppvObj = (ITfFnGetPreferredTouchKeyboardLayout *)this;
    }
	else if (IsEqualIID(riid, IID_ITfFnConfigure))
    {
		debugPrint(L"CDIME::QueryInterface() :IID_ITfFnConfigure");
        *ppvObj = (ITfFnConfigure *)this;
    }
	else if (IsEqualIID(riid, IID_ITfReverseConversionMgr ))
    {
		debugPrint(L"CDIME::QueryInterface() :IID_ITfReverseConversionMgr");
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

STDAPI_(ULONG) CDIME::AddRef()
{
	debugPrint(L"CDIME::AddRef(), _refCount = %d", _refCount+1); 
    return ++_refCount;
}

//+---------------------------------------------------------------------------
//
// Release
//
//----------------------------------------------------------------------------

STDAPI_(ULONG) CDIME::Release()
{
	debugPrint(L"CDIME::Release(), _refCount = %d", _refCount-1);
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

STDAPI CDIME::ActivateEx(ITfThreadMgr *pThreadMgr, TfClientId tfClientId, DWORD dwFlags)
{
	debugPrint(L"CDIME::ActivateEx(); ITfTextInputProcessorEx::ActivateEx()");
    _pThreadMgr = pThreadMgr;
	if(_pThreadMgr)
		_pThreadMgr->AddRef();

    _tfClientId = tfClientId;
    _dwActivateFlags = dwFlags;


	debugPrint(L"CDIME::ActivateEx(); _InitThreadMgrEventSink.");
    if (!_InitThreadMgrEventSink())
    {
		debugPrint(L"CDIME::ActivateEx(); _InitThreadMgrEventSink failed.");
        goto ExitError;
    }

	
    ITfDocumentMgr* pDocMgrFocus = nullptr;
    if ( _pThreadMgr && SUCCEEDED(_pThreadMgr->GetFocus(&pDocMgrFocus)) && pDocMgrFocus)
    {
		debugPrint(L"CDIME::ActivateEx(); _InitTextEditSink.");
        _InitTextEditSink(pDocMgrFocus);
        pDocMgrFocus->Release();
    }


	debugPrint(L"CDIME::ActivateEx(); _InitKeyEventSink.");
    if (!_InitKeyEventSink())
    {
		debugPrint(L"CDIME::ActivateEx(); _InitKeyEventSink failed.");
        goto ExitError;
    }

	debugPrint(L"CDIME::ActivateEx(); _InitActiveLanguageProfileNotifySink.");
    if (!_InitActiveLanguageProfileNotifySink())
    {
		debugPrint(L"CDIME::ActivateEx(); _InitActiveLanguageProfileNotifySink failed.");
        goto ExitError;
    }

	debugPrint(L"CDIME::ActivateEx(); _InitThreadFocusSink.");
    if (!_InitThreadFocusSink())
    {
		debugPrint(L"CDIME::ActivateEx(); _InitThreadFocusSink failed.");
        goto ExitError;
    }

	debugPrint(L"CDIME::ActivateEx(); _InitDisplayAttributeGuidAtom.");
    if (!_InitDisplayAttributeGuidAtom())
    {
		debugPrint(L"CDIME::ActivateEx(); _InitDisplayAttributeGuidAtom failed.");
        goto ExitError;
    }

	debugPrint(L"CDIME::ActivateEx(); _InitFunctionProviderSink.");
    if (!_InitFunctionProviderSink())
    {
		debugPrint(L"CDIME::ActivateEx(); _InitFunctionProviderSink failed.");
        goto ExitError;
    }

	debugPrint(L"CDIME::ActivateEx(); _AddTextProcessorEngine.");
    if (!_AddTextProcessorEngine())
    {
		debugPrint(L"CDIME::ActivateEx(); _AddTextProcessorEngine failed.");
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

STDAPI CDIME::Deactivate()
{
	debugPrint(L"CDIME::Deactivate()");
    if (_pCompositionProcessorEngine)
    {
        delete _pCompositionProcessorEngine;
		_pCompositionProcessorEngine = nullptr;

		//original do this in deconstructor of compositionprocessorengine
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

	if(_pITfReverseConversion[(UINT)Global::imeMode])
		{
			_pITfReverseConversion[(UINT)Global::imeMode]->Release();
			_pITfReverseConversion[(UINT)Global::imeMode] = nullptr;
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

        _candidateMode = CANDIDATE_MODE::CANDIDATE_NONE;
        _isCandidateWithWildcard = FALSE;
    }

    _UninitFunctionProviderSink();

    _UninitThreadFocusSink();

    _UninitActiveLanguageProfileNotifySink();

    _UninitKeyEventSink();

	_UnInitTextEditSink();

    _UninitThreadMgrEventSink();

	HideAllLanguageBarIcons();

	CCompartment CompartmentIMEMode(_pThreadMgr, _tfClientId, Global::DIMEGuidCompartmentIMEMode);
    CompartmentIMEMode._ClearCompartment();


    CCompartment CompartmentDoubleSingleByte(_pThreadMgr, _tfClientId, Global::DIMEGuidCompartmentDoubleSingleByte);
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
HRESULT CDIME::GetType(__RPC__out GUID *pguid)
{
    HRESULT hr = E_INVALIDARG;
    if (pguid)
    {
        *pguid = Global::DIMECLSID;
        hr = S_OK;
    }
    return hr;
}

//+---------------------------------------------------------------------------
//
// ITfFunctionProvider::::GetDescription
//
//----------------------------------------------------------------------------
HRESULT CDIME::GetDescription(__RPC__deref_out_opt BSTR *pbstrDesc)
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
HRESULT CDIME::GetFunction(__RPC__in REFGUID rguid, __RPC__in REFIID riid, __RPC__deref_out_opt IUnknown **ppunk)
{
	debugPrint(L"CDIME::GetFunction()");
    HRESULT hr = E_NOINTERFACE;
	
    if ((IsEqualGUID(rguid, GUID_NULL)) 
        && (IsEqualGUID(riid, __uuidof(ITfFnSearchCandidateProvider))) && _pITfFnSearchCandidateProvider)
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
HRESULT CDIME::GetDisplayName(_Out_ BSTR *pbstrDisplayName)
{
	debugPrint(L"CDIME::GetDisplayName()");
	
	if(pbstrDisplayName == NULL)
		return E_INVALIDARG;
	

	BSTR bstrName = new (std::nothrow) WCHAR[64];
	if (bstrName == NULL)	return E_OUTOFMEMORY;

	*bstrName = { 0 };
	if(Global::imeMode == IME_MODE::IME_MODE_DAYI)
		LoadString(Global::dllInstanceHandle, IDS_DAYI_DESCRIPTION, bstrName, 64);
	if(Global::imeMode == IME_MODE::IME_MODE_ARRAY)
		LoadString(Global::dllInstanceHandle, IDS_ARRAY_DESCRIPTION, bstrName, 64);
	if(Global::imeMode == IME_MODE::IME_MODE_PHONETIC)
		LoadString(Global::dllInstanceHandle, IDS_PHONETIC_DESCRIPTION, bstrName, 64);
	if(Global::imeMode == IME_MODE::IME_MODE_GENERIC)
		LoadString(Global::dllInstanceHandle, IDS_GENERIC_DESCRIPTION, bstrName, 64);
	
	*pbstrDisplayName = SysAllocString(bstrName);
	delete[] bstrName;
	return S_OK;

}

//+---------------------------------------------------------------------------
//
// ITfFnGetPreferredTouchKeyboardLayout::GetLayout
// The tkblayout will be Optimized layout.
//----------------------------------------------------------------------------
HRESULT CDIME::GetLayout(_Out_ TKBLayoutType *ptkblayoutType, _Out_ WORD *pwPreferredLayoutId)
{
	debugPrint(L"CDIME::GetLayout(), imeMode = %d ", Global::imeMode);
    HRESULT hr = E_INVALIDARG;
    if ((ptkblayoutType != nullptr) && (pwPreferredLayoutId != nullptr))
    {
		if(Global::imeMode==IME_MODE::IME_MODE_DAYI)
		{
			*ptkblayoutType = TKBLT_CLASSIC;
			*pwPreferredLayoutId = TKBL_CLASSIC_TRADITIONAL_CHINESE_DAYI;
		}
		else if(Global::imeMode==IME_MODE::IME_MODE_PHONETIC)
		{
			*ptkblayoutType = TKBLT_OPTIMIZED;
			*pwPreferredLayoutId = TKBL_OPT_TRADITIONAL_CHINESE_PHONETIC;
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


//+---------------------------------------------------------------------------
//
// CDIME::CreateInstance 
//
//----------------------------------------------------------------------------

HRESULT CDIME::CreateInstance(REFCLSID rclsid, REFIID riid, _Outptr_result_maybenull_ LPVOID* ppv, _Out_opt_ HINSTANCE* phInst, BOOL isComLessMode)
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
        hr = CDIME::ComLessCreateInstance(rclsid, riid, ppv, phInst);
    }

    return hr;
	}

//+---------------------------------------------------------------------------
//
// CDIME::ComLessCreateInstance
//
//----------------------------------------------------------------------------

HRESULT CDIME::ComLessCreateInstance(REFGUID rclsid, REFIID riid, _Outptr_result_maybenull_ void **ppv, _Out_opt_ HINSTANCE *phInst)
{
    HRESULT hr = S_OK;
    HINSTANCE DIMEDllHandle = nullptr;
    WCHAR wchPath[MAX_PATH] = {'\0'}; // Initialize with null characters
    WCHAR szExpandedPath[MAX_PATH] = {'\0'}; // Initialize with null characters
    DWORD dwCnt = 0;
    *ppv = nullptr;

    hr = phInst ? S_OK : E_FAIL;
    if (SUCCEEDED(hr))
    {
        *phInst = nullptr;
        hr = CDIME::GetComModuleName(rclsid, wchPath, ARRAYSIZE(wchPath));
        if (SUCCEEDED(hr))
        {
            dwCnt = ExpandEnvironmentStringsW(wchPath, szExpandedPath, ARRAYSIZE(szExpandedPath));
            hr = (0 < dwCnt && dwCnt <= ARRAYSIZE(szExpandedPath)) ? S_OK : E_FAIL;
            if (SUCCEEDED(hr))
            {
                DIMEDllHandle = LoadLibraryEx(szExpandedPath, NULL, 0);
                hr = DIMEDllHandle ? S_OK : E_FAIL;
                if (SUCCEEDED(hr))
                {
                    *phInst = DIMEDllHandle;
                    FARPROC pfn = GetProcAddress(DIMEDllHandle, "DllGetClassObject");
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
// CDIME::GetComModuleName
//
//----------------------------------------------------------------------------

HRESULT CDIME::GetComModuleName(REFGUID rclsid, _Out_writes_(cchPath)WCHAR* wchPath, DWORD cchPath)
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

BOOL CDIME::SetupLanguageProfile(LANGID langid, REFGUID guidLanguageProfile, _In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId, BOOL isSecureMode)
{
    _langid = langid;
	_guidProfile = guidLanguageProfile;
	
	debugPrint(L"CDIME::SetupLanguageProfile()\n");

    BOOL ret = TRUE;
    if ((tfClientId == 0) && (pThreadMgr == nullptr))
    {
        ret = FALSE;
        goto Exit;
    }

	if(_pCompositionProcessorEngine)
	{
		IME_MODE imeMode = _pCompositionProcessorEngine->GetImeModeFromGuidProfile(_guidProfile);

		if (pThreadMgr)  // pThreadMgr = Null for reverse conversion
		{
			Global::imeMode = imeMode;
			debugPrint(L"CDIME::SetupLanguageProfile() imeMode = %d \n", imeMode);



			InitializeDIMECompartment(pThreadMgr, tfClientId);
			SetupLanguageBar(pThreadMgr, tfClientId, isSecureMode);


			_pCompositionProcessorEngine->SetupPreserved(pThreadMgr, tfClientId);
			_pCompositionProcessorEngine->SetupDictionaryFile(imeMode);
			
            CConfig::LoadConfig(Global::imeMode);
			_pCompositionProcessorEngine->SetupConfiguration(imeMode);
            _pCompositionProcessorEngine->SetupKeystroke(imeMode);
            _pCompositionProcessorEngine->SetupCandidateListRange(imeMode);

		}
	}

    
Exit:
	debugPrint(L"CDIME::SetupLanguageProfile()finished \n");
    return ret;
}

BOOL CDIME::_IsUILessMode()
{
	if(_pUIPresenter)
		return _pUIPresenter->isUILessMode();
	else
		return FALSE;
}

void CDIME::_LoadConfig(BOOL isForce, IME_MODE imeMode)
{
    if (!isForce)
    {
        BOOL configUpdated = CConfig::LoadConfig(Global::imeMode);
        BOOL dictionaryUpdated = FALSE;
        if (_pCompositionProcessorEngine)
            dictionaryUpdated = _pCompositionProcessorEngine->SetupDictionaryFile(Global::imeMode);
        if (configUpdated || dictionaryUpdated) // config file updated
        {
            _pCompositionProcessorEngine->SetupConfiguration(Global::imeMode);
            _pCompositionProcessorEngine->SetupKeystroke(Global::imeMode);
            _pCompositionProcessorEngine->SetupCandidateListRange(Global::imeMode);
        }
    }

	if(CConfig::GetReloadReverseConversion() || isForce)
	{
		if(_pITfReverseConversion[(UINT)imeMode])
		{
			_pITfReverseConversion[(UINT)imeMode]->Release();
			_pITfReverseConversion[(UINT)imeMode] = nullptr;
		}
		if(!IsEqualCLSID(CConfig::GetReverseConverstionCLSID(), CLSID_NULL) && !IsEqualCLSID(CConfig::GetReverseConversionGUIDProfile(), CLSID_NULL))
		{
			ITfReverseConversionMgr * pITfReverseConversionMgr = nullptr;
			if(SUCCEEDED(CoCreateInstance(CConfig::GetReverseConverstionCLSID(), nullptr, CLSCTX_INPROC_SERVER, 
				IID_ITfReverseConversionMgr, (void**)&pITfReverseConversionMgr)) && pITfReverseConversionMgr)
			{
				if(SUCCEEDED( pITfReverseConversionMgr->GetReverseConversion(_langid, 
					CConfig::GetReverseConversionGUIDProfile(), NULL, &_pITfReverseConversion[(UINT)imeMode])) && _pITfReverseConversion[(UINT)imeMode])
				{   //test if the interface can really do reverse conversion
					BSTR bstr;
					bstr = SysAllocStringLen(L"一" , (UINT) 1);
					ITfReverseConversionList* reverseConversionList = nullptr;
					if(bstr && FAILED(_pITfReverseConversion[(UINT)imeMode]->DoReverseConversion(bstr, &reverseConversionList)) || reverseConversionList == nullptr)
					{
						_pITfReverseConversion[(UINT)imeMode] = nullptr;
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
	}


}

//+---------------------------------------------------------------------------
//
// CDIME::doBeep
//
//----------------------------------------------------------------------------

void CDIME::DoBeep(BEEP_TYPE type)
{
	if (CConfig::GetDoBeep() || type == BEEP_TYPE::BEEP_ON_CANDI)
		MessageBeep(MB_OK);
	if (CConfig::GetDoBeepNotify() && type == BEEP_TYPE::BEEP_COMPOSITION_ERROR)
	{
		CStringRange notify;
		if (_pUIPresenter)
            _pUIPresenter->ShowNotifyText(&notify.Set(L"錯誤組字", 4), 0, 1000, NOTIFY_TYPE::NOTIFY_BEEP);
	}
		
}