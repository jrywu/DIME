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
#include "CompositionProcessorEngine.h"
#include "PhoneticSymbols.h"
#include "Globals.h"
#include "Compartment.h"
#include "LanguageBar.h"

#define MAX_READINGSTRING 64

//////////////////////////////////////////////////////////////////////
//
// CompositionProcessorEngine implementation.
//
//////////////////////////////////////////////////////////////////////+---------------------------------------------------------------------------
//
// ctor
//
//----------------------------------------------------------------------------

CCompositionProcessorEngine::CCompositionProcessorEngine(_In_ CDIME *pTextService)
{
	debugPrint(L"CCompositionProcessorEngine::CCompositionProcessorEngine() constructor");
	_pTextService = pTextService;
	if (_pTextService)
		_pTextService->AddRef();

	_imeMode = IME_MODE::IME_MODE_NONE;


	for (UINT i = 0; i < IM_SLOTS; i++)
	{
		_pTableDictionaryEngine[i] = nullptr;
		_pTableDictionaryFile[i] = nullptr;
		_pCustomTableDictionaryEngine[i] = nullptr;
		_pCustomTableDictionaryFile[i] = nullptr;
	}

	//phoneticKeyboardLayout = PHONETIC_STANDARD_KEYBOARD_LAYOUT;
	phoneticSyllable = 0;

	//Array
	_pArrayShortCodeTableDictionaryEngine = nullptr;
	_pArrayShortCodeDictionaryFile = nullptr;

	_pArraySpecialCodeTableDictionaryEngine = nullptr;
	_pArraySpecialCodeDictionaryFile = nullptr;

	_pArrayExtBTableDictionaryEngine = nullptr;
	_pArrayExtBDictionaryFile = nullptr;

	_pArrayExtCDTableDictionaryEngine = nullptr;
	_pArrayExtCDDictionaryFile = nullptr;

	_pArrayExtETableDictionaryEngine = nullptr;
	_pArrayExtEDictionaryFile = nullptr;

	_pArrayPhraseTableDictionaryEngine = nullptr;
	_pArrayPhraseDictionaryFile = nullptr;

	//Phrase
	_pPhraseTableDictionaryEngine = nullptr;
	_pPhraseDictionaryFile = nullptr;

	//TCSC
	_pTCSCTableDictionaryEngine = nullptr;
	_pTCSCTableDictionaryFile = nullptr;
	_pTCFreqTableDictionaryEngine = nullptr;
	_pTCFreqTableDictionaryFile = nullptr;

	_pEndkey = nullptr;

	_tfClientId = TF_CLIENTID_NULL;

	_pActiveCandidateListIndexRange = &_candidateListIndexRange;

	_hasWildcardIncludedInKeystrokeBuffer = FALSE;

	_isKeystrokeSort = FALSE;

	_isWildCardWordFreqSort = TRUE;



	_candidateWndWidth = DEFAULT_CAND_ITEM_LENGTH + TRAILING_SPACE;  //default with =  3 charaters +  trailling space

	_candidateListPhraseModifier = 0;

	_candidatePageSize = 0;




}

//+---------------------------------------------------------------------------
//
// dtor
//
//----------------------------------------------------------------------------

CCompositionProcessorEngine::~CCompositionProcessorEngine()
{
	debugPrint(L"CCompositionProcessorEngine::~CCompositionProcessorEngine() destructor");
	if (_pTextService) _pTextService->Release();
	ReleaseDictionaryFiles();
}

