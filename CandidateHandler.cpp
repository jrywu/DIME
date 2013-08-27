//#define DEBUG_PRINT

#include "TSFTTS.h"
#include "BaseStructure.h"
#include "UIPresenter.h"
#include "CompositionProcessorEngine.h"


//+---------------------------------------------------------------------------
//
// _HandleCandidateFinalize
//
//----------------------------------------------------------------------------

HRESULT CTSFTTS::_HandleCandidateFinalize(TfEditCookie ec, _In_ ITfContext *pContext)
{
	return _HandleCandidateWorker(ec, pContext);
}

//+---------------------------------------------------------------------------
//
// _HandleCandidateConvert
//
//----------------------------------------------------------------------------

HRESULT CTSFTTS::_HandleCandidateConvert(TfEditCookie ec, _In_ ITfContext *pContext)
{
    return _HandleCandidateWorker(ec, pContext);
	
}

//+---------------------------------------------------------------------------
//
// _HandleCandidateWorker
//
//----------------------------------------------------------------------------

HRESULT CTSFTTS::_HandleCandidateWorker(TfEditCookie ec, _In_ ITfContext *pContext)
{

	debugPrint(L"CTSFTTS::_HandleCandidateWorker() \n");
    HRESULT hr = S_OK;
	CStringRange commitString, convertedString;
	CTSFTTSArray<CCandidateListItem> candidatePhraseList;	
	CStringRange candidateString;
	
    if (nullptr == _pUIPresenter)
    {
        goto Exit; //should not happen
    }
	
	const WCHAR* pCandidateString = nullptr;
	DWORD_PTR candidateLen = 0;
	BOOL arrayUsingSPCode =FALSE;

	if (!_IsComposing())
		_StartComposition(pContext);

	if(Global::imeMode == IME_MODE_ARRAY)
	{
		candidateLen = _pCompositionProcessorEngine->CollectWordFromArraySpeicalCode(&pCandidateString);
		if(candidateLen) arrayUsingSPCode = TRUE;
	}
	if(candidateLen == 0)
	{
		candidateLen = _pUIPresenter->_GetSelectedCandidateString(&pCandidateString);
	}
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
	
	
	commitString.Set(pCandidateString , candidateLen);
	
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
	//-----------------do reverse conversion notify. We should not show notify in UI-less mode, thus cancel reverse conversion notify in UILess Mode
	if(_pITfReverseConversion[Global::imeMode] && !_IsUILessMode())
	{
		
		BSTR bstr;
		bstr = SysAllocStringLen(pCandidateString , (UINT) candidateLen);
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
					_pUIPresenter->ShowNotifyText(&reverseConvNotify.Set(pwch, wcslen(pwch)), -1);
				}
			}
			
		}
	}
	//-----------------do  array spcial code notify. We should not show notify in UI-less mode, thus cancel forceSP mode in UILess Mode
	BOOL ArraySPFound = FALSE;            
	if(Global::imeMode == IME_MODE_ARRAY && !_IsUILessMode()  && !arrayUsingSPCode && (CConfig::GetArrayForceSP() || CConfig::GetArrayNotifySP()))
	{
		CStringRange specialCode;
		CStringRange notifyText;
		ArraySPFound = _pCompositionProcessorEngine->GetArraySpeicalCodeFromConvertedText(&commitString, &specialCode); 
		if(specialCode.Get())
			_pUIPresenter->ShowNotifyText(&specialCode, -1);
	}
	convertedString = commitString;
	//----------------- do TC to SC covert if required 
	if(CConfig::GetDoHanConvert())
	{
		_pCompositionProcessorEngine->GetSCFromTC(&commitString, &convertedString);
	}
	//----------------- commit the selected string   
	if(Global::imeMode == IME_MODE_ARRAY && !_IsUILessMode()  && !arrayUsingSPCode && CConfig::GetArrayForceSP() &&  ArraySPFound )
	{
		_pCompositionProcessorEngine->DoBeep();
		_HandleCancel(ec,pContext);
		return hr;
	}
	else
	{
		hr = _AddComposingAndChar(ec, pContext, &convertedString);
		if (FAILED(hr))	return hr;
		// Do not send _endcandidatelist (or handleComplete) here to avoid cand dissapear in win8 metro
		_HandleComplete(ec,pContext);
	}
	//-----------------do accociated phrase (make phrase)
	if (CConfig::GetMakePhrase())
	{
		_pCompositionProcessorEngine->GetCandidateStringInConverted(candidateString, &candidatePhraseList);


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
			_pUIPresenter->_SetCandidateText(&candidatePhraseList, TRUE, _pCompositionProcessorEngine->GetCandidateWindowWidth());
			_pUIPresenter->_SetCandidateSelection(-1, FALSE); // set selected index to -1 if showing phrase candidates
			_phraseCandShowing = TRUE;  //_phraseCandShowing = TRUE. phrase cand is showing
			debugPrint(L"CTSFTTS::_HandleCandidateWorker() phrase cand is showing");
			_candidateMode = CANDIDATE_PHRASE;
			_isCandidateWithWildcard = FALSE;	
			
		}
		else
		{   //cancel the composition if the phrase lookup return 0 results
			_HandleCancel(ec,pContext);
		}


	}
	else
		_DeleteCandidateList(TRUE, pContext); // endCanddiateUI if not doing associated phrase.
	
	
Exit:
    return hr;
}

//+---------------------------------------------------------------------------
//
// _HandleCandidateArrowKey
//
//----------------------------------------------------------------------------

HRESULT CTSFTTS::_HandleCandidateArrowKey(TfEditCookie ec, _In_ ITfContext *pContext, _In_ KEYSTROKE_FUNCTION keyFunction)
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

HRESULT CTSFTTS::_HandleCandidateSelectByNumber(TfEditCookie ec, _In_ ITfContext *pContext, _In_ UINT uCode)
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

HRESULT CTSFTTS::_HandlePhraseFinalize(TfEditCookie ec, _In_ ITfContext *pContext)
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

HRESULT CTSFTTS::_HandlePhraseArrowKey(TfEditCookie ec, _In_ ITfContext *pContext, _In_ KEYSTROKE_FUNCTION keyFunction)
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

HRESULT CTSFTTS::_HandlePhraseSelectByNumber(TfEditCookie ec, _In_ ITfContext *pContext, _In_ UINT uCode)
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

HRESULT CTSFTTS::_CreateAndStartCandidate(_In_ CCompositionProcessorEngine *pCompositionProcessorEngine, TfEditCookie ec, _In_ ITfContext *pContext)
{
	debugPrint(L"CTSFTTS::_CreateAndStartCandidate(), _candidateMode = %d", _candidateMode);
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

VOID CTSFTTS::_DeleteCandidateList(BOOL isForce, _In_opt_ ITfContext *pContext)
{
	//isForce;
	pContext;
	debugPrint(L"CTSFTTS::_DeleteCandidateList()\n");
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
