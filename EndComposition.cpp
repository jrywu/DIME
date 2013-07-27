//
//
// Derived from Microsoft Sample IME by Jeremy '13,7,17
//
//


#include "Private.h"
#include "Globals.h"
#include "EditSession.h"
#include "TSFDayi.h"

//////////////////////////////////////////////////////////////////////
//
//    ITfEditSession
//        CEditSessionBase
// CEndCompositionEditSession class
//
//////////////////////////////////////////////////////////////////////+---------------------------------------------------------------------------
//
// CEndCompositionEditSession
//
//----------------------------------------------------------------------------

class CEndCompositionEditSession : public CEditSessionBase
{
public:
    CEndCompositionEditSession(_In_ CTSFDayi *pTextService, _In_ ITfContext *pContext) : CEditSessionBase(pTextService, pContext)
    {
    }

    // ITfEditSession
    STDMETHODIMP DoEditSession(TfEditCookie ec)
    {
        _pTextService->_TerminateComposition(ec, _pContext, TRUE);
        return S_OK;
    }

};

//////////////////////////////////////////////////////////////////////
//
// CTSFDayi class
//
//////////////////////////////////////////////////////////////////////+---------------------------------------------------------------------------
//
// _TerminateComposition
//
//----------------------------------------------------------------------------

void CTSFDayi::_TerminateComposition(TfEditCookie ec, _In_ ITfContext *pContext, BOOL isCalledFromDeactivate)
{
	isCalledFromDeactivate;

    if (_pComposition != nullptr)
    {
		if(isCalledFromDeactivate)
			_RemoveDummyCompositionForComposing(ec, _pComposition);
		else // remove the display attribute from the composition range.
			_ClearCompositionDisplayAttributes(ec, pContext);

		//if (FAILED(_pComposition->EndComposition(ec)) )
  //      {
  //          // if we fail to EndComposition, then we need to close the reverse reading window.
  //          _DeleteCandidateList(TRUE, pContext);
  //      }
		_pComposition->EndComposition(ec);
		_DeleteCandidateList(TRUE, pContext);
        _pComposition->Release();
        _pComposition = nullptr;

        if (_pContext)
        {
            _pContext->Release();
            _pContext = nullptr;
        }
    }
}

//+---------------------------------------------------------------------------
//
// _EndComposition
//
//----------------------------------------------------------------------------

void CTSFDayi::_EndComposition(_In_opt_ ITfContext *pContext)
{
    CEndCompositionEditSession *pEditSession = new (std::nothrow) CEndCompositionEditSession(this, pContext);
    HRESULT hr = S_OK;

    if (nullptr != pEditSession)
    {
        pContext->RequestEditSession(_tfClientId, pEditSession, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &hr);
        pEditSession->Release();
    }
}
void CTSFDayi::clearAndExit()
{
	// switching to English (native) mode delete the phrase candidate window before exting.
	if(_pContext) 
		_EndComposition(_pContext);
	_DeleteCandidateList(FALSE, NULL);
	
}