//+---------------------------------------------------------------------------
//
// GetReadingString
// Retrieves string from Composition Processor Engine.
// param
//     [out] pReadingString - Specified returns pointer of CUnicodeString.
// returns
//     none
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::GetReadingString(_Inout_ CStringRange *pReadingString, _Inout_opt_ BOOL *pIsWildcardIncluded, _In_opt_ CStringRange *pKeyCode)
{
	debugPrint(L"CCompositionProcessorEngine::GetReadingStrings() ");
	if (!IsDictionaryAvailable(Global::imeMode))
	{
		debugPrint(L"CCompositionProcessorEngine::GetReadingStrings() dictionary is not available.");
		return;
	}

	CStringRange* pKeyStrokeBuffer = nullptr;
	pKeyStrokeBuffer = (pKeyCode == nullptr) ? &_keystrokeBuffer : pKeyCode;
	*pReadingString = *pKeyStrokeBuffer;

	CStringRange oneKeystroke;

	_hasWildcardIncludedInKeystrokeBuffer = FALSE;

	if (pKeyStrokeBuffer->GetLength())
	{

		PWCHAR pwchRadical;
		pwchRadical = new (std::nothrow) WCHAR[MAX_READINGSTRING];
		if (pwchRadical == NULL) return; // should not happen
		*pwchRadical = L'\0';

		if (_pTableDictionaryEngine[(UINT)Global::imeMode]->GetRadicalMap() &&
			_pTableDictionaryEngine[(UINT)Global::imeMode]->GetRadicalMap()->size() && !IsSymbol())
		{

			for (DWORD index = 0; index < pKeyStrokeBuffer->GetLength(); index++)
			{
				if (_pTableDictionaryEngine[(UINT)Global::imeMode]->GetRadicalMap() &&
					_pTableDictionaryEngine[(UINT)Global::imeMode]->GetRadicalMap()->size() && !IsSymbol()) // if radicalMap is valid (size()>0), then convert the keystroke buffer 
				{
					_T_RadicalMap::iterator item =
						_pTableDictionaryEngine[(UINT)Global::imeMode]->GetRadicalMap()->find(towupper(*(pKeyStrokeBuffer->Get() + index)));
					if (_pTableDictionaryEngine[(UINT)Global::imeMode]->GetRadicalMap() &&
						item != _pTableDictionaryEngine[(UINT)Global::imeMode]->GetRadicalMap()->end())
					{
						// Replace assert with runtime bounds checking
						size_t currentLen = wcslen(pwchRadical);
						size_t appendLen = wcslen(item->second);
						if (currentLen + appendLen >= MAX_READINGSTRING - 1)
						{
							// Truncate to fit within buffer
							size_t maxAppend = MAX_READINGSTRING - 1 - currentLen;
							StringCchCatN(pwchRadical, MAX_READINGSTRING, item->second, maxAppend);
						}
						else
						{
							StringCchCat(pwchRadical, MAX_READINGSTRING, item->second);
						}
					}
					else
					{
						// Replace assert with runtime bounds checking
						size_t currentLen = wcslen(pwchRadical);
						if (currentLen + 1 >= MAX_READINGSTRING - 1)
						{
							// Buffer full, skip appending
							debugPrint(L"Warning: Reading string buffer full, truncating input");
						}
						else
						{
							StringCchCatN(pwchRadical, MAX_READINGSTRING, (pKeyStrokeBuffer->Get() + index), 1);
						}
					}
				}

				oneKeystroke.Set(pKeyStrokeBuffer->Get() + index, 1);
				
				if (IsWildcardChar(*oneKeystroke.Get()))
					_hasWildcardIncludedInKeystrokeBuffer = TRUE;
				
			}
			pReadingString->Set(pwchRadical, wcslen(pwchRadical));
		}
		else if (pKeyStrokeBuffer->GetLength())
		{
			// Replace assert with runtime bounds checking
			size_t currentLen = wcslen(pwchRadical);
			size_t keyStrokeLen = pKeyStrokeBuffer->GetLength();
			if (currentLen + keyStrokeLen >= MAX_READINGSTRING - 1)
			{
				// Truncate to fit within buffer
				size_t maxAppend = MAX_READINGSTRING - 1 - currentLen;
				StringCchCatN(pwchRadical, MAX_READINGSTRING, (pKeyStrokeBuffer->Get()), maxAppend);
				debugPrint(L"Warning: Reading string buffer full, truncating keystroke input");
			}
			else
			{
				StringCchCatN(pwchRadical, MAX_READINGSTRING, (pKeyStrokeBuffer->Get()), keyStrokeLen);
			}
			pReadingString->Set(pwchRadical, wcslen(pwchRadical));
		}

	}
	if (pIsWildcardIncluded)
		*pIsWildcardIncluded = _hasWildcardIncludedInKeystrokeBuffer;
}

