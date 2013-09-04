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
#include "UIPresenter.h"
#include "CompositionProcessorEngine.h"
#include "BaseStructure.h"

//////////////////////////////////////////////////////////////////////
//
// CTSFTTS class
//
//////////////////////////////////////////////////////////////////////+---------------------------------------------------------------------------
//
// _IsRangeCovered
//
// Returns TRUE if pRangeTest is entirely contained within pRangeCover.
//
//----------------------------------------------------------------------------

BOOL CTSFTTS::_IsRangeCovered(TfEditCookie ec, _In_ ITfRange *pRangeTest, _In_ ITfRange *pRangeCover)
{
    LONG lResult = 0;;

    if (FAILED(pRangeCover->CompareStart(ec, pRangeTest, TF_ANCHOR_START, &lResult)) 
        || (lResult > 0))
    {
        return FALSE;
    }

    if (FAILED(pRangeCover->CompareEnd(ec, pRangeTest, TF_ANCHOR_END, &lResult)) 
        || (lResult < 0))
    {
        return FALSE;
    }

    return TRUE;
}

//+---------------------------------------------------------------------------
//
// _HandleComplete
//
//----------------------------------------------------------------------------

HRESULT CTSFTTS::_HandleComplete(TfEditCookie ec, _In_ ITfContext *pContext)
{
	debugPrint(L"CTSFTTS::_HandleComplete()");
    _DeleteCandidateList(FALSE, pContext);

    // just terminate the composition
    _TerminateComposition(ec, pContext);

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _HandleCancel
//
//----------------------------------------------------------------------------

HRESULT CTSFTTS::_HandleCancel(TfEditCookie ec, _In_ ITfContext *pContext)
{
	debugPrint(L"CTSFTTS::_HandleCancel()");

    _RemoveDummyCompositionForComposing(ec, _pComposition);

    _DeleteCandidateList(TRUE, pContext);

    _TerminateComposition(ec, pContext);

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _HandleCompositionInput
//
// If the keystroke happens within a composition, eat the key and return S_OK.
//
//----------------------------------------------------------------------------

HRESULT CTSFTTS::_HandleCompositionInput(TfEditCookie ec, _In_ ITfContext *pContext, WCHAR wch)
{
	debugPrint(L"CTSFTTS::_HandleCompositionInput(), _candidateMode = %d", _candidateMode );
    ITfRange* pRangeComposition = nullptr;
    TF_SELECTION tfSelection;
    ULONG fetched = 0;
    BOOL isCovered = TRUE;

    CCompositionProcessorEngine* pCompositionProcessorEngine = nullptr;
    pCompositionProcessorEngine = _pCompositionProcessorEngine;


	if (_pUIPresenter 
		&& _candidateMode != CANDIDATE_INCREMENTAL &&_candidateMode != CANDIDATE_NONE )
    {
        _HandleCompositionFinalize(ec, pContext, TRUE);
    }

    // Start the new (std::nothrow) compositon if there is no composition.
    if (!_IsComposing())
    {
        _StartComposition(pContext);
    }

    // first, test where a keystroke would go in the document if we did an insert
    if (pContext->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &tfSelection, &fetched) != S_OK || fetched != 1)
    {
        return S_FALSE;
    }

    // is the insertion point covered by a composition?
    if (SUCCEEDED(_pComposition->GetRange(&pRangeComposition)))
    {
        isCovered = _IsRangeCovered(ec, tfSelection.range, pRangeComposition);

        pRangeComposition->Release();

        if (!isCovered)
        {
            goto Exit;
        }
    }

    // Add virtual key to composition processor engine
    pCompositionProcessorEngine->AddVirtualKey(wch);

    _HandleCompositionInputWorker(pCompositionProcessorEngine, ec, pContext);

Exit:
    tfSelection.range->Release();
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _HandleCompositionInputWorker
//
// If the keystroke happens within a composition, eat the key and return S_OK.
//
//----------------------------------------------------------------------------

HRESULT CTSFTTS::_HandleCompositionInputWorker(_In_ CCompositionProcessorEngine *pCompositionProcessorEngine, TfEditCookie ec, _In_ ITfContext *pContext)
{
	debugPrint(L"CTSFTTS::_HandleCompositionInputWorker()");
    HRESULT hr = S_OK;
    CTSFTTSArray<CStringRange> readingStrings;
    BOOL isWildcardIncluded = TRUE;

    //
    // Get reading string from composition processor engine
    //
    pCompositionProcessorEngine->GetReadingStrings(&readingStrings, &isWildcardIncluded);

    for (UINT index = 0; index < readingStrings.Count(); index++)
    {
		hr = _AddComposingAndChar(ec, pContext, readingStrings.GetAt(index));

		if(readingStrings.GetAt(index)) delete [] readingStrings.GetAt(index)->Get();
		
        if (FAILED(hr))
        {
            return hr;
        }
    }

    //
    // Get candidate string from composition processor engine
    //
	BOOL symbolMode = pCompositionProcessorEngine->IsSymbol();
	if(CConfig::GetAutoCompose() // auto composing mode: show candidates while composition updated imeediately.
		|| symbolMode // fetch candidate in symobl mode with composition started with '='(DAYI) or 'W' (Array)
		||  Global::imeMode== IME_MODE_ARRAY) //
	{
		CTSFTTSArray<CCandidateListItem> candidateList;
	
		pCompositionProcessorEngine->GetCandidateList(&candidateList, !(pCompositionProcessorEngine->IsSymbol()|| Global::imeMode== IME_MODE_ARRAY ), FALSE);
		
		UINT nCount = candidateList.Count();

		if (nCount)
		{
			hr = _CreateAndStartCandidate(pCompositionProcessorEngine, ec, pContext);
			if (SUCCEEDED(hr))
			{
				_pUIPresenter->_ClearCandidateList();
				_pUIPresenter->_SetCandidateTextColor(CConfig::GetItemColor(), CConfig::GetItemBGColor()); 
				_pUIPresenter->_SetCandidateSelectedTextColor(CConfig::GetSelectedColor(), CConfig::GetSelectedBGColor());    
				_pUIPresenter->_SetCandidateNumberColor(CConfig::GetNumberColor(), CConfig::GetItemBGColor());    
				_pUIPresenter->_SetCandidateFillColor(CConfig::GetItemBGColor());
				_pUIPresenter->_SetCandidateText(&candidateList, TRUE, pCompositionProcessorEngine->GetCandidateWindowWidth());
				_candidateMode = CANDIDATE_INCREMENTAL;
				_isCandidateWithWildcard = FALSE;
			}
			if(nCount==1 && symbolMode )  //finalized with the only candidate without showing cand.
			{
				_HandleCandidateFinalize(ec, pContext);
				return hr;
			}
			


		}
		else if (_pUIPresenter)
		{
			_pUIPresenter->_ClearCandidateList();
			_pUIPresenter->Show(FALSE);  // hide the candidate window if now candidates in autocompose mode
		}
		else if (readingStrings.Count() && isWildcardIncluded)
		{
			
			hr = _CreateAndStartCandidate(pCompositionProcessorEngine, ec, pContext);
			if (SUCCEEDED(hr))
			{
				_pUIPresenter->_ClearCandidateList();
				_candidateMode = CANDIDATE_INCREMENTAL;
				_isCandidateWithWildcard = FALSE;
			}
		}

	}
    return hr;
}


//+---------------------------------------------------------------------------
//
// _HandleCompositionFinalize
//
//----------------------------------------------------------------------------

HRESULT CTSFTTS::_HandleCompositionFinalize(TfEditCookie ec, _In_ ITfContext *pContext, BOOL isCandidateList)
{
	debugPrint(L"CTSFTTS::_HandleCompositionFinalize()");
    HRESULT hr = S_OK;

    if (isCandidateList && _pUIPresenter)
    {
        // Finalize selected candidate string from UIPresenter
        DWORD_PTR candidateLen = 0;
        const WCHAR *pCandidateString = nullptr;

        candidateLen = _pUIPresenter->_GetSelectedCandidateString(&pCandidateString);

        CStringRange candidateString;
        candidateString.Set(pCandidateString, candidateLen);

        if (candidateLen)
        {
            // Finalize character
            hr = _AddCharAndFinalize(ec, pContext, &candidateString);
            if (FAILED(hr))
            {
                return hr;
            }
        }
		else
			_HandleCancel(ec, pContext);
    }
    else
    {
        // Finalize current text store strings
        if (_IsComposing())
        {
            ULONG fetched = 0;
            TF_SELECTION tfSelection;

            if (FAILED(pContext->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &tfSelection, &fetched)) || fetched != 1)
            {
                return S_FALSE;
            }

            ITfRange* pRangeComposition = nullptr;
            if (SUCCEEDED(_pComposition->GetRange(&pRangeComposition)))
            {
                if (_IsRangeCovered(ec, tfSelection.range, pRangeComposition))
                {
                    _EndComposition(pContext);
                }

                pRangeComposition->Release();
            }

            tfSelection.range->Release();
        }
    }

    //_HandleCancel(ec, pContext);

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _HandleCompositionConvert
//
//----------------------------------------------------------------------------

HRESULT CTSFTTS::_HandleCompositionConvert(TfEditCookie ec, _In_ ITfContext *pContext, BOOL isWildcardSearch)
{
    HRESULT hr = S_OK;

    CTSFTTSArray<CCandidateListItem> candidateList;

    //
    // Get candidate string from composition processor engine
    //
    CCompositionProcessorEngine* pCompositionProcessorEngine = nullptr;
    pCompositionProcessorEngine = _pCompositionProcessorEngine;
    pCompositionProcessorEngine->GetCandidateList(&candidateList, FALSE, isWildcardSearch);

    // If there is no candlidate listin the current reading string, we don't do anything. Just wait for
    // next char to be ready for the conversion with it.
    int nCount = candidateList.Count();
    if (nCount)
    {
		 if (SUCCEEDED(_CreateAndStartCandidate(pCompositionProcessorEngine, ec, pContext)))
		 {
			_candidateMode = CANDIDATE_ORIGINAL;
			 _isCandidateWithWildcard = isWildcardSearch;
			 _pUIPresenter->_ClearCandidateList();
			 _pUIPresenter->_SetCandidateTextColor(CConfig::GetItemColor(), CConfig::GetItemBGColor());    
			 _pUIPresenter->_SetCandidateSelectedTextColor(CConfig::GetSelectedColor(), CConfig::GetSelectedBGColor());    
			 _pUIPresenter->_SetCandidateNumberColor(CConfig::GetNumberColor(), CConfig::GetItemBGColor());    
			 _pUIPresenter->_SetCandidateFillColor(CConfig::GetItemBGColor());
			 _pUIPresenter->_SetCandidateText(&candidateList, FALSE, pCompositionProcessorEngine->GetCandidateWindowWidth());
		 }
		
    }
	else
	{
		pCompositionProcessorEngine->DoBeep();
	}
	if(nCount==1 )  //finalized with the only candidate without showing cand.
	{
		_HandleCandidateFinalize(ec, pContext);
	}

    return hr;
}

//+---------------------------------------------------------------------------
//
// _HandleCompositionBackspace
//
//----------------------------------------------------------------------------

HRESULT CTSFTTS::_HandleCompositionBackspace(TfEditCookie ec, _In_ ITfContext *pContext)
{
    ITfRange* pRangeComposition = nullptr;
    TF_SELECTION tfSelection;
    ULONG fetched = 0;
    BOOL isCovered = TRUE;

    // Start the new (std::nothrow) compositon if there is no composition.
    if (!_IsComposing())
    {
        return S_OK;
    }

    // first, test where a keystroke would go in the document if we did an insert
    if (FAILED(pContext->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &tfSelection, &fetched)) || fetched != 1)
    {
        return S_FALSE;
    }

    // is the insertion point covered by a composition?
    if (SUCCEEDED(_pComposition->GetRange(&pRangeComposition)))
    {
        isCovered = _IsRangeCovered(ec, tfSelection.range, pRangeComposition);

        pRangeComposition->Release();

        if (!isCovered)
        {
            goto Exit;
        }
    }

    //
    // Add virtual key to composition processor engine
    //
    CCompositionProcessorEngine* pCompositionProcessorEngine = nullptr;
    pCompositionProcessorEngine = _pCompositionProcessorEngine;

    DWORD_PTR vKeyLen = pCompositionProcessorEngine->GetVirtualKeyLength();

    if (vKeyLen)
    {
        pCompositionProcessorEngine->RemoveVirtualKey(vKeyLen - 1);

        if (pCompositionProcessorEngine->GetVirtualKeyLength())
        {
            _HandleCompositionInputWorker(pCompositionProcessorEngine, ec, pContext);
        }
        else
        {
            _HandleCancel(ec, pContext);
        }
    }

Exit:
    tfSelection.range->Release();
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _HandleCompositionArrowKey
//
// Update the selection within a composition.
//
//----------------------------------------------------------------------------

HRESULT CTSFTTS::_HandleCompositionArrowKey(TfEditCookie ec, _In_ ITfContext *pContext, KEYSTROKE_FUNCTION keyFunction)
{
    ITfRange* pRangeComposition = nullptr;
    TF_SELECTION tfSelection;
    ULONG fetched = 0;

    // get the selection
    if (FAILED(pContext->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &tfSelection, &fetched))
        || fetched != 1)
    {
        // no selection, eat the keystroke
        return S_OK;
    }

    // get the composition range
    if (FAILED(_pComposition->GetRange(&pRangeComposition)))
    {
        goto Exit;
    }

    // For incremental candidate list
    if (_pUIPresenter)
    {
        _pUIPresenter->AdviseUIChangedByArrowKey(keyFunction);
    }

    pContext->SetSelection(ec, 1, &tfSelection);

    pRangeComposition->Release();

Exit:
    tfSelection.range->Release();
    return S_OK;
}


//+---------------------------------------------------------------------------
//
// _HandleCompositionDoubleSingleByte
//
//----------------------------------------------------------------------------

HRESULT CTSFTTS::_HandleCompositionDoubleSingleByte(TfEditCookie ec, _In_ ITfContext *pContext, WCHAR wch)
{
    HRESULT hr = S_OK;

    WCHAR fullWidth = Global::FullWidthCharTable[wch - 0x20];

    CStringRange fullWidthString;
    fullWidthString.Set(&fullWidth, 1);

    // Finalize character
    hr = _AddCharAndFinalize(ec, pContext, &fullWidthString);
    if (FAILED(hr))
    {
        return hr;
    }

    _HandleCancel(ec, pContext);

    return S_OK;
}


//+---------------------------------------------------------------------------
//
// _HandleAddressChar
//
//----------------------------------------------------------------------------

HRESULT CTSFTTS::_HandleCompositionAddressChar(TfEditCookie ec, _In_ ITfContext *pContext, WCHAR wch)
{
	HRESULT hr = S_OK;

   //
    // Get punctuation char from composition processor engine
    //
    CCompositionProcessorEngine* pCompositionProcessorEngine = nullptr;
    pCompositionProcessorEngine = _pCompositionProcessorEngine;

	WCHAR addressChar = pCompositionProcessorEngine->GetDayiAddressChar(wch);

    CStringRange addressCharString;
    addressCharString.Set(&addressChar, 1);

    // Finalize character
    hr = _AddCharAndFinalize(ec, pContext, &addressCharString);
    if (FAILED(hr))
    {
        return hr;
    }

    _HandleCancel(ec, pContext);

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _InvokeKeyHandler
//
// This text service is interested in handling keystrokes to demonstrate the
// use the compositions. Some apps will cancel compositions if they receive
// keystrokes while a compositions is ongoing.
//
// param
//    [in] uCode - virtual key code of WM_KEYDOWN wParam
//    [in] dwFlags - WM_KEYDOWN lParam
//    [in] dwKeyFunction - Function regarding virtual key
//----------------------------------------------------------------------------

HRESULT CTSFTTS::_InvokeKeyHandler(_In_ ITfContext *pContext, UINT code, WCHAR wch, DWORD flags, _KEYSTROKE_STATE keyState)
{
    flags;

    CKeyHandlerEditSession* pEditSession = nullptr;
    HRESULT hr = E_FAIL;

    // we'll insert a char ourselves in place of this keystroke
    pEditSession = new (std::nothrow) CKeyHandlerEditSession(this, pContext, code, wch, keyState);
    if (pEditSession == nullptr)
    {
        goto Exit;
    }

    //
    // Call CKeyHandlerEditSession::DoEditSession().
    //
    // Do not specify TF_ES_SYNC so edit session is not invoked on WinWord
    //
    hr = pContext->RequestEditSession(_tfClientId, pEditSession, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &hr);

    pEditSession->Release();

Exit:
    return hr;
}
