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

#include "DIME.h"
#include "UIPresenter.h"




//+---------------------------------------------------------------------------
//
// CReverseConversionEditSession
//
//----------------------------------------------------------------------------

class CReverseConversionEditSession : public CEditSessionBase
{
public:
    CReverseConversionEditSession(_In_ CDIME *pTextService, _In_ ITfContext *pContext) : CEditSessionBase(pTextService, pContext)
    {}

    // ITfEditSession
    STDMETHODIMP DoEditSession(TfEditCookie ec);
};

//+---------------------------------------------------------------------------
//
// ITfEditSession::DoEditSession
//
//----------------------------------------------------------------------------

STDAPI CReverseConversionEditSession::DoEditSession(TfEditCookie ec)
{
	debugPrint(L"CReverseConversionEditSession::DoEditSession()\n");
	if(_pTextService && _pContext) 
		_pTextService->_AsyncReverseConversionNotification(ec, _pContext);
	
    return S_OK;
}

void CDIME::_AsyncReverseConversion(_In_ ITfContext* pContext)
{
	debugPrint(L"CDIME::_AsyncReverseConversion() pContext = %x\n", pContext);
	CReverseConversionEditSession* pReverseConversionEditSession = new (std::nothrow) CReverseConversionEditSession(this, pContext);

	if ( pReverseConversionEditSession)
	{
		
		HRESULT hrES = S_OK, hr = S_OK;
		hr = pContext->RequestEditSession(_tfClientId, pReverseConversionEditSession, TF_ES_ASYNC | TF_ES_READWRITE, &hrES);
		
		debugPrint(L"CDIME::_AsyncReverseConversion() RequestEdisession HRESULT = %x, return HRESULT  = %x\n", hrES, hr );

		pReverseConversionEditSession->Release();
	}
	
}

HRESULT CDIME::_AsyncReverseConversionNotification(_In_ TfEditCookie ec,_In_ ITfContext *pContext)
{
	ec;
	debugPrint(L"CDIME::_AsyncReverseConversionNotification() pContext = %x\n", pContext);
	BSTR bstr;
	bstr = SysAllocStringLen(_commitString , (UINT) wcslen(_commitString));
	if (bstr == nullptr) return E_OUTOFMEMORY;
	ITfReverseConversionList* reverseConversionList;
	if(SUCCEEDED(_pITfReverseConversion[(UINT)Global::imeMode]->DoReverseConversion(bstr, &reverseConversionList)) && reverseConversionList)
	{
		UINT hasResult;
		if(reverseConversionList && SUCCEEDED(reverseConversionList->GetLength(&hasResult)) && hasResult)
		{
			BSTR bstrResult;
			if(SUCCEEDED(reverseConversionList->GetString(0, &bstrResult))  && bstrResult && SysStringLen(bstrResult))
			{
				CStringRange reverseConvNotify;
				WCHAR* pwch = new (std::nothrow) WCHAR[SysStringLen(bstrResult)+1];
				StringCchCopy(pwch, SysStringLen(bstrResult)+1, (WCHAR*) bstrResult);
				_pUIPresenter->ShowNotifyText(&reverseConvNotify.Set(pwch, wcslen(pwch)), 0, 0, NOTIFY_TYPE::NOTIFY_OTHERS);
			}
		}
		reverseConversionList->Release();
		return S_OK;
	}
	return S_FALSE;
}