//+---------------------------------------------------------------------------
//
// GetCandidateList
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::GetCandidateList(_Inout_ CDIMEArray<CCandidateListItem> *pCandidateList, BOOL isIncrementalWordSearch, BOOL isWildcardSearch, BOOL isArrayPhraseEnding)
{
	debugPrint(L"CCompositionProcessorEngine::GetCandidateList(), isIncrementalWordSearch = %d, isWildcardSearch = %d, isArrayPhraseEnding =%d ", isIncrementalWordSearch, isWildcardSearch, isArrayPhraseEnding);
	if (!IsDictionaryAvailable(Global::imeMode) || pCandidateList == nullptr || _keystrokeBuffer.Get() == nullptr)
	{
		return;
	}
	//Check if only *, ? in _keystrokebuffer
	UINT virtualKeyLen = (UINT)GetVirtualKeyLength();

	if (virtualKeyLen > 0)
	{
		if (virtualKeyLen == 1 && IsWildcardChar(*_keystrokeBuffer.Get()))
		{
			CCandidateListItem* pLI = nullptr;
			CStringRange readingString;
			GetReadingString(&readingString, FALSE);

			pLI = pCandidateList->Append();
			pLI->_FindKeyCode = _keystrokeBuffer;
			pLI->_ItemString = readingString;
			_hasWildcardIncludedInKeystrokeBuffer = FALSE;
			return;
		}

		//Check if all wildcard
		BOOL allWildCard = TRUE;
		for (UINT i = 0; i < virtualKeyLen; i++)
		{
			allWildCard = IsWildcardChar(*(_keystrokeBuffer.Get() + i));
			if (!allWildCard) break;
		}
		if (allWildCard)
		{
			_hasWildcardIncludedInKeystrokeBuffer = FALSE;
			return;
		}


	}

	_pActiveCandidateListIndexRange = &_candidateListIndexRange;  // Reset the active cand list range

	// check keystroke buffer already has wildcard char which end user want wildcard search
	DWORD wildcardIndex = 0;
	BOOL isFindWildcard = FALSE;

	for (wildcardIndex = 0; wildcardIndex < _keystrokeBuffer.GetLength(); wildcardIndex++)
	{
		if (IsWildcardChar(*(_keystrokeBuffer.Get() + wildcardIndex)))
		{
			isFindWildcard = TRUE;
			break;
		}
	}
	

	if (isIncrementalWordSearch && IsArrayShortCode() && !isFindWildcard) //array short code mode
	{
		if (_pArrayShortCodeTableDictionaryEngine == nullptr)
		{
			_pTableDictionaryEngine[(UINT)Global::imeMode]->SetSearchSection(SEARCH_SECTION::SEARCH_SECTION_PRHASE_FROM_KEYSTROKE);
			_pTableDictionaryEngine[(UINT)Global::imeMode]->CollectWord(&_keystrokeBuffer, pCandidateList);
		}
		else
		{
			_pArrayShortCodeTableDictionaryEngine->CollectWord(&_keystrokeBuffer, pCandidateList);
		}
	}
	else if (isIncrementalWordSearch && 
		!(Global::imeMode == IME_MODE::IME_MODE_ARRAY && CConfig::GetArrayScope()!= ARRAY_SCOPE::ARRAY40_BIG5))
	{
		CStringRange wildcardSearch;
		DWORD_PTR keystrokeBufLen = _keystrokeBuffer.GetLength() + 3;


		PWCHAR pwch = new (std::nothrow) WCHAR[keystrokeBufLen];
		if (!pwch)  return;

		StringCchCopyN(pwch, keystrokeBufLen, _keystrokeBuffer.Get(), _keystrokeBuffer.GetLength());
		if (!isFindWildcard)
		{
			StringCchCat(pwch, keystrokeBufLen, L"*");
		}

		size_t len = 0;
		if (StringCchLength(pwch, STRSAFE_MAX_CCH, &len) == S_OK)
		{
			wildcardSearch.Set(pwch, len);
		}
		else
		{
			return;
		}
		BOOL customTablePriority = CConfig::getCustomTablePriority();
		//search custom table with priority
		if (_pCustomTableDictionaryEngine[(UINT)Global::imeMode] && customTablePriority)
			_pCustomTableDictionaryEngine[(UINT)Global::imeMode]->CollectWordForWildcard(&wildcardSearch, pCandidateList);
		_pTableDictionaryEngine[(UINT)Global::imeMode]->SetSearchSection(SEARCH_SECTION::SEARCH_SECTION_TEXT);
		_pTableDictionaryEngine[(UINT)Global::imeMode]->CollectWordForWildcard(&wildcardSearch, pCandidateList);
		//search custom table without priority
		if (_pCustomTableDictionaryEngine[(UINT)Global::imeMode] && !customTablePriority)
			_pCustomTableDictionaryEngine[(UINT)Global::imeMode]->CollectWordForWildcard(&wildcardSearch, pCandidateList);


		if (pCandidateList && 0 >= pCandidateList->Count())
		{
			return;
		}

		if (IsKeystrokeSort())
		{
			_pTableDictionaryEngine[(UINT)Global::imeMode]->SortListItemByFindKeyCode(pCandidateList);
		}

		// Incremental search would show keystroke data from all candidate list items
		// but wont show identical keystroke data for user inputted.
		for (UINT index = 0; index < pCandidateList->Count(); index++)
		{
			CCandidateListItem *pLI = pCandidateList->GetAt(index);
			DWORD_PTR keystrokeBufferLen = 0;

			keystrokeBufferLen = _keystrokeBuffer.GetLength();
			

			if (pLI)
			{
				CStringRange newFindKeyCode;
				newFindKeyCode.Set(pLI->_FindKeyCode.Get() + keystrokeBufferLen, pLI->_FindKeyCode.GetLength() - keystrokeBufferLen);
				pLI->_FindKeyCode.Set(newFindKeyCode);
			}
		}

		delete[] pwch;
	}
	else if (IsSymbol() && (Global::imeMode == IME_MODE::IME_MODE_DAYI || Global::imeMode == IME_MODE::IME_MODE_ARRAY))
	{
		if (_pTableDictionaryEngine[(UINT)Global::imeMode]->GetDictionaryType() == DICTIONARY_TYPE::TTS_DICTIONARY)
			_pTableDictionaryEngine[(UINT)Global::imeMode]->SetSearchSection(SEARCH_SECTION::SEARCH_SECTION_SYMBOL);
		_pTableDictionaryEngine[(UINT)Global::imeMode]->CollectWord(&_keystrokeBuffer, pCandidateList);
	}
	else if (isWildcardSearch)
	{

		//Prepare for sorting candidate list with TC frequency table
		if (_isWildCardWordFreqSort && _pTCFreqTableDictionaryEngine == nullptr) SetupTCFreqTable();

		//Phonetic anytone
		BOOL phoneticAnyTone = FALSE;
		CStringRange noToneKeyStroke;
		if (Global::imeMode == IME_MODE::IME_MODE_PHONETIC && ((phoneticSyllable&vpToneMask) == vpAnyTone))
		{
			phoneticAnyTone = TRUE;
			noToneKeyStroke = buildKeyStrokesFromPhoneticSyllable(phoneticSyllable&(~vpToneMask));
		}

		// search custom table with priority
		BOOL customPhrasePriority = CConfig::getCustomTablePriority();
		if (_pCustomTableDictionaryEngine[(UINT)Global::imeMode] && customPhrasePriority)
		{
			if (phoneticAnyTone)
				_pCustomTableDictionaryEngine[(UINT)Global::imeMode]->CollectWordForWildcard(&noToneKeyStroke, pCandidateList);
			if (Global::imeMode != IME_MODE::IME_MODE_ARRAY || ( Global::imeMode == IME_MODE::IME_MODE_ARRAY && (CConfig::GetArraySingleQuoteCustomPhrase() == isArrayPhraseEnding) ) )
				_pCustomTableDictionaryEngine[(UINT)Global::imeMode]->CollectWordForWildcard(&_keystrokeBuffer, pCandidateList);
		}

		//Search IM table
		if (_pTableDictionaryEngine[(UINT)Global::imeMode] && phoneticAnyTone)
			_pTableDictionaryEngine[(UINT)Global::imeMode]->CollectWordForWildcard(&noToneKeyStroke, pCandidateList, _pTCFreqTableDictionaryEngine);

		//Search Main table
		_pTableDictionaryEngine[(UINT)Global::imeMode]->SetSearchSection(SEARCH_SECTION::SEARCH_SECTION_TEXT);
		if (Global::imeMode != IME_MODE::IME_MODE_ARRAY || (Global::imeMode == IME_MODE::IME_MODE_ARRAY && !isArrayPhraseEnding))
			_pTableDictionaryEngine[(UINT)Global::imeMode]->CollectWordForWildcard(&_keystrokeBuffer, pCandidateList, _pTCFreqTableDictionaryEngine);
		else if (Global::imeMode == IME_MODE::IME_MODE_ARRAY && CConfig::GetArrayScope() != ARRAY_SCOPE::ARRAY40_BIG5
				&& _pArrayPhraseTableDictionaryEngine && isArrayPhraseEnding)
			_pArrayPhraseTableDictionaryEngine->CollectWordForWildcard(&_keystrokeBuffer, pCandidateList);

		//Sort candidate list with TC frequency table
		if (_pTableDictionaryEngine[(UINT)Global::imeMode] &&
			_isWildCardWordFreqSort && _pTCFreqTableDictionaryEngine && !isArrayPhraseEnding)
			_pTableDictionaryEngine[(UINT)Global::imeMode]->SortListItemByWordFrequency(pCandidateList);
		


		//Search cutom table without priority
		if (_pCustomTableDictionaryEngine[(UINT)Global::imeMode] && !customPhrasePriority)
		{
			if (phoneticAnyTone)
				_pCustomTableDictionaryEngine[(UINT)Global::imeMode]->CollectWordForWildcard(&noToneKeyStroke, pCandidateList);
			else if (Global::imeMode != IME_MODE::IME_MODE_ARRAY || (Global::imeMode == IME_MODE::IME_MODE_ARRAY && CConfig::GetArrayScope() != ARRAY_SCOPE::ARRAY40_BIG5
				&& (CConfig::GetArraySingleQuoteCustomPhrase() == isArrayPhraseEnding)))
				_pCustomTableDictionaryEngine[(UINT)Global::imeMode]->CollectWordForWildcard(&_keystrokeBuffer, pCandidateList);
		}

		//Search Array unicode extensions
		if (Global::imeMode == IME_MODE::IME_MODE_ARRAY && !isArrayPhraseEnding
			&& CConfig::GetArrayScope() != ARRAY_SCOPE::ARRAY40_BIG5
			&& CConfig::GetArrayScope() != ARRAY_SCOPE::ARRAY30_UNICODE_EXT_A )
		{
			switch (CConfig::GetArrayScope())
			{

			case ARRAY_SCOPE::ARRAY30_UNICODE_EXT_AB:
				if (_pArrayExtBTableDictionaryEngine)	_pArrayExtBTableDictionaryEngine->CollectWordForWildcard(&_keystrokeBuffer, pCandidateList);
				break;
			case ARRAY_SCOPE::ARRAY30_UNICODE_EXT_ABCD:
				if (_pArrayExtBTableDictionaryEngine)	_pArrayExtBTableDictionaryEngine->CollectWordForWildcard(&_keystrokeBuffer, pCandidateList);
				if (_pArrayExtCDTableDictionaryEngine)	_pArrayExtCDTableDictionaryEngine->CollectWordForWildcard(&_keystrokeBuffer, pCandidateList);
				break;
			case ARRAY_SCOPE::ARRAY30_UNICODE_EXT_ABCDE:
				if (_pArrayExtBTableDictionaryEngine)	_pArrayExtBTableDictionaryEngine->CollectWordForWildcard(&_keystrokeBuffer, pCandidateList);
				if (_pArrayExtCDTableDictionaryEngine)	_pArrayExtCDTableDictionaryEngine->CollectWordForWildcard(&_keystrokeBuffer, pCandidateList);
				if (_pArrayExtETableDictionaryEngine)	_pArrayExtETableDictionaryEngine->CollectWordForWildcard(&_keystrokeBuffer, pCandidateList);
				break;
			default:
				break;
			}

		}


	}
	else
	{
		
		// search custom table with priority
		BOOL customPhrasePriority = CConfig::getCustomTablePriority();
		if (_pCustomTableDictionaryEngine[(UINT)Global::imeMode] && customPhrasePriority \
			&& (Global::imeMode != IME_MODE::IME_MODE_ARRAY || 
				(Global::imeMode == IME_MODE::IME_MODE_ARRAY && CConfig::GetArrayScope() != ARRAY_SCOPE::ARRAY40_BIG5 &&
					(CConfig::GetArraySingleQuoteCustomPhrase() == isArrayPhraseEnding))) )
			_pCustomTableDictionaryEngine[(UINT)Global::imeMode]->CollectWord(&_keystrokeBuffer, pCandidateList);

		_pTableDictionaryEngine[(UINT)Global::imeMode]->SetSearchSection(SEARCH_SECTION::SEARCH_SECTION_TEXT);
		
		if (_pTableDictionaryEngine[(UINT)Global::imeMode] &&
			(Global::imeMode != IME_MODE::IME_MODE_ARRAY || (Global::imeMode == IME_MODE::IME_MODE_ARRAY && !isArrayPhraseEnding)) )
			_pTableDictionaryEngine[(UINT)Global::imeMode]->CollectWord(&_keystrokeBuffer, pCandidateList);
		else if (Global::imeMode == IME_MODE::IME_MODE_ARRAY && CConfig::GetArrayScope() != ARRAY_SCOPE::ARRAY40_BIG5 &&
					_pArrayPhraseTableDictionaryEngine && isArrayPhraseEnding)  // Array Phrase 
			_pArrayPhraseTableDictionaryEngine->CollectWord(&_keystrokeBuffer, pCandidateList);

		// search custom table without priority
		debugPrint(L"CCompositionProcessorEngine::GetCandidateList(), CConfig::GetArraySingleQuoteCustomPhrase() = %d, isArrayPhraseEnding = %d ", 
			CConfig::GetArraySingleQuoteCustomPhrase(), isArrayPhraseEnding);
		if (_pCustomTableDictionaryEngine[(UINT)Global::imeMode] && !customPhrasePriority \
			&& (Global::imeMode != IME_MODE::IME_MODE_ARRAY || (Global::imeMode == IME_MODE::IME_MODE_ARRAY 
				&& (CConfig::GetArraySingleQuoteCustomPhrase() == isArrayPhraseEnding))))
			_pCustomTableDictionaryEngine[(UINT)Global::imeMode]->CollectWord(&_keystrokeBuffer, pCandidateList);


		//Search Array unicode extensions
		if (Global::imeMode == IME_MODE::IME_MODE_ARRAY && !isArrayPhraseEnding
			&& CConfig::GetArrayScope() != ARRAY_SCOPE::ARRAY40_BIG5
			&& CConfig::GetArrayScope() != ARRAY_SCOPE::ARRAY30_UNICODE_EXT_A)
		{
			switch (CConfig::GetArrayScope())
			{

			case ARRAY_SCOPE::ARRAY30_UNICODE_EXT_AB:
				if (_pArrayExtBTableDictionaryEngine)	_pArrayExtBTableDictionaryEngine->CollectWord(&_keystrokeBuffer, pCandidateList);
				break;
			case ARRAY_SCOPE::ARRAY30_UNICODE_EXT_ABCD:
				if (_pArrayExtBTableDictionaryEngine)	_pArrayExtBTableDictionaryEngine->CollectWord(&_keystrokeBuffer, pCandidateList);
				if (_pArrayExtCDTableDictionaryEngine)	_pArrayExtCDTableDictionaryEngine->CollectWord(&_keystrokeBuffer, pCandidateList);
				break;
			case ARRAY_SCOPE::ARRAY30_UNICODE_EXT_ABCDE:
				if (_pArrayExtBTableDictionaryEngine)	_pArrayExtBTableDictionaryEngine->CollectWord(&_keystrokeBuffer, pCandidateList);
				if (_pArrayExtCDTableDictionaryEngine)	_pArrayExtCDTableDictionaryEngine->CollectWord(&_keystrokeBuffer, pCandidateList);
				if (_pArrayExtETableDictionaryEngine)	_pArrayExtETableDictionaryEngine->CollectWord(&_keystrokeBuffer, pCandidateList);
				break;
			default:
				break;
			}
		}

	}


	_candidateWndWidth = DEFAULT_CAND_ITEM_LENGTH + TRAILING_SPACE;
	for (UINT index = 0; index < pCandidateList->Count();)
	{
		CCandidateListItem *pLI = pCandidateList->GetAt(index);

		if (pLI)
		{
			UINT len = (UINT)pLI->_ItemString.GetLength();
			if (len > MAX_CAND_ITEM_LENGTH) len = MAX_CAND_ITEM_LENGTH;
			if (len > _candidateWndWidth - TRAILING_SPACE)
			{
				_candidateWndWidth = len + TRAILING_SPACE;
			}
			index++;
		}
	}

	if(!isIncrementalWordSearch)
		_hasWildcardIncludedInKeystrokeBuffer = FALSE; //remove has wildcard flag anyway
}

