//#define DEBUG_PRINT

#include "DIME.h"
#include "BaseStructure.h"
#include "UIPresenter.h"
#include "CompositionProcessorEngine.h"


//+---------------------------------------------------------------------------
//
// _HandleCandidateFinalize
//
//----------------------------------------------------------------------------

HRESULT CDIME::_HandleCandidateFinalize(TfEditCookie ec, _In_ ITfContext *pContext)
{
	return _HandleCandidateWorker(ec, pContext);
}

//+---------------------------------------------------------------------------
//
// _HandleCandidateConvert
//
//----------------------------------------------------------------------------

HRESULT CDIME::_HandleCandidateConvert(TfEditCookie ec, _In_ ITfContext *pContext)
{
    return _HandleCandidateWorker(ec, pContext);
	
}

//+---------------------------------------------------------------------------
//
// _HandleCandidateWorker
//
//----------------------------------------------------------------------------

HRESULT CDIME::_HandleCandidateWorker(TfEditCookie ec, _In_ ITfContext *pContext)
{

	debugPrint(L"CDIME::_HandleCandidateWorker() \n");
    HRESULT hr = S_OK;
	CStringRange commitString, convertedString;
	CDIMEArray<CCandidateListItem> candidatePhraseList;	
	CStringRange lastChar;
	CStringRange notify;
	
    if (nullptr == _pUIPresenter)
    {
        goto Exit; //should not happen
    }
	
	const WCHAR* pCandidateString = nullptr;
	DWORD_PTR candidateLen = 0;
	BOOL arrayUsingSPCode =FALSE;

	if (!_IsComposing())
		_StartComposition(pContext);

	if (Global::imeMode == IME_MODE_ARRAY)// check if the _strokebuffer is array special code
	{
		candidateLen = _pCompositionProcessorEngine->CollectWordFromArraySpeicalCode(&pCandidateString);
		if(candidateLen) arrayUsingSPCode = TRUE;
	}
	candidateLen = 0;
	candidateLen = _pUIPresenter->_GetSelectedCandidateString(&pCandidateString);
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
			_HandleCancel(ec, pContext);
			DoBeep(BEEP_COMPOSITION_ERROR); //beep for no valid mapping found
			goto Exit;
		}
    }
	if (_pCompositionProcessorEngine->IsArrayShortCode() && candidateLen == 1 && *pCandidateString == 0x2394) // empty position in arry short code table.
	{
		hr = S_FALSE;
		if (Global::imeMode == IME_MODE_PHONETIC)
			DoBeep(BEEP_WARNING);
		else
		{
			_HandleCancel(ec, pContext);
			DoBeep(BEEP_COMPOSITION_ERROR); //beep for no valid mapping found
		}
		goto Exit;
	}
	
	
	StringCchCopy(_commitString, 1, L"\0");
	StringCchCatN(_commitString, MAX_COMMIT_LENGTH, pCandidateString, candidateLen);
	commitString.Set(_commitString, candidateLen);
	//_commitString = commitString;
	
	PWCHAR pwch = new (std::nothrow) WCHAR[2];  // pCandidateString will be destroyed after _detelteCanddiateList was called.
	if(candidateLen > 1)
	{	
		StringCchCopyN(pwch, 2, pCandidateString + candidateLen -1, 1); 
	}else // cnadidateLen ==1
	{
		StringCchCopyN(pwch, 2, pCandidateString, 1); 	
	}
	lastChar.Set(pwch, 1 );
	//-----------------do reverse conversion notify. We should not show notify in UI-less mode, thus cancel reverse conversion notify in UILess Mode
	if(_pITfReverseConversion[Global::imeMode] && !_IsUILessMode())
	{
		_AsyncReverseConversion(pContext); //asynchronized the reverse conversion with editsession for better perfomance
	}
	//-----------------do  array spcial code notify. We should not show notify in UI-less mode, thus cancel forceSP mode in UILess Mode---
	BOOL ArraySPFound = FALSE;            
	if(Global::imeMode == IME_MODE_ARRAY && !_IsUILessMode()  && !arrayUsingSPCode && (CConfig::GetArrayForceSP() || CConfig::GetArrayNotifySP()))
	{
		CStringRange specialCode;
		CStringRange notifyText;
		ArraySPFound = _pCompositionProcessorEngine->GetArraySpeicalCodeFromConvertedText(&commitString, &specialCode); 
		if(specialCode.Get())
			_pUIPresenter->ShowNotifyText(&specialCode);
	}
	convertedString = commitString;
	//----------------- do TC to SC covert if required----------------------------------------------------------------------------------
	if(CConfig::GetDoHanConvert())
	{
		_pCompositionProcessorEngine->GetSCFromTC(&commitString, &convertedString);
	}
	//----------------- commit the selected string  ------------------------------------------------------------------------------------ 
	if(Global::imeMode == IME_MODE_ARRAY && !_IsUILessMode()  && !arrayUsingSPCode && CConfig::GetArrayForceSP() &&  ArraySPFound )
	{
		_HandleCancel(ec, pContext);
		DoBeep(BEEP_COMPOSITION_ERROR);
		return hr;
	}
	else
	{
		hr = _AddComposingAndChar(ec, pContext, &convertedString);
		if (FAILED(hr))	return hr;
		// Do not send _endcandidatelist (or handleComplete) here to avoid cand dissapear in win8 metro
		_HandleComplete(ec,pContext);
	}
	//-----------------do accociated phrase (make phrase)--------------------------------------------------------------------------------
	if (CConfig::GetMakePhrase())
	{
		_pCompositionProcessorEngine->GetCandidateStringInConverted(lastChar, &candidatePhraseList);


		// We have a candidate list if candidatePhraseList.Cnt is not 0
		// If we are showing reverse conversion, use UIPresenter

		if (candidatePhraseList.Count())
		{
			CStringRange emptyComposition;
			if (!_IsComposing())
				_StartComposition(pContext);  //StartCandidateList require a valid selection from a valid pComposition to determine the location to show the candidate window
			_AddComposingAndChar(ec, pContext, &emptyComposition.Set(L" ",1)); 

			_pUIPresenter->_ClearCandidateList();
			_pUIPresenter->_SetCandidateTextColor(CConfig::GetPhraseColor(), CConfig::GetItemBGColor());    // Text color is green
			_pUIPresenter->_SetCandidateSelectedTextColor(CConfig::GetSelectedColor(), CConfig::GetSelectedBGColor());    
			_pUIPresenter->_SetCandidateNumberColor(CConfig::GetNumberColor(), CConfig::GetItemBGColor());    
			_pUIPresenter->_SetCandidateFillColor(CConfig::GetItemBGColor());//(HBRUSH)(COLOR_WINDOW+1));    // Background color is window
			_pUIPresenter->_SetCandidateText(&candidatePhraseList, _pCompositionProcessorEngine->GetCandidateListIndexRange(),
				TRUE, _pCompositionProcessorEngine->GetCandidateWindowWidth());
			_pUIPresenter->_SetCandidateSelection(-1, FALSE); // set selected index to -1 if showing phrase candidates
			_candidateMode = CANDIDATE_PHRASE;
			_isCandidateWithWildcard = FALSE;	
			
		}
		else
		{   //cancel the composition if the phrase lookup return 0 results
			_HandleCancel(ec,pContext);
		}


	}
	else
	{
		_DeleteCandidateList(TRUE, pContext); // endCanddiateUI if not doing associated phrase.
	}
	
