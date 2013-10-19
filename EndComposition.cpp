//
//
// Derived from Microsoft Sample IME by Jeremy '13,7,17
//
//
//#define DEBUG_PRINT

#include "Private.h"
#include "Globals.h"
#include "EditSession.h"
#include "TSFTTS.h"

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
    CEndCompositionEditSession(_In_ CTSFTTS *pTextService, _In_ ITfContext *pContext) : CEditSessionBase(pTextService, pContext)
    {
    }

    // ITfEditSession
    STDMETHODIMP DoEditSession(TfEditCookie ec)
    {
		if(_pTextService)
			_pTextService->_TerminateComposition(ec, _pContext, TRUE);
        return S_OK;
    }

};

//////////////////////////////////////////////////////////////////////
//
// CTSFTTS class
//
//////////////////////////////////////////////////////////////////////+---------------------------------------------------------------------------
//
// _TerminateComposition
//
//----------------------------------------------------------------------------

void CTSFTTS::_TerminateComposition(TfEditCookie ec, _In_ ITfContext *pContext, BOOL isCalledFromDeactivate)
{
	debugPrint(L"CTSFTTS::_TerminateComposition()\n");


    if (_pComposition)
    {
		if(isCalledFromDeactivate)
			_RemoveDummyCompositionForComposing(ec, _pComposition);
		else // remove the display attribute from the composition range.
			_ClearCompositionDisplayAttributes(ec, pContext);

		if (FAILED(_pComposition->EndComposition(ec)))
        {
			_DeleteCandidateList(TRUE, pContext);
		}
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

void CTSFTTS::_EndComposition(_In_opt_ ITfContext *pContext)
{
	debugPrint(L"CTSFTTS::_EndComposition()\n");
    CEndCompositionEditSession *pEditSession = new (std::nothrow) CEndCompositionEditSession(this, pContext);
    HRESULT hr = S_OK;

    if (pEditSession && pContext)
    {
        pContext->RequestEditSession(_tfClientId, pEditSession, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &hr);
        pEditSession->Release();
    }
}

