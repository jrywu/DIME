#include "TSFDayi.h"
#include "TSFDayiBaseStructure.h"
#include "TSFDayiUIPresenter.h"
#include "CompositionProcessorEngine.h"


//+---------------------------------------------------------------------------
//
// _HandleCandidateFinalize
//
//----------------------------------------------------------------------------

HRESULT CTSFDayi::_HandleCandidateFinalize(TfEditCookie ec, _In_ ITfContext *pContext)
{
	return _HandleCandidateWorker(ec, pContext);
}

//+---------------------------------------------------------------------------
//
// _HandleCandidateConvert
//
//----------------------------------------------------------------------------

HRESULT CTSFDayi::_HandleCandidateConvert(TfEditCookie ec, _In_ ITfContext *pContext)
{
    return _HandleCandidateWorker(ec, pContext);
	
}

//+---------------------------------------------------------------------------
//
// _HandleCandidateWorker
//
//----------------------------------------------------------------------------

HRESULT CTSFDayi::_HandleCandidateWorker(TfEditCookie ec, _In_ ITfContext *pContext)
{
	OutputDebugString(L"CTSFDayi::_HandleCandidateWorker() \n");
    HRESULT hr = S_OK;
	CStringRange commitString;
	CTSFDayiArray<CCandidateListItem> candidatePhraseList;	
	CStringRange candidateString;
	
    if (nullptr == _pTSFDayiUIPresenter)
    {
        goto Exit; //should not happen
    }
	
	const WCHAR* pCandidateString = nullptr;
	DWORD_PTR candidateLen = 0;    

	if (!_IsComposing())
		_StartComposition(pContext);

	candidateLen = _pTSFDayiUIPresenter->_GetSelectedCandidateString(&pCandidateString);
	if (candidateLen == 0)
    {
		if(_candidateMode == CANDIDATE_WITH_NEXT_COMPOSITION || _candidateMode == CANDIDATE_PHRASE)
		{
			_HandleCancel(ec, pContext);
			goto Exit;
		}
		else
		{
			hr = S_FALSE;
			_pCompositionProcessorEngine->DoBeep(); //beep for no valid mapping found
			goto Exit;
		}
    }
	
	
	commitString.Set(pCandidateString , candidateLen );
	
	PWCHAR pwch = new (std::nothrow) WCHAR[2];  // pCandidateString will be destroyed after _detelteCanddiateList was called.
	pwch[1] = L'0';
	if(candidateLen > 1)
	{	
		StringCchCopyN(pwch, 2, pCandidateString + candidateLen -1, 1); 
	}else // cnadidateLen ==1
	{
		StringCchCopyN(pwch, 2, pCandidateString, 1); 	
	}
	candidateString.Set(pwch, 1 );

	hr = _AddComposingAndChar(ec, pContext, &commitString);
	if (FAILED(hr))	return hr;
	
	_HandleComplete(ec, pContext);
	
	

	BOOL fMakePhraseFromText = _pCompositionProcessorEngine->IsMakePhraseFromText();
	if (fMakePhraseFromText)
	{
		_pCompositionProcessorEngine->GetCandidateStringInConverted(candidateString, &candidatePhraseList);
		LCID locale = _pCompositionProcessorEngine->GetLocale();

		_pTSFDayiUIPresenter->RemoveSpecificCandidateFromList(locale, candidatePhraseList, candidateString);
	}
	
	// We have a candidate list if candidatePhraseList.Cnt is not 0
	// If we are showing reverse conversion, use CTSFDayiUIPresenter
	CANDIDATE_MODE tempCandMode = CANDIDATE_NONE;
	CTSFDayiUIPresenter* _pPhraseTSFDayiUIPresenter = nullptr;
	
	if (candidatePhraseList.Count())
	{
		tempCandMode =  CANDIDATE_PHRASE; 

		_pPhraseTSFDayiUIPresenter = new (std::nothrow) CTSFDayiUIPresenter(this, Global::AtomCandidateWindow,
			CATEGORY_CANDIDATE,
			_pCompositionProcessorEngine->GetCandidateListIndexRange(),
			FALSE,
			_pCompositionProcessorEngine);
		if (nullptr == _pPhraseTSFDayiUIPresenter)
		{
			hr = E_OUTOFMEMORY;
			goto Exit;
		}
	}
	else
		goto Exit;
	
	// call _Start*Line for CTSFDayiUIPresenter or CReadingLine
	// we don't cache the document manager object so get it from pContext.
	ITfDocumentMgr* pDocumentMgr = nullptr;
	HRESULT hrStartCandidateList = E_FAIL;
	if (pContext->GetDocumentMgr(&pDocumentMgr) == S_OK)
	{
		ITfRange* pRange = nullptr;
		CStringRange emptyComposition;
		if (!_IsComposing())
			_StartComposition(pContext);  //StartCandidateList require a valid selection from a valid pComposition to determine the location to show the candidate window
		
		// add a space character to empty composition buffer so as the phrase cand can showed in right position in non TSF award program.
		_AddComposingAndChar(ec, pContext, &emptyComposition.Set(L" ",1)); 
		if (_pComposition->GetRange(&pRange) == S_OK)
		{
			if (_pPhraseTSFDayiUIPresenter)
			{
				hrStartCandidateList = _pPhraseTSFDayiUIPresenter->_StartCandidateList(_tfClientId, pDocumentMgr, pContext, ec, pRange, _pCompositionProcessorEngine->GetCandidateWindowWidth());
			} 

			pRange->Release();
			
		}
		
		//_TerminateComposition(ec, pContext);
		pDocumentMgr->Release();
	}
	
	
	

	// set up candidate list if it is being shown
	if (SUCCEEDED(hrStartCandidateList))
	{
		_pPhraseTSFDayiUIPresenter->_SetTextColor(RGB(0, 0x80, 0), GetSysColor(COLOR_WINDOW));    // Text color is green
		_pPhraseTSFDayiUIPresenter->_SetFillColor((HBRUSH)(COLOR_WINDOW+1));    // Background color is window
		_pPhraseTSFDayiUIPresenter->_SetText(&candidatePhraseList, FALSE);

		
		// close candidate list
		if (_pTSFDayiUIPresenter)
		{
			_pTSFDayiUIPresenter->_EndCandidateList();
			delete _pTSFDayiUIPresenter;
			_pTSFDayiUIPresenter = nullptr;

			_candidateMode = CANDIDATE_NONE;
			_isCandidateWithWildcard = FALSE;
		}

		if (hr == S_OK)
		{
			// copy temp candidate
			_pTSFDayiUIPresenter = _pPhraseTSFDayiUIPresenter;
			_pTSFDayiUIPresenter->_SetSelection(-1, FALSE); // set selected index to -1 if showing phrase candidates

			_phraseCandShowing = TRUE;
			OutputDebugString(L"CTSFDayi::_HandleCandidateWorker(); _phraseCandShowing = TRUE. phrase cand is showing\n");

			_candidateMode = tempCandMode;
			_isCandidateWithWildcard = FALSE;
		}
		
	}
	_pPhraseTSFDayiUIPresenter = nullptr;

	// no next phrase list, exit without doing anything.
	
Exit:
    return hr;
}