//+---------------------------------------------------------------------------
//
// GetCandidateStringInConverted
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::GetCandidateStringInConverted(CStringRange &searchString, _In_ CDIMEArray<CCandidateListItem> *pCandidateList)
{
	debugPrint(L"CCompositionProcessorEngine::GetCandidateStringInConverted() ");

	if (!IsDictionaryAvailable(Global::imeMode) || pCandidateList == nullptr)
	{
		return;
	}

	_pActiveCandidateListIndexRange = &_phraseCandidateListIndexRange;  // Reset the active cand list range

	// Search phrase from SECTION_TEXT's converted string list
	CStringRange searchText;
	DWORD_PTR srgKeystrokeBufLen = searchString.GetLength() + 2;
	PWCHAR pwch = new (std::nothrow) WCHAR[srgKeystrokeBufLen];
	if (!pwch)
	{
		return;
	}

	StringCchCopyN(pwch, srgKeystrokeBufLen, searchString.Get(), searchString.GetLength());
	if (!(Global::hasPhraseSection || Global::hasCINPhraseSection)) StringCchCat(pwch, srgKeystrokeBufLen, L"*");  // do wild search if no TTS [Phrase]section present

	// add wildcard char
	size_t len = 0;
	if (StringCchLength(pwch, STRSAFE_MAX_CCH, &len) != S_OK)
	{
		return;
	}
	searchText.Set(pwch, len);

	if (_pPhraseTableDictionaryEngine) // do phrase lookup if CIN file has phrase section
	{
		if (_pPhraseTableDictionaryEngine->GetDictionaryType() == DICTIONARY_TYPE::TTS_DICTIONARY)
		{
			_pPhraseTableDictionaryEngine->SetSearchSection(SEARCH_SECTION::SEARCH_SECTION_PHRASE);
		}
		_pPhraseTableDictionaryEngine->CollectWord(&searchText, pCandidateList);
	}
	//else // no phrase section, do wildcard text search
	//	_pPhraseTableDictionaryEngine->CollectWordFromConvertedStringForWildcard(&searchText, pCandidateList);

	if (IsKeystrokeSort())
		_pTableDictionaryEngine[(UINT)Global::imeMode]->SortListItemByFindKeyCode(pCandidateList);

	_candidateWndWidth = DEFAULT_CAND_ITEM_LENGTH + TRAILING_SPACE;
	for (UINT index = 0; index < pCandidateList->Count();)
	{
		CCandidateListItem *pLI = pCandidateList->GetAt(index);

		if (pLI && pLI->_ItemString.GetLength() > _candidateWndWidth - TRAILING_SPACE)
		{
			_candidateWndWidth = (UINT)pLI->_ItemString.GetLength() + TRAILING_SPACE;
		}

		index++;
	}

	searchText.Clear();
	delete[] pwch;
}


