/* DIME IME for Windows 7/8/10/11

BSD 3-Clause License

Copyright (c) 2022, Jeremy Wu
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
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
	debugPrint(L"CDIME::_HandleCandidateFinalize()");
	return _HandleCandidateWorker(ec, pContext);
}

//+---------------------------------------------------------------------------
//
// _HandleCandidateConvert
//
//----------------------------------------------------------------------------

HRESULT CDIME::_HandleCandidateConvert(TfEditCookie ec, _In_ ITfContext *pContext)
{
	debugPrint(L"CDIME::_HandleCandidateFinalize()");
    return _HandleCandidateWorker(ec, pContext);
	
}

//+---------------------------------------------------------------------------
//
// _HandleCandidateWorker
//
//----------------------------------------------------------------------------

HRESULT CDIME::_HandleCandidateWorker(TfEditCookie ec, _In_ ITfContext *pContext)
{

	debugPrint(L"CDIME::_HandleCandidateWorker() _IsComposing()= %d", _IsComposing());
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

	candidateLen = _pUIPresenter->_GetSelectedCandidateString(&pCandidateString);

	if (Global::imeMode == IME_MODE::IME_MODE_ARRAY && CConfig::GetArrayScope() != ARRAY_SCOPE::ARRAY40_BIG5)
		// check if the _strokebuffer is array special code
	{
		
		if(_pCompositionProcessorEngine->CollectWordFromArraySpeicalCode(&pCandidateString)) 
			arrayUsingSPCode = TRUE;
	}
	
	if (candidateLen == 0)
    {
		//if(_candidateMode == CANDIDATE_WITH_NEXT_COMPOSITION || _candidateMode == CANDIDATE_PHRASE)
		if(_candidateMode == CANDIDATE_MODE::CANDIDATE_PHRASE)
		{
			_HandleCancel(ec, pContext);
			goto Exit;
		}
		else
		{
			hr = S_FALSE;
			_HandleCancel(ec, pContext);
			DoBeep(BEEP_TYPE::BEEP_COMPOSITION_ERROR); //beep for no valid mapping found
			goto Exit;
		}
    }
	if (_pCompositionProcessorEngine->IsArrayShortCode() && candidateLen == 1 && *pCandidateString == 0x2394) // empty position in arry short code table.
	{
		hr = S_FALSE;
		if (Global::imeMode == IME_MODE::IME_MODE_PHONETIC)
			DoBeep(BEEP_TYPE::BEEP_WARNING);
		else
		{
			if (CConfig::GetClearOnBeep()) _HandleCancel(ec, pContext);
			DoBeep(BEEP_TYPE::BEEP_COMPOSITION_ERROR); //beep for no valid mapping found
		}
		goto Exit;
	}
	
	
	StringCchCopy(_commitString, 1, L"\0");
	StringCchCatN(_commitString, MAX_COMMIT_LENGTH, pCandidateString, candidateLen);
	commitString.Set(_commitString, candidateLen);
	//_commitString = commitString;
	
	PWCHAR pwch = new (std::nothrow) WCHAR[2];  // pCandidateString will be destroyed after _detelteCanddiateList was called.
	if (pwch == NULL) //should not happen.
		goto Exit;
	if(candidateLen > 1)
	{	
		StringCchCopyN(pwch, 2, pCandidateString + candidateLen -1, 1); 
	}else // cnadidateLen ==1
	{
		StringCchCopyN(pwch, 2, pCandidateString, 1); 	
	}
	lastChar.Set(pwch, 1 );
	//-----------------do reverse conversion notify. We should not show notify in UI-less mode, thus cancel reverse conversion notify in UILess Mode
	if (!_IsUILessMode())
	{
		if (_pITfReverseConversion[(UINT)Global::imeMode])
		{
			_AsyncReverseConversion(pContext); //asynchronized the reverse conversion with editsession for better perfomance
		}
		//-----------------or complete real key code when input with wildcard
		else if (_isCandidateWithWildcard)
		{
			const WCHAR* pCandidateKeyCode = nullptr;
			DWORD_PTR keyCodeLen = _pUIPresenter->_GetSelectedCandidateKeyCode(&pCandidateKeyCode);
			StringCchCopy(_commitKeyCode, 1, L"\0");
			if(pCandidateKeyCode)
				StringCchCatN(_commitKeyCode, MAX_KEY_LENGTH, pCandidateKeyCode, keyCodeLen);
			CStringRange commitKeyCode, convertedKeyCode;
			_pCompositionProcessorEngine->GetReadingString(&convertedKeyCode, NULL, &commitKeyCode.Set(_commitKeyCode, wcslen(_commitKeyCode)));
			_pUIPresenter->ShowNotifyText(&convertedKeyCode, 0, 0, NOTIFY_TYPE::NOTIFY_OTHERS);
		}

	}
	//-----------------do  array spcial code notify. We should not show notify in UI-less mode, thus cancel forceSP mode in UILess Mode---
	BOOL ArraySPFound = FALSE;            
	if(Global::imeMode == IME_MODE::IME_MODE_ARRAY && CConfig::GetArrayScope() != ARRAY_SCOPE::ARRAY40_BIG5 &&
		!_IsUILessMode()  && !arrayUsingSPCode && (CConfig::GetArrayForceSP() || CConfig::GetArrayNotifySP()))
	{
		CStringRange specialCode;
		CStringRange notifyText;
		ArraySPFound = _pCompositionProcessorEngine->GetArraySpeicalCodeFromConvertedText(&commitString, &specialCode); 
		if(specialCode.Get())
			_pUIPresenter->ShowNotifyText(&specialCode, 0, 0, NOTIFY_TYPE::NOTIFY_OTHERS);
	}
	convertedString = commitString;
	//----------------- do TC to SC covert if required----------------------------------------------------------------------------------
	if(CConfig::GetDoHanConvert())
	{
		_pCompositionProcessorEngine->GetSCFromTC(&commitString, &convertedString);
	}
	//----------------- commit the selected string  ------------------------------------------------------------------------------------ 
	if(Global::imeMode == IME_MODE::IME_MODE_ARRAY && !_IsUILessMode()  && !arrayUsingSPCode && CConfig::GetArrayForceSP() &&  ArraySPFound )
	{
		_HandleCancel(ec, pContext);
		DoBeep(BEEP_TYPE::BEEP_WARNING);
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
	// Issue #27.  Turn off associated phrase to avoid cursor gone in command shell.
	if (CConfig::GetMakePhrase() && !isCMDShell())
	{
		_pCompositionProcessorEngine->GetCandidateStringInConverted(lastChar, &candidatePhraseList);


		// We have a candidate list if candidatePhraseList.Cnt is not 0
		// If we are showing reverse conversion, use UIPresenter

		if (candidatePhraseList.Count())
		{

			_pUIPresenter->_ClearCandidateList();
			_pUIPresenter->_SetCandidateTextColor(CConfig::GetPhraseColor(), CConfig::GetItemBGColor());    // Text color is green
			_pUIPresenter->_SetCandidateSelectedTextColor(CConfig::GetSelectedColor(), CConfig::GetSelectedBGColor());    
			_pUIPresenter->_SetCandidateNumberColor(CConfig::GetNumberColor(), CConfig::GetItemBGColor());    
			_pUIPresenter->_SetCandidateFillColor(CConfig::GetItemBGColor());//(HBRUSH)(COLOR_WINDOW+1));    // Background color is window
			_pUIPresenter->_SetCandidateText(&candidatePhraseList, _pCompositionProcessorEngine->GetCandidateListIndexRange(),
				FALSE, _pCompositionProcessorEngine->GetCandidateWindowWidth());
			_pUIPresenter->_SetCandidateSelection(-1, FALSE); // set selected index to -1 if showing phrase candidates
			_candidateMode = CANDIDATE_MODE::CANDIDATE_PHRASE;
			_isCandidateWithWildcard = FALSE;	
			
			//StartCandidateList require a valid selection from a valid pComposition to determine the location to show the candidate window
			//Moved after setCandidateText or candidatePhraseList can be corrupt after probe comosition.
			CStringRange emptyComposition;
			if (!_IsComposing())
				_StartComposition(pContext); 
			_AddComposingAndChar(ec, pContext, &emptyComposition.Set(L" ",1)); 
			
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

HRESULT CDIME::_HandleCandidateArrowKey(TfEditCookie ec, _In_ ITfContext *pContext, _In_ KEYSTROKE_FUNCTION keyFunction)
{
	debugPrint(L"CDIME::_HandleCandidateArrowKey()");
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

HRESULT CDIME::_HandleCandidateSelectByNumber(TfEditCookie ec, _In_ ITfContext *pContext, _In_ UINT uCode,_In_ WCHAR wch)
{
	debugPrint(L"CDIME::_HandleCandidateSelectByNumber() \n");

	int iSelectAsNumber = _pCompositionProcessorEngine->GetCandidateListIndexRange()->GetIndex(uCode, wch, _candidateMode);
	debugPrint(L"CDIME::_HandleCandidateSelectByNumber() iSelectAsNumber = %d", iSelectAsNumber);

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
	debugPrint(L"CDIME::_HandlePhraseFinalize() ");
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
	debugPrint(L"CDIME::_HandlePhraseArrowKey() ");
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

HRESULT CDIME::_HandlePhraseSelectByNumber(TfEditCookie ec, _In_ ITfContext *pContext, _In_ UINT uCode, _In_ WCHAR wch)
{
	debugPrint(L"CDIME::_HandlePhraseSelectByNumber() ");
	int iSelectAsNumber = _pCompositionProcessorEngine->GetCandidateListIndexRange()->GetIndex(uCode, wch, _candidateMode);
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

	
	if ((_candidateMode == CANDIDATE_MODE::CANDIDATE_NONE) && (_pUIPresenter))
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
	_candidateMode = CANDIDATE_MODE::CANDIDATE_NONE;
    _isCandidateWithWildcard = FALSE;
}