//+---------------------------------------------------------------------------
//
// _HandleCandidateArrowKey
//
//----------------------------------------------------------------------------

HRESULT CTSFDayi::_HandleCandidateArrowKey(TfEditCookie ec, _In_ ITfContext *pContext, _In_ KEYSTROKE_FUNCTION keyFunction)
{
    ec;
    pContext;

    _pTSFDayiUIPresenter->AdviseUIChangedByArrowKey(keyFunction);

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _HandleCandidateSelectByNumber
//
//----------------------------------------------------------------------------

HRESULT CTSFDayi::_HandleCandidateSelectByNumber(TfEditCookie ec, _In_ ITfContext *pContext, _In_ UINT uCode)
{
	int iSelectAsNumber = _pCompositionProcessorEngine->GetCandidateListIndexRange()->GetIndex(uCode, _candidateMode);
    if (iSelectAsNumber == -1)
    {
        return S_FALSE;
    }

    if (_pTSFDayiUIPresenter)
    {
        if (_pTSFDayiUIPresenter->_SetSelectionInPage(iSelectAsNumber))
        {
            return _HandleCandidateConvert(ec, pContext);
        }
    }

    return S_FALSE;
}

//+---------------------------------------------------------------------------
//
// _HandlePhraseFinalize
//
//----------------------------------------------------------------------------

HRESULT CTSFDayi::_HandlePhraseFinalize(TfEditCookie ec, _In_ ITfContext *pContext)
{
    HRESULT hr = S_OK;

    DWORD phraseLen = 0;
    const WCHAR* pPhraseString = nullptr;

    phraseLen = (DWORD)_pTSFDayiUIPresenter->_GetSelectedCandidateString(&pPhraseString);

    CStringRange phraseString, clearString;
    phraseString.Set(pPhraseString, phraseLen);

    if (phraseLen)
    {

        if ((hr = _AddComposingAndChar(ec, pContext, &phraseString)) != S_OK)
        {
            return hr;
        }
    }

    _HandleComplete(ec, pContext);

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _HandlePhraseArrowKey
//
//----------------------------------------------------------------------------

HRESULT CTSFDayi::_HandlePhraseArrowKey(TfEditCookie ec, _In_ ITfContext *pContext, _In_ KEYSTROKE_FUNCTION keyFunction)
{
    ec;
    pContext;

    _pTSFDayiUIPresenter->AdviseUIChangedByArrowKey(keyFunction);

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _HandlePhraseSelectByNumber
//
//----------------------------------------------------------------------------

HRESULT CTSFDayi::_HandlePhraseSelectByNumber(TfEditCookie ec, _In_ ITfContext *pContext, _In_ UINT uCode)
{
	int iSelectAsNumber = _pCompositionProcessorEngine->GetCandidateListIndexRange()->GetIndex(uCode, _candidateMode);
    if (iSelectAsNumber == -1)
    {
        return S_FALSE;
    }

    if (_pTSFDayiUIPresenter)
    {
        if (_pTSFDayiUIPresenter->_SetSelectionInPage(iSelectAsNumber))
        {
            return _HandlePhraseFinalize(ec, pContext);
        }
    }

    return S_FALSE;
}
