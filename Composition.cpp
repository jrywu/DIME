//
//
// Derived from Microsoft Sample IME by Jeremy '13,7,17
//
//
//#define DEBUG_PRINT

#include "Private.h"
#include "Globals.h"
#include "TSFTTS.h"
#include "CompositionProcessorEngine.h"
#include "UIPresenter.h"
#include "GetTextExtentEditSession.h"

//+---------------------------------------------------------------------------
//
// ITfCompositionSink::OnCompositionTerminated
//
// Callback for ITfCompositionSink.  The system calls this method whenever
// someone other than this service ends a composition.
//----------------------------------------------------------------------------

STDAPI CTSFTTS::OnCompositionTerminated(TfEditCookie ecWrite, _In_ ITfComposition *pComposition)
{
	debugPrint(L"CTSFTTS::OnCompositionTerminated()");
	HRESULT hr = S_OK;
    ITfContext* pContext = _pContext;
   
	// Clear dummy composition
	_RemoveDummyCompositionForComposing(ecWrite, pComposition);
	
    // Clear display attribute and end composition, _EndComposition will release composition for us
     if (pContext)
    {
        pContext->AddRef();
	}
	
	_EndComposition(pContext);
	_DeleteCandidateList(TRUE, pContext);
	
	if (pContext)
	{
		pContext->Release();
        pContext = nullptr;
    }
    return hr;
}

HRESULT CTSFTTS::_LayoutChangeNotification(TfEditCookie ec, _In_ ITfContext *pContext, RECT* rc)
{
	ec; pContext;
	debugPrint(L"\nCTSFTTS::_HandlTextLayoutChange() _candidateMode = %d", _candidateMode);
	debugPrint (L"CTSFTTS::_HandlTextLayoutChange(); top=%d, bottom=%d, left =%d, righ=%d",rc->top, rc->bottom, rc->left, rc->right);
	POINT curPos;
	GetCaretPos(&curPos);
	ClientToScreen(GetFocus(), &curPos);
	debugPrint (L"CTSFTTS::_HandlTextLayoutChange(); x=%d, y=%d",curPos.x, curPos.y);		
	if( _candidateMode == CANDIDATE_WITH_NEXT_COMPOSITION || _candidateMode == CANDIDATE_PHRASE)
	{
		
		debugPrint(L"CTSFTTS::_HandlTextLayouyChange() candMode = CANDIDATE_WITH_NEXT_COMPOSITION or CANDIDATE_PHRASE");
		if(_phraseCandShowing)
		{
			debugPrint(L"CTSFTTS::_HandlTextLayouyChange() _phraseCand is showing ");
			_phraseCandShowing = FALSE; //finishing showing phrase cand
			_phraseCandLocation.x = rc->left; //curPos.x;
			_phraseCandLocation.y = rc->bottom;//curPos.y;
			
		}
		else if( (_phraseCandLocation.x != rc->left) || (_phraseCandLocation.y != rc->bottom))//-------> bug here may cancel cand accidently
		{  //phrase cand moved delete the cand.
			debugPrint(L"CTSFTTS::_HandlTextLayouyChange() cursor moved. end composition and kill the cand.");
			//_HandleCancel(ec, pContext);
			
		}


	}

	

   
	
	return S_OK;
}

//+---------------------------------------------------------------------------
//
// _IsComposing
//
//----------------------------------------------------------------------------

BOOL CTSFTTS::_IsComposing()
{
    return _pComposition != nullptr;
}

//+---------------------------------------------------------------------------
//
// _SetComposition
//
//----------------------------------------------------------------------------

void CTSFTTS::_SetComposition(_In_ ITfComposition *pComposition)
{
    _pComposition = pComposition;
}

//+---------------------------------------------------------------------------
//
// _AddComposingAndChar
//
//----------------------------------------------------------------------------