Exit:
    return hr;
}

//+---------------------------------------------------------------------------
//
// _HandleCandidateArrowKey
//
//----------------------------------------------------------------------------

HRESULT CDIME::_HandleCandidateArrowKey(TfEditCookie ec, _In_ ITfContext *pContext, _In_ KEYSTROKE_FUNCTION keyFunction)
{
    ec;
    pContext;

    _pUIPresenter->AdviseUIChangedByArrowKey(keyFunction);

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _HandleCandidateSelectByNumber
//
//----------------------------------------------------------------------------

HRESULT CDIME::_HandleCandidateSelectByNumber(TfEditCookie ec, _In_ ITfContext *pContext, _In_ UINT uCode)
{
	int iSelectAsNumber = _pCompositionProcessorEngine->GetCandidateListIndexRange()->GetIndex(uCode, _candidateMode);
    if (iSelectAsNumber == -1)
    {
        return S_FALSE;
    }

    if (_pUIPresenter)
    {
        if (_pUIPresenter->_SetCandidateSelectionInPage(iSelectAsNumber))
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

HRESULT CDIME::_HandlePhraseFinalize(TfEditCookie ec, _In_ ITfContext *pContext)
{
    HRESULT hr = S_OK;

    DWORD phraseLen = 0;
    const WCHAR* pPhraseString = nullptr;

    phraseLen = (DWORD)_pUIPresenter->_GetSelectedCandidateString(&pPhraseString);

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

HRESULT CDIME::_HandlePhraseArrowKey(TfEditCookie ec, _In_ ITfContext *pContext, _In_ KEYSTROKE_FUNCTION keyFunction)
{
    ec;
    pContext;

    _pUIPresenter->AdviseUIChangedByArrowKey(keyFunction);

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _HandlePhraseSelectByNumber
//
//----------------------------------------------------------------------------

HRESULT CDIME::_HandlePhraseSelectByNumber(TfEditCookie ec, _In_ ITfContext *pContext, _In_ UINT uCode)
{
	int iSelectAsNumber = _pCompositionProcessorEngine->GetCandidateListIndexRange()->GetIndex(uCode, _candidateMode);
    if (iSelectAsNumber == -1)
    {
        return S_FALSE;
    }

    if (_pUIPresenter)
    {
        if (_pUIPresenter->_SetCandidateSelectionInPage(iSelectAsNumber))
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

HRESULT CDIME::_CreateAndStartCandidate(_In_ CCompositionProcessorEngine *pCompositionProcessorEngine, TfEditCookie ec, _In_ ITfContext *pContext)
{
	debugPrint(L"CDIME::_CreateAndStartCandidate(), _candidateMode = %d", _candidateMode);
    HRESULT hr = S_OK;

	
	if ((_candidateMode == CANDIDATE_NONE) && (_pUIPresenter))
    {
 
		// we don't cache the document manager object. So get it from pContext.
		ITfDocumentMgr* pDocumentMgr = nullptr;
		if (SUCCEEDED(pContext->GetDocumentMgr(&pDocumentMgr)))
		{
			// get the composition range.
			ITfRange* pRange = nullptr;
			if (SUCCEEDED(_pComposition->GetRange(&pRange)))
			{
				hr = _pUIPresenter->_StartCandidateList(_tfClientId, pDocumentMgr, pContext, ec, pRange, pCompositionProcessorEngine->GetCandidateWindowWidth());
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

VOID CDIME::_DeleteCandidateList(BOOL isForce, _In_opt_ ITfContext *pContext)
{
	//isForce;
	pContext;
	debugPrint(L"CDIME::_DeleteCandidateList()\n");
	if(_pCompositionProcessorEngine) 
	{
	    _pCompositionProcessorEngine->PurgeVirtualKey();
	}

    if (_pUIPresenter && isForce)
    {
		_pUIPresenter->_EndCandidateList();
    }
	_candidateMode = CANDIDATE_NONE;
    _isCandidateWithWildcard = FALSE;
}