//+---------------------------------------------------------------------------
//
// GetDayiAddressChar
//
//----------------------------------------------------------------------------

WCHAR CCompositionProcessorEngine::GetDayiAddressChar(WCHAR wch)
{
	for (int i = 0; i < ARRAYSIZE(Global::dayiAddressCharTable); i++)
	{
		if (Global::dayiAddressCharTable[i]._Code == wch)
		{
			if (CConfig::getDayiArticleMode())  //article mode: input full-shape symbols with address keys
				return Global::dayiArticleCharTable[i]._AddressChar;
			else
				return Global::dayiAddressCharTable[i]._AddressChar;
		}
	}
	return 0;
}


//+---------------------------------------------------------------------------
//
// checkArraySpeicalCode
//
//----------------------------------------------------------------------------
DWORD_PTR CCompositionProcessorEngine::CollectWordFromArraySpeicalCode(_Inout_opt_ const WCHAR **ppwchSpecialCodeResultString)
{
	if (!IsDictionaryAvailable(Global::imeMode))
	{
		return 0;
	}
	//if(ppwchSpecialCodeResultString)
	//	*ppwchSpecialCodeResultString = nullptr;

	if (Global::imeMode != IME_MODE::IME_MODE_ARRAY || _keystrokeBuffer.GetLength() != 2) return 0;

	CDIMEArray<CCandidateListItem> candidateList;

	if (_pArraySpecialCodeTableDictionaryEngine == nullptr)
	{
		_pTableDictionaryEngine[(UINT)Global::imeMode]->SetSearchSection(SEARCH_SECTION::SEARCH_SECTION_TEXT);
		_pTableDictionaryEngine[(UINT)Global::imeMode]->CollectWord(&_keystrokeBuffer, &candidateList);
	}
	else
	{
		_pArraySpecialCodeTableDictionaryEngine->CollectWord(&_keystrokeBuffer, &candidateList);
	}

	//issue #18 check if the special code corresponding to the same coverted text
	if (candidateList.Count() == 1 && ppwchSpecialCodeResultString && 
		   CompareString(_pTextService->GetLocale(), NULL, *ppwchSpecialCodeResultString, 1, candidateList.GetAt(0)->_ItemString.Get(), 1 ) == CSTR_EQUAL)
	{
		//*ppwchSpecialCodeResultString = candidateList.GetAt(0)->_ItemString.Get();
		return  candidateList.GetAt(0)->_ItemString.GetLength();
	}
	else
		return 0;



}
//+---------------------------------------------------------------------------
//
// checkArraySpeicalCode
//
//----------------------------------------------------------------------------
BOOL CCompositionProcessorEngine::GetArraySpeicalCodeFromConvertedText(_In_ CStringRange *inword, _Inout_opt_ CStringRange *csrReslt)
{
	if (!IsDictionaryAvailable(Global::imeMode))
	{
		csrReslt = inword;
		return FALSE;
	}

	if (Global::imeMode != IME_MODE::IME_MODE_ARRAY || _pArraySpecialCodeTableDictionaryEngine == nullptr || inword == nullptr) return FALSE;

	CDIMEArray<CCandidateListItem> candidateList;
	_pArraySpecialCodeTableDictionaryEngine->CollectWordFromConvertedString(inword, &candidateList);
	if (candidateList.Count() == 1)
	{

		PWCHAR pwch;
		pwch = new (std::nothrow) WCHAR[MAX_READINGSTRING];
		if (pwch == NULL)
		{
			csrReslt = inword;
			return FALSE;
		}
		*pwch = L'\0';
		_T_RadicalMap* pRadicalMap = _pTableDictionaryEngine[(UINT)Global::imeMode]->GetRadicalMap();
		if (pRadicalMap && pRadicalMap->size() &&
			candidateList.GetAt(0)->_FindKeyCode.GetLength() )
		{
			for (UINT i = 0; i < candidateList.GetAt(0)->_FindKeyCode.GetLength(); i++)
			{ // query keyname from keymap
				_T_RadicalMap::iterator item =
					pRadicalMap->find(towupper(*(candidateList.GetAt(0)->_FindKeyCode.Get() + i)));
				if (item != pRadicalMap->end())
				{
					// Replace assert with runtime bounds checking
					size_t currentLen = wcslen(pwch);
					size_t appendLen = wcslen(item->second);
					if (currentLen + appendLen >= MAX_READINGSTRING - 1)
					{
						// Truncate to fit within buffer
						size_t maxAppend = MAX_READINGSTRING - 1 - currentLen;
						StringCchCatN(pwch, MAX_READINGSTRING, item->second, maxAppend);
						debugPrint(L"Warning: Reading string buffer full, truncating radical mapping");
					}
					else
					{
						StringCchCat(pwch, MAX_READINGSTRING, item->second);
					}
				}
			}
			csrReslt->Set(pwch, wcslen(pwch));
			return TRUE;
		}
		else
		{
			delete[] pwch;
			csrReslt = inword;

		}
	}
		

	return FALSE;


}