HRESULT CTSFTTS::_AddComposingAndChar(TfEditCookie ec, _In_ ITfContext *pContext, _In_ CStringRange *pstrAddString)
{
	debugPrint(L"CTSFTTS::_AddComposingAndChar()");
    HRESULT hr = S_OK;

    ULONG fetched = 0;
    TF_SELECTION tfSelection;

    if (pContext->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &tfSelection, &fetched) != S_OK || fetched == 0)
        return S_FALSE;

    //
    // make range start to selection
    //
    ITfRange* pAheadSelection = nullptr;
    hr = pContext->GetStart(ec, &pAheadSelection);
    if (SUCCEEDED(hr))
    {
        hr = pAheadSelection->ShiftEndToRange(ec, tfSelection.range, TF_ANCHOR_START);
        if (SUCCEEDED(hr))
        {
            ITfRange* pRange = nullptr;
            BOOL exist_composing = _FindComposingRange(ec, pContext, pAheadSelection, &pRange);

            _SetInputString(ec, pContext, pRange, pstrAddString, exist_composing);

            if (pRange)
            {
                pRange->Release();
            }
        }
    }

    tfSelection.range->Release();

    if (pAheadSelection)
    {
        pAheadSelection->Release();
    }

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _AddCharAndFinalize
//
//----------------------------------------------------------------------------

HRESULT CTSFTTS::_AddCharAndFinalize(TfEditCookie ec, _In_ ITfContext *pContext, _In_ CStringRange *pstrAddString)
{
	debugPrint(L"CTSFTTS::_AddCharAndFinalize()");
    HRESULT hr = E_FAIL;

    ULONG fetched = 0;
    TF_SELECTION tfSelection;

    if ((hr = pContext->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &tfSelection, &fetched)) != S_OK || fetched != 1)
        return hr;

    // we use SetText here instead of InsertTextAtSelection because we've already started a composition
    // we don't want the app to adjust the insertion point inside our composition
    hr = tfSelection.range->SetText(ec, 0, pstrAddString->Get(), (LONG)pstrAddString->GetLength());
    if (hr == S_OK)
    {
        // update the selection, we'll make it an insertion point just past
        // the inserted text.
        tfSelection.range->Collapse(ec, TF_ANCHOR_END);
        pContext->SetSelection(ec, 1, &tfSelection);
    }

    tfSelection.range->Release();

    return hr;
}

//+---------------------------------------------------------------------------
//
// _FindComposingRange
//
//----------------------------------------------------------------------------

BOOL CTSFTTS::_FindComposingRange(TfEditCookie ec, _In_ ITfContext *pContext, _In_ ITfRange *pSelection, _Outptr_result_maybenull_ ITfRange **ppRange)
{
	debugPrint(L"CTSFTTS::_FindComposingRange()");

    if (ppRange == nullptr)
    {
        return FALSE;
    }

    *ppRange = nullptr;

    // find GUID_PROP_COMPOSING
    ITfProperty* pPropComp = nullptr;
    IEnumTfRanges* enumComp = nullptr;

    HRESULT hr = pContext->GetProperty(GUID_PROP_COMPOSING, &pPropComp);
    if (FAILED(hr) || pPropComp == nullptr)
    {
        return FALSE;
    }

    hr = pPropComp->EnumRanges(ec, &enumComp, pSelection);
    if (FAILED(hr) || enumComp == nullptr)
    {
        pPropComp->Release();
        return FALSE;
    }

    BOOL isCompExist = FALSE;
    VARIANT var;
    ULONG  fetched = 0;

    while (enumComp->Next(1, ppRange, &fetched) == S_OK && fetched == 1)
    {
        hr = pPropComp->GetValue(ec, *ppRange, &var);
        if (hr == S_OK)
        {
            if (var.vt == VT_I4 && var.lVal != 0)
            {
                isCompExist = TRUE;
                break;
            }
        }
        (*ppRange)->Release();
        *ppRange = nullptr;
    }

    pPropComp->Release();
    enumComp->Release();

    return isCompExist;
}

