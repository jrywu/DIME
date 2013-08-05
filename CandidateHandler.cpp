#define DEBUG_PRINT

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

	debugPrint(L"CTSFDayi::_HandleCandidateWorker() \n");
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
	
	if (candidatePhraseList.Count())
	{
		CStringRange emptyComposition;
		if (!_IsComposing())
			_StartComposition(pContext);  //StartCandidateList require a valid selection from a valid pComposition to determine the location to show the candidate window
		_AddComposingAndChar(ec, pContext, &emptyComposition.Set(L" ",1)); 
		_candidateMode = CANDIDATE_PHRASE;
		_isCandidateWithWildcard = FALSE;
		if(SUCCEEDED(_CreateAndStartCandidate(_pCompositionProcessorEngine, ec, pContext)))
		{		
			_pTSFDayiUIPresenter->_SetCandidateTextColor(RGB(0, 0x80, 0), GetSysColor(COLOR_WINDOW));    // Text color is green
			_pTSFDayiUIPresenter->_SetCandidateFillColor((HBRUSH)(COLOR_WINDOW+1));    // Background color is window
			_pTSFDayiUIPresenter->_SetCandidateText(&candidatePhraseList, TRUE);
			_pTSFDayiUIPresenter->_SetCandidateSelection(-1, FALSE); // set selected index to -1 if showing phrase candidates
			_phraseCandShowing = TRUE;
			OutputDebugString(L"CTSFDayi::_HandleCandidateWorker(); _phraseCandShowing = TRUE. phrase cand is showing\n");

		}
	}
	
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
        if (_pTSFDayiUIPresenter->_SetCandidateSelectionInPage(iSelectAsNumber))
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
        if (_pTSFDayiUIPresenter->_SetCandidateSelectionInPage(iSelectAsNumber))
        {
            return _HandlePhraseFinalize(ec, pContext);
        }
    }

    return S_FALSE;
}

//+---------------------------------------------------------------------------
//
// _CreateAndStartCandidate
//
//----------------------------------------------------------------------------

HRESULT CTSFDayi::_CreateAndStartCandidate(_In_ CCompositionProcessorEngine *pCompositionProcessorEngine, TfEditCookie ec, _In_ ITfContext *pContext)
{
    HRESULT hr = S_OK;


	/*
	if(_pTSFDayiUIPresenter)
    {
        // Recreate the candidate list without destroy _pTSFDayiUIPresenter. simply end the original candidatelist
        //_pTSFDayiUIPresenter->_EndCandidateList();
		_pTSFDayiUIPresenter->_ClearCandidateList();
    }
	else
    {
        _pTSFDayiUIPresenter = new (std::nothrow) CTSFDayiUIPresenter(this, pCompositionProcessorEngine);
        if (!_pTSFDayiUIPresenter)
        {
            return E_OUTOFMEMORY;
        }	
    }

	// we don't cache the document manager object. So get it from pContext.
	ITfDocumentMgr* pDocumentMgr = nullptr;
	if (SUCCEEDED(pContext->GetDocumentMgr(&pDocumentMgr)))
	{
		// get the composition range. the candidate window need the composition range rect to place the window.
		ITfRange* pRange = nullptr;
		if (SUCCEEDED(_pComposition->GetRange(&pRange)))
		{
			hr = _pTSFDayiUIPresenter->_StartCandidateList(_tfClientId, pDocumentMgr, pContext, ec, pRange, pCompositionProcessorEngine->GetCandidateWindowWidth());
			pRange->Release();
		}
		pDocumentMgr->Release();
	}
	*/
	if (((_candidateMode == CANDIDATE_PHRASE) && (_pTSFDayiUIPresenter))
        || ((_candidateMode == CANDIDATE_NONE) && (_pTSFDayiUIPresenter)))
    {
        // Recreate candidate list
        _pTSFDayiUIPresenter->_EndCandidateList();
        delete _pTSFDayiUIPresenter;
        _pTSFDayiUIPresenter = nullptr;

    }

    if (_pTSFDayiUIPresenter == nullptr)
    {
        _pTSFDayiUIPresenter = new (std::nothrow) CTSFDayiUIPresenter(this, pCompositionProcessorEngine);
        if (!_pTSFDayiUIPresenter)
        {
            return E_OUTOFMEMORY;
        }

 
        // we don't cache the document manager object. So get it from pContext.
        ITfDocumentMgr* pDocumentMgr = nullptr;
        if (SUCCEEDED(pContext->GetDocumentMgr(&pDocumentMgr)))
        {
            // get the composition range.
            ITfRange* pRange = nullptr;
            if (SUCCEEDED(_pComposition->GetRange(&pRange)))
            {
                hr = _pTSFDayiUIPresenter->_StartCandidateList(_tfClientId, pDocumentMgr, pContext, ec, pRange, pCompositionProcessorEngine->GetCandidateWindowWidth());
                pRange->Release();
            }
            pDocumentMgr->Release();
        }
    }
	return hr;
}


//+---------------------------------------------------------------------------
//
// _DeleteCandidateList
//
//----------------------------------------------------------------------------

VOID CTSFDayi::_DeleteCandidateList(BOOL isForce, _In_opt_ ITfContext *pContext)
{
	OutputDebugString(L"CTSFDayi::_DeleteCandidateList()\n");
    isForce;pContext;

    CCompositionProcessorEngine* pCompositionProcessorEngine = nullptr;
    pCompositionProcessorEngine = _pCompositionProcessorEngine;
    pCompositionProcessorEngine->PurgeVirtualKey();

    if (_pTSFDayiUIPresenter)
    {
        _pTSFDayiUIPresenter->_EndCandidateList();

        _candidateMode = CANDIDATE_NONE;
        _isCandidateWithWildcard = FALSE;
    }
}