IME_MODE CCompositionProcessorEngine::GetImeModeFromGuidProfile(REFGUID guidLanguageProfile)
{
	debugPrint(L"CCompositionProcessorEngine::GetImeModeFromGuidProfile() \n");
	IME_MODE imeMode = IME_MODE::IME_MODE_NONE;
	if (guidLanguageProfile == Global::DIMEDayiGuidProfile)
	{
		debugPrint(L"CCompositionProcessorEngine::GetImeModeFromGuidProfile() : DAYI Mode");
		imeMode = IME_MODE::IME_MODE_DAYI;
	}
	else if (guidLanguageProfile == Global::DIMEArrayGuidProfile)
	{
		debugPrint(L"CCompositionProcessorEngine::GetImeModeFromGuidProfile() : Array Mode");
		imeMode = IME_MODE::IME_MODE_ARRAY;
	}
	else if (guidLanguageProfile == Global::DIMEPhoneticGuidProfile)
	{
		debugPrint(L"CCompositionProcessorEngine::GetImeModeFromGuidProfile() : Phonetic Mode");
		imeMode = IME_MODE::IME_MODE_PHONETIC;
	}
	else if (guidLanguageProfile == Global::DIMEGenericGuidProfile)
	{
		debugPrint(L"CCompositionProcessorEngine::GetImeModeFromGuidProfile() : Generic Mode");
		imeMode = IME_MODE::IME_MODE_GENERIC;
	}

	return imeMode;

}