//+---------------------------------------------------------------------------
//
// _SetInputString
//
//----------------------------------------------------------------------------

HRESULT CTSFTTS::_SetInputString(TfEditCookie ec, _In_ ITfContext *pContext, _Out_opt_ ITfRange *pRange, _In_ CStringRange *pstrAddString, BOOL exist_composing)
{

    ITfRange* pRangeInsert = nullptr;
    if (!exist_composing)
    {
        _InsertAtSelection(ec, pContext, pstrAddString, &pRangeInsert);
        if (pRangeInsert == nullptr)
        {
            return S_OK;
        }
        pRange = pRangeInsert;
    }
    if (pRange != nullptr)
    {
        pRange->SetText(ec, 0, pstrAddString->Get(), (LONG)pstrAddString->GetLength());
    }

    _SetCompositionLanguage(ec, pContext);

    _SetCompositionDisplayAttributes(ec, pContext, _gaDisplayAttributeInput);

    // update the selection, we'll make it an insertion point just past
    // the inserted text.
    ITfRange* pSelection = nullptr;
    TF_SELECTION sel;

    if ((pRange != nullptr) && (pRange->Clone(&pSelection) == S_OK))
    {
        pSelection->Collapse(ec, TF_ANCHOR_END);

        sel.range = pSelection;
        sel.style.ase = TF_AE_NONE;
        sel.style.fInterimChar = FALSE;
        pContext->SetSelection(ec, 1, &sel);
        pSelection->Release();
    }

    if (pRangeInsert)
    {
        pRangeInsert->Release();
    }


    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _InsertAtSelection
//
//----------------------------------------------------------------------------

HRESULT CTSFTTS::_InsertAtSelection(TfEditCookie ec, _In_ ITfContext *pContext, _In_ CStringRange *pstrAddString, _Outptr_ ITfRange **ppCompRange)
{
    ITfRange* rangeInsert = nullptr;
    ITfInsertAtSelection* pias = nullptr;
    HRESULT hr = S_OK;

    if (ppCompRange == nullptr)
    {
        hr = E_INVALIDARG;
        goto Exit;
    }

    *ppCompRange = nullptr;

    hr = pContext->QueryInterface(IID_ITfInsertAtSelection, (void **)&pias);
    if (FAILED(hr))
    {
        goto Exit;
    }

    hr = pias->InsertTextAtSelection(ec, TF_IAS_QUERYONLY, pstrAddString->Get(), (LONG)pstrAddString->GetLength(), &rangeInsert);

    if ( FAILED(hr) || rangeInsert == nullptr)
    {
        rangeInsert = nullptr;
        pias->Release();
        goto Exit;
    }

    *ppCompRange = rangeInsert;
    pias->Release();
    hr = S_OK;

Exit:
    return hr;
}

//+---------------------------------------------------------------------------
//
// _RemoveDummyCompositionForComposing
//
//----------------------------------------------------------------------------

HRESULT CTSFTTS::_RemoveDummyCompositionForComposing
	(TfEditCookie ec, _In_ ITfComposition *pComposition)
{
	debugPrint(L"CTSFTTS::_RemoveDummyCompositionForComposing()\n");
    HRESULT hr = S_OK;

    ITfRange* pRange = nullptr;
    
    if (pComposition)
    {
        hr = pComposition->GetRange(&pRange);
        if (SUCCEEDED(hr))
        {
            pRange->SetText(ec, 0, nullptr, 0);
            pRange->Release();
        }
    }

    return hr;
}

//+---------------------------------------------------------------------------
//
// _SetCompositionLanguage
//
//----------------------------------------------------------------------------

BOOL CTSFTTS::_SetCompositionLanguage(TfEditCookie ec, _In_ ITfContext *pContext)
{
    HRESULT hr = S_OK;
    BOOL ret = TRUE;

    ITfRange* pRangeComposition = nullptr;
    ITfProperty* pLanguageProperty = nullptr;

    // we need a range and the context it lives in
    hr = _pComposition->GetRange(&pRangeComposition);
    if (FAILED(hr))
    {
        ret = FALSE;
        goto Exit;
    }

    // get our the language property
    hr = pContext->GetProperty(GUID_PROP_LANGID, &pLanguageProperty);
    if (FAILED(hr))
    {
        ret = FALSE;
        goto Exit;
    }

    VARIANT var;
    var.vt = VT_I4;   // we're going to set DWORD
    var.lVal = _langid; 

    hr = pLanguageProperty->SetValue(ec, pRangeComposition, &var);
    if (FAILED(hr))
    {
        ret = FALSE;
        goto Exit;
    }

    pLanguageProperty->Release();
    pRangeComposition->Release();

Exit:
    return ret;
}


//+---------------------------------------------------------------------------
//
// CProbeComposistionEditSession
//
//----------------------------------------------------------------------------

class CProbeComposistionEditSession : public CEditSessionBase
{
public:
    CProbeComposistionEditSession(_In_ CTSFTTS *pTextService, _In_ ITfContext *pContext) : CEditSessionBase(pTextService, pContext)
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

STDAPI CProbeComposistionEditSession::DoEditSession(TfEditCookie ec)
{
	debugPrint(L"CProbeComposistionEditSession::DoEditSession()\n");
	_pTextService->_ProbeCompositionRangeNotification(ec, _pContext);
	
    return S_OK;
}

//////////////////////////////////////////////////////////////////////
//
// CTSFTTS class
//
//////////////////////////////////////////////////////////////////////+---------------------------------------------------------------------------
//
// _ProbeComposition
//
// this starts the new (std::nothrow) composition at the selection of the current 
// focus context.
//----------------------------------------------------------------------------

void CTSFTTS::_ProbeComposition(_In_ ITfContext *pContext)
{
	debugPrint(L"CTSFTTS::_ProbeComposition()\n");
	CProbeComposistionEditSession* pProbeComposistionEditSession = new (std::nothrow) CProbeComposistionEditSession(this, pContext);

	if (nullptr != pProbeComposistionEditSession)
	{
		HRESULT hr = S_OK;
		pContext->RequestEditSession(_tfClientId, pProbeComposistionEditSession, TF_ES_SYNC | TF_ES_READWRITE, &hr);

		pProbeComposistionEditSession->Release();
	}
	
	_EndComposition(pContext);
	
   
}

HRESULT CTSFTTS::_ProbeCompositionRangeNotification(_In_ TfEditCookie ec, _In_ ITfContext *pContext)
{
	debugPrint(L"CTSFTTS::_ProbeCompositionRangeNotification()\n");
	HRESULT hr = S_OK;
	if(!_IsComposing())
		_StartComposition(pContext);
	CStringRange empty;
	hr = _AddComposingAndChar(ec, pContext, &empty.Set(L" ", 1));
	
	ITfRange *pRange;
	ITfContextView* pContextView;
	ITfDocumentMgr* pDocumgr;
	if (_pComposition&&SUCCEEDED(_pComposition->GetRange(&pRange)))
	{
		if(SUCCEEDED(pContext->GetActiveView(&pContextView)))
		{
			if(SUCCEEDED( pContext->GetDocumentMgr(&pDocumgr)))
			{
				_pUIPresenter->_StartLayout(pContext, ec, pRange);

				pDocumgr->Release();
			}
		pContextView->Release();
		}
		pRange->Release();
	}

	RECT rcTextExt;
	if (SUCCEEDED(_pUIPresenter->_GetTextExt(&rcTextExt)) 
		&& (rcTextExt.bottom - rcTextExt.top >1) && (rcTextExt.right - rcTextExt.left >1)  ) // confirm the extent rect is valid.
	{
		_pUIPresenter->_LayoutChangeNotification(&rcTextExt);
	}
	
    
	
	return hr;
}
