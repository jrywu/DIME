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

//+---------------------------------------------------------------------------
//
// CStartCompositinoEditSession
//
//----------------------------------------------------------------------------

class CStartCompositionEditSession : public CEditSessionBase
{
public:
    CStartCompositionEditSession(_In_ CTSFTTS *pTextService, _In_ ITfContext *pContext) : CEditSessionBase(pTextService, pContext)
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

STDAPI CStartCompositionEditSession::DoEditSession(TfEditCookie ec)
{
	debugPrint(L"CStartCompositionEditSession::DoEditSession()\n");
    ITfInsertAtSelection* pInsertAtSelection = nullptr;
    ITfRange* pRangeInsert = nullptr;
    ITfContextComposition* pContextComposition = nullptr;
    ITfComposition* pComposition = nullptr;

    if (FAILED(_pContext->QueryInterface(IID_ITfInsertAtSelection, (void **)&pInsertAtSelection)))
    {
        goto Exit;
    }

    if (FAILED(pInsertAtSelection->InsertTextAtSelection(ec, TF_IAS_QUERYONLY, NULL, 0, &pRangeInsert)))
    {
        goto Exit;
    }

    if (FAILED(_pContext->QueryInterface(IID_ITfContextComposition, (void **)&pContextComposition)))
    {
        goto Exit;
    }

    if (_pTextService && _pContext &&
		SUCCEEDED(pContextComposition->StartComposition(ec, pRangeInsert, _pTextService, &pComposition)) && pComposition)
    {
        _pTextService->_SetComposition(pComposition);

        // set selection to the adjusted range
        TF_SELECTION tfSelection;
        tfSelection.range = pRangeInsert;
        tfSelection.style.ase = TF_AE_NONE;
        tfSelection.style.fInterimChar = FALSE;

        _pContext->SetSelection(ec, 1, &tfSelection);
        _pTextService->_SaveCompositionContext(_pContext);
    }

Exit:
    if (pContextComposition)
    {
        pContextComposition->Release();
    }

    if (pRangeInsert)
    {
        pRangeInsert->Release();
    }

    if (pInsertAtSelection)
    {
        pInsertAtSelection->Release();
    }

    return S_OK;
}

//////////////////////////////////////////////////////////////////////
//
// CTSFTTS class
//
//////////////////////////////////////////////////////////////////////+---------------------------------------------------------------------------
//
// _StartComposition
//
// this starts the new (std::nothrow) composition at the selection of the current 
// focus context.
//----------------------------------------------------------------------------

void CTSFTTS::_StartComposition(_In_ ITfContext *pContext)
{
	debugPrint(L"CTSFTTS::_StartComposition()\n");
	_LoadConfig();// update config upon start composition
    CStartCompositionEditSession* pStartCompositionEditSession = new (std::nothrow) CStartCompositionEditSession(this, pContext);

    if (pContext && pStartCompositionEditSession)
    {
        HRESULT hr = S_OK;
        pContext->RequestEditSession(_tfClientId, pStartCompositionEditSession, TF_ES_SYNC | TF_ES_READWRITE, &hr);

        pStartCompositionEditSession->Release();
    }
}

//+---------------------------------------------------------------------------
//
// _SaveCompositionContext
//
// this saves the context _pComposition belongs to; we need this to clear
// text attribute in case composition has not been terminated on 
// deactivation
//----------------------------------------------------------------------------

void CTSFTTS::_SaveCompositionContext(_In_ ITfContext *pContext)
{
	if(pContext)
	{
		pContext->AddRef();
		_pContext = pContext;
	}
} 