HRESULT CCompositionProcessorEngine::GetReverseConversionResults(IME_MODE imeMode, _In_ LPCWSTR lpstrToConvert, _Inout_ CDIMEArray<CCandidateListItem> *pCandidateList)
{
	debugPrint(L"CCompositionProcessorEngine::GetReverseConversionResults() \n");


	if (_pTableDictionaryEngine[(UINT)imeMode] == nullptr)
		return S_FALSE;

	_pTableDictionaryEngine[(UINT)imeMode]->SetSearchSection(SEARCH_SECTION::SEARCH_SECTION_TEXT);
	CStringRange csrToCovert;
	_pTableDictionaryEngine[(UINT)imeMode]->CollectWordFromConvertedString(&csrToCovert.Set(lpstrToConvert, wcslen(lpstrToConvert)), pCandidateList);
	return S_OK;
}







CStringRange CCompositionProcessorEngine::buildKeyStrokesFromPhoneticSyllable(UINT syllable)
{
	CStringRange csr;
	PWCHAR buf = new (std::nothrow) WCHAR[5];
	if (buf)
	{
		WCHAR *b = buf;
		if (syllable & vpConsonantMask)
			*b++ = VPSymbolToStandardLayoutChar(syllable&vpConsonantMask);
		if (syllable & vpMiddleVowelMask)
			*b++ = VPSymbolToStandardLayoutChar(syllable&vpMiddleVowelMask);
		if (syllable & vpVowelMask)
			*b++ = VPSymbolToStandardLayoutChar(syllable&vpVowelMask);
		if (syllable & vpToneMask)
			*b++ = VPSymbolToStandardLayoutChar(syllable&vpToneMask);
		*b = 0;
		csr.Set(buf, wcslen(buf));
	}
	return csr;
}