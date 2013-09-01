//#define DEBUG_PRINT

#include "TSFTTS.h"
#include "UIPresenter.h"




//+---------------------------------------------------------------------------
//
// CReverseConversionEditSession
//
//----------------------------------------------------------------------------

class CReverseConversionEditSession : public CEditSessionBase
{
public:
    CReverseConversionEditSession(_In_ CTSFTTS *pTextService, _In_ ITfContext *pContext) : CEditSessionBase(pTextService, pContext)
    {
    }

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
	_pTextService->_AsyncReverseConversionNotification(ec, _pContext);
	
    return S_OK;
}

void CTSFTTS::_AsyncReverseConversion(_In_ ITfContext* pContext)
{
	debugPrint(L"CTSFTTS::_AsyncReverseConversion() pContext = %x\n", pContext);
	CReverseConversionEditSession* pReverseConversionEditSession = new (std::nothrow) CReverseConversionEditSession(this, pContext);

	if (nullptr != pReverseConversionEditSession)
	{
		
		HRESULT hrES = S_OK, hr = S_OK;
		hr = pContext->RequestEditSession(_tfClientId, pReverseConversionEditSession, TF_ES_ASYNC | TF_ES_READWRITE, &hrES);
		
		debugPrint(L"CTSFTTS::_AsyncReverseConversion() RequestEdisession HRESULT = %x, return HRESULT  = %x\n", hrES, hr );

		pReverseConversionEditSession->Release();
	}
	
}

HRESULT CTSFTTS::_AsyncReverseConversionNotification(_In_ TfEditCookie ec,_In_ ITfContext *pContext)
{
	ec;
	debugPrint(L"CTSFTTS::_AsyncReverseConversionNotification() pContext = %x\n", pContext);
	BSTR bstr;
	bstr = SysAllocStringLen(_commitString.Get() , (UINT) _commitString.GetLength());
	ITfReverseConversionList* reverseConversionList;
	if(SUCCEEDED(_pITfReverseConversion[Global::imeMode]->DoReverseConversion(bstr, &reverseConversionList)))
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
				_pUIPresenter->ShowNotifyText(&reverseConvNotify.Set(pwch, wcslen(pwch)));
			}
		}
		reverseConversionList->Release();
		return S_OK;
	}
	return S_FALSE;
}


