//
//
// Derived from Microsoft Sample IME by Jeremy '13,7,17
//
//
//#define DEBUG_PRINT
#include <Shlobj.h>
#include <Shlwapi.h>
#include "Private.h"
#include "DIME.h"
#include "CompositionProcessorEngine.h"
#include "BaseStructure.h"
#include "DictionarySearch.h"
#include "Globals.h"
#include "Compartment.h"
#include "LanguageBar.h"
#include "sddl.h"

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

	_imeMode = IME_MODE_NONE;


	for (UINT i = 0; i < IM_SLOTS; i++)
	{
		_pTableDictionaryEngine[i] = nullptr;
		_pTableDictionaryFile[i] = nullptr;
		_pCustomTableDictionaryEngine[i] = nullptr;
		_pCustomTableDictionaryFile[i] = nullptr;
	}

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

	//Phrase
	_pPhraseTableDictionaryEngine = nullptr;
	_pPhraseDictionaryFile = nullptr;

	//TCSC
	_pTCSCTableDictionaryEngine = nullptr;
	_pTCSCTableDictionaryFile = nullptr;

	_tfClientId = TF_CLIENTID_NULL;

	_pActiveCandidateListIndexRange = &_candidateListIndexRange;

	_hasWildcardIncludedInKeystrokeBuffer = FALSE;

	_isWildcard = TRUE;
	_isDisableWildcardAtFirst = TRUE;
	_isKeystrokeSort = FALSE;



	_candidateWndWidth = DEFAULT_CAND_ITEM_LENGTH + TRAILING_SPACE;  //default with =  3 charaters +  trailling space

	_candidateListPhraseModifier = 0;



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
void CCompositionProcessorEngine::ReleaseDictionaryFiles()
{

	for (UINT i = 0; i < IM_SLOTS; i++)
	{
		if (_pTableDictionaryFile[i])
		{
			delete _pTableDictionaryFile[i];
			_pTableDictionaryFile[i] = nullptr;
		}
		if (_pCustomTableDictionaryFile[i])
		{
			delete _pCustomTableDictionaryFile[i];
			_pCustomTableDictionaryFile[i] = nullptr;
		}
		if (_pTableDictionaryEngine[i])
		{   // _pTableDictionaryEngine[i] is only a pointer to either _pTTSDictionaryFile[i] or _pCINTableDictionaryEngine. no need to delete it.
			_pTableDictionaryEngine[i] = nullptr;
		}
		if (_pCustomTableDictionaryEngine[i])
		{
			_pCustomTableDictionaryEngine[i] = nullptr;
		}

	}
	// Array short-code
	if (_pArrayShortCodeDictionaryFile)
	{
		delete _pArrayShortCodeDictionaryFile;
		_pArrayShortCodeDictionaryFile = nullptr;
	}
	if (_pArrayShortCodeTableDictionaryEngine)
	{
		delete _pArrayShortCodeTableDictionaryEngine;
		_pArrayShortCodeTableDictionaryEngine = nullptr;
	}

	// Array special-code
	if (_pArraySpecialCodeDictionaryFile)
	{
		delete _pArraySpecialCodeDictionaryFile;
		_pArraySpecialCodeDictionaryFile = nullptr;
	}
	if (_pArraySpecialCodeTableDictionaryEngine)
	{
		delete _pArraySpecialCodeTableDictionaryEngine;
		_pArraySpecialCodeTableDictionaryEngine = nullptr;
	}
	// Array ext-B
	if (_pArrayExtBDictionaryFile)
	{
		delete _pArrayExtBDictionaryFile;
		_pArrayExtBDictionaryFile = nullptr;
	}
	if (_pArrayExtBTableDictionaryEngine)
	{
		delete _pArrayExtBTableDictionaryEngine;
		_pArrayExtBTableDictionaryEngine = nullptr;
	}
	// Array ext-CD
	if (_pArrayExtCDDictionaryFile)
	{
		delete _pArrayExtCDDictionaryFile;
		_pArrayExtCDDictionaryFile = nullptr;
	}
	if (_pArrayExtETableDictionaryEngine)
	{
		delete _pArrayExtETableDictionaryEngine;
		_pArrayExtETableDictionaryEngine = nullptr;
	}



	// TC-SC dictinary
	if (_pTCSCTableDictionaryEngine)
	{
		delete _pTCSCTableDictionaryEngine;
		_pTCSCTableDictionaryEngine = nullptr;
	}
	if (_pTCSCTableDictionaryFile)
	{
		delete _pTCSCTableDictionaryFile;
		_pTCSCTableDictionaryFile = nullptr;
	}




}


//+---------------------------------------------------------------------------
//
// AddVirtualKey
// Add virtual key code to Composition Processor Engine for used to parse keystroke data.
// param
//     [in] uCode - Specify virtual key code.
// returns
//     State of Text Processor Engine.
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::AddVirtualKey(WCHAR wch)
{
	if (!wch)
	{
		return FALSE;
	}
	if ((UINT)_keystrokeBuffer.GetLength() >= CConfig::GetMaxCodes())
	{
		//_pTextService->DoBeep(); // do not eat the key if keystroke buffer length >= _maxcodes
		return FALSE;
	}
	//
	// append one keystroke in buffer.
	//
	DWORD_PTR srgKeystrokeBufLen = _keystrokeBuffer.GetLength();
	PWCHAR pwch = new (std::nothrow) WCHAR[srgKeystrokeBufLen + 1];
	if (!pwch)
	{
		return FALSE;
	}

	memcpy(pwch, _keystrokeBuffer.Get(), srgKeystrokeBufLen * sizeof(WCHAR));
	pwch[srgKeystrokeBufLen] = wch;

	if (_keystrokeBuffer.Get())
	{
		delete[] _keystrokeBuffer.Get();
	}

	_keystrokeBuffer.Set(pwch, srgKeystrokeBufLen + 1);

	return TRUE;
}

//+---------------------------------------------------------------------------
//
// RemoveVirtualKey
// Remove stored virtual key code.
// param
//     [in] dwIndex   - Specified index.
// returns
//     none.
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::RemoveVirtualKey(DWORD_PTR dwIndex)
{
	DWORD_PTR srgKeystrokeBufLen = _keystrokeBuffer.GetLength();

	if (dwIndex + 1 < srgKeystrokeBufLen)
	{
		// shift following eles left
		memmove((BYTE*)_keystrokeBuffer.Get() + (dwIndex * sizeof(WCHAR)),
			(BYTE*)_keystrokeBuffer.Get() + ((dwIndex + 1) * sizeof(WCHAR)),
			(srgKeystrokeBufLen - dwIndex - 1) * sizeof(WCHAR));
	}

	_keystrokeBuffer.Set(_keystrokeBuffer.Get(), srgKeystrokeBufLen - 1);
}

//+---------------------------------------------------------------------------
//
// PurgeVirtualKey
// Purge stored virtual key code.
// param
//     none.
// returns
//     none.
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::PurgeVirtualKey()
{
	if (_keystrokeBuffer.Get())
	{
		delete[] _keystrokeBuffer.Get();
		_keystrokeBuffer.Set(NULL, 0);
	}
}

WCHAR CCompositionProcessorEngine::GetVirtualKey(DWORD_PTR dwIndex)
{
	if (dwIndex < _keystrokeBuffer.GetLength())
	{
		return *(_keystrokeBuffer.Get() + dwIndex);
	}
	return 0;
}
//+---------------------------------------------------------------------------
//
// GetReadingStrings
// Retrieves string from Composition Processor Engine.
// param
//     [out] pReadingStrings - Specified returns pointer of CUnicodeString.
// returns
//     none
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::GetReadingStrings(_Inout_ CDIMEArray<CStringRange> *pReadingStrings, _Out_ BOOL *pIsWildcardIncluded)
{
	if (!IsDictionaryAvailable(Global::imeMode))
	{
		return;
	}

	CStringRange oneKeystroke;

	_hasWildcardIncludedInKeystrokeBuffer = FALSE;

	if (pReadingStrings && pReadingStrings->Count() == 0 && _keystrokeBuffer.GetLength())
	{
		CStringRange* pNewString = nullptr;

		pNewString = pReadingStrings->Append();
		if (pNewString)
		{
			*pNewString = _keystrokeBuffer;
		}

		PWCHAR pwchRadical;
		pwchRadical = new (std::nothrow) WCHAR[MAX_READINGSTRING];
		*pwchRadical = L'\0';

		if (_pTableDictionaryEngine[Global::imeMode]->GetRadicalMap() &&
			_pTableDictionaryEngine[Global::imeMode]->GetRadicalMap()->size() && !IsSymbol())
		{

			for (DWORD index = 0; index < _keystrokeBuffer.GetLength(); index++)
			{
				if (_pTableDictionaryEngine[Global::imeMode]->GetRadicalMap() &&
					_pTableDictionaryEngine[Global::imeMode]->GetRadicalMap()->size() && !IsSymbol()) // if radicalMap is valid (size()>0), then convert the keystroke buffer 
				{
					_T_RadicalMap::iterator item =
						_pTableDictionaryEngine[Global::imeMode]->GetRadicalMap()->find(towupper(*(_keystrokeBuffer.Get() + index)));
					if (_pTableDictionaryEngine[Global::imeMode]->GetRadicalMap() &&
						item != _pTableDictionaryEngine[Global::imeMode]->GetRadicalMap()->end())
					{
						assert(wcslen(pwchRadical) + wcslen(item->second) < MAX_READINGSTRING - 1);
						StringCchCat(pwchRadical, MAX_READINGSTRING, item->second);
					}
					else
					{
						assert(wcslen(pwchRadical) + 1 < MAX_READINGSTRING - 1);
						StringCchCatN(pwchRadical, MAX_READINGSTRING, (_keystrokeBuffer.Get() + index), 1);
					}
				}

				oneKeystroke.Set(_keystrokeBuffer.Get() + index, 1);

				if (IsWildcard() && IsWildcardChar(*oneKeystroke.Get()))
				{
					_hasWildcardIncludedInKeystrokeBuffer = TRUE;
				}
			}
			if (pNewString) pNewString->Set(pwchRadical, wcslen(pwchRadical));
		}
		else if (_keystrokeBuffer.GetLength())
		{
			assert(wcslen(pwchRadical) + _keystrokeBuffer.GetLength() < MAX_READINGSTRING - 1);
			StringCchCatN(pwchRadical, MAX_READINGSTRING, (_keystrokeBuffer.Get()), _keystrokeBuffer.GetLength());
			if (pNewString) pNewString->Set(pwchRadical, wcslen(pwchRadical));

		}
	}

	*pIsWildcardIncluded = _hasWildcardIncludedInKeystrokeBuffer;
}

//+---------------------------------------------------------------------------
//
// GetCandidateList
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::GetCandidateList(_Inout_ CDIMEArray<CCandidateListItem> *pCandidateList, BOOL isIncrementalWordSearch, BOOL isWildcardSearch)
{
	if (!IsDictionaryAvailable(Global::imeMode) || pCandidateList == nullptr)
	{
		return;
	}

	_pActiveCandidateListIndexRange = &_candidateListIndexRange;  // Reset the active cand list range

	if (isIncrementalWordSearch && IsArrayShortCode()) //array short code mode
	{
		if (_pArrayShortCodeTableDictionaryEngine == nullptr)
		{
			_pTableDictionaryEngine[Global::imeMode]->SetSearchSection(SEARCH_SECTION_PRHASE_FROM_KEYSTROKE);
			_pTableDictionaryEngine[Global::imeMode]->CollectWord(&_keystrokeBuffer, pCandidateList);
		}
		else
		{
			_pArrayShortCodeTableDictionaryEngine->CollectWord(&_keystrokeBuffer, pCandidateList);
		}
	}
	else if (isIncrementalWordSearch && Global::imeMode != IME_MODE_ARRAY)
	{
		CStringRange wildcardSearch;
		DWORD_PTR keystrokeBufLen = _keystrokeBuffer.GetLength() + 3;


		// check keystroke buffer already has wildcard char which end user want wildcard search
		DWORD wildcardIndex = 0;
		BOOL isFindWildcard = FALSE;

		if (IsWildcard())
		{
			for (wildcardIndex = 0; wildcardIndex < _keystrokeBuffer.GetLength(); wildcardIndex++)
			{
				if (IsWildcardChar(*(_keystrokeBuffer.Get() + wildcardIndex)))
				{
					isFindWildcard = TRUE;
					break;
				}
			}
		}

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
		_pTableDictionaryEngine[Global::imeMode]->SetSearchSection(SEARCH_SECTION_TEXT);
		_pTableDictionaryEngine[Global::imeMode]->CollectWordForWildcard(&wildcardSearch, pCandidateList);
		//Search cutom table
		if (_pCustomTableDictionaryEngine[Global::imeMode])
			_pCustomTableDictionaryEngine[Global::imeMode]->CollectWordForWildcard(&wildcardSearch, pCandidateList);


		if (pCandidateList && 0 >= pCandidateList->Count())
		{
			return;
		}

		if (IsKeystrokeSort())
		{
			_pTableDictionaryEngine[Global::imeMode]->SortListItemByFindKeyCode(pCandidateList);
		}

		// Incremental search would show keystroke data from all candidate list items
		// but wont show identical keystroke data for user inputted.
		for (UINT index = 0; index < pCandidateList->Count(); index++)
		{
			CCandidateListItem *pLI = pCandidateList->GetAt(index);
			DWORD_PTR keystrokeBufferLen = 0;

			if (IsWildcard())
			{
				keystrokeBufferLen = wildcardIndex;
			}
			else
			{
				keystrokeBufferLen = _keystrokeBuffer.GetLength();
			}

			if (pLI)
			{
				CStringRange newFindKeyCode;
				newFindKeyCode.Set(pLI->_FindKeyCode.Get() + keystrokeBufferLen, pLI->_FindKeyCode.GetLength() - keystrokeBufferLen);
				pLI->_FindKeyCode.Set(newFindKeyCode);
			}
		}

		delete[] pwch;
	}
	else if (IsSymbol() && (Global::imeMode == IME_MODE_DAYI|| Global::imeMode == IME_MODE_ARRAY )) 
	{
		if (_pTableDictionaryEngine[Global::imeMode]->GetDictionaryType() == TTS_DICTIONARY)
			_pTableDictionaryEngine[Global::imeMode]->SetSearchSection(SEARCH_SECTION_SYMBOL);
		_pTableDictionaryEngine[Global::imeMode]->CollectWord(&_keystrokeBuffer, pCandidateList);
	}
	else if (isWildcardSearch)
	{
		_pTableDictionaryEngine[Global::imeMode]->SetSearchSection(SEARCH_SECTION_TEXT);
		_pTableDictionaryEngine[Global::imeMode]->CollectWordForWildcard(&_keystrokeBuffer, pCandidateList);

		//Search Array unicode extensions
		if (Global::imeMode == IME_MODE_ARRAY && CConfig::GetArrayUnicodeScope() != ARRAY_UNICODE_EXT_A)
		{
			switch (CConfig::GetArrayUnicodeScope())
			{

			case ARRAY_UNICODE_EXT_AB:
				if (_pArrayExtBTableDictionaryEngine)	_pArrayExtBTableDictionaryEngine->CollectWordForWildcard(&_keystrokeBuffer, pCandidateList);
				break;
			case ARRAY_UNICODE_EXT_ABCD:
				if (_pArrayExtBTableDictionaryEngine)	_pArrayExtBTableDictionaryEngine->CollectWordForWildcard(&_keystrokeBuffer, pCandidateList);
				if (_pArrayExtCDTableDictionaryEngine)	_pArrayExtCDTableDictionaryEngine->CollectWordForWildcard(&_keystrokeBuffer, pCandidateList);
				break;
			case ARRAY_UNICODE_EXT_ABCDE:
				if (_pArrayExtBTableDictionaryEngine)	_pArrayExtBTableDictionaryEngine->CollectWordForWildcard(&_keystrokeBuffer, pCandidateList);
				if (_pArrayExtCDTableDictionaryEngine)	_pArrayExtCDTableDictionaryEngine->CollectWordForWildcard(&_keystrokeBuffer, pCandidateList);
				if (_pArrayExtETableDictionaryEngine)	_pArrayExtETableDictionaryEngine->CollectWordForWildcard(&_keystrokeBuffer, pCandidateList);
				break;
			default:
				break;
			}

		}
		//Search cutom table
		if (_pCustomTableDictionaryEngine[Global::imeMode])
			_pCustomTableDictionaryEngine[Global::imeMode]->CollectWordForWildcard(&_keystrokeBuffer, pCandidateList);

	}
	else
	{
		_pTableDictionaryEngine[Global::imeMode]->SetSearchSection(SEARCH_SECTION_TEXT);
		_pTableDictionaryEngine[Global::imeMode]->CollectWord(&_keystrokeBuffer, pCandidateList);

		//Search Array unicode extensions
		if (Global::imeMode == IME_MODE_ARRAY && CConfig::GetArrayUnicodeScope() != ARRAY_UNICODE_EXT_A)
		{
			switch (CConfig::GetArrayUnicodeScope())
			{

			case ARRAY_UNICODE_EXT_AB:
				if (_pArrayExtBTableDictionaryEngine)	_pArrayExtBTableDictionaryEngine->CollectWord(&_keystrokeBuffer, pCandidateList);
				break;
			case ARRAY_UNICODE_EXT_ABCD:
				if (_pArrayExtBTableDictionaryEngine)	_pArrayExtBTableDictionaryEngine->CollectWord(&_keystrokeBuffer, pCandidateList);
				if (_pArrayExtCDTableDictionaryEngine)	_pArrayExtCDTableDictionaryEngine->CollectWord(&_keystrokeBuffer, pCandidateList);
				break;
			case ARRAY_UNICODE_EXT_ABCDE:
				if (_pArrayExtBTableDictionaryEngine)	_pArrayExtBTableDictionaryEngine->CollectWord(&_keystrokeBuffer, pCandidateList);
				if (_pArrayExtCDTableDictionaryEngine)	_pArrayExtCDTableDictionaryEngine->CollectWord(&_keystrokeBuffer, pCandidateList);
				if (_pArrayExtETableDictionaryEngine)	_pArrayExtETableDictionaryEngine->CollectWord(&_keystrokeBuffer, pCandidateList);
				break;
			default:
				break;
			}
		}
		//Search cutom table
		if (_pCustomTableDictionaryEngine[Global::imeMode])
			_pCustomTableDictionaryEngine[Global::imeMode]->CollectWord(&_keystrokeBuffer, pCandidateList);

	}
	
	
	_candidateWndWidth = DEFAULT_CAND_ITEM_LENGTH + TRAILING_SPACE;
	for (UINT index = 0; index < pCandidateList->Count();)
	{
		CCandidateListItem *pLI = pCandidateList->GetAt(index);
		CStringRange startItemString;
		CStringRange endItemString;

		if (pLI)
		{
			startItemString.Set(pLI->_ItemString.Get(), 1);
			endItemString.Set(pLI->_ItemString.Get() + pLI->_ItemString.GetLength() - 1, 1);

			if (pLI->_ItemString.GetLength() > _candidateWndWidth - TRAILING_SPACE)
			{
				_candidateWndWidth = (UINT)pLI->_ItemString.GetLength() + TRAILING_SPACE;
			}
			index++;
		}
	}
}

//+---------------------------------------------------------------------------
//
// GetCandidateStringInConverted
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::GetCandidateStringInConverted(CStringRange &searchString, _In_ CDIMEArray<CCandidateListItem> *pCandidateList)
{
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
		if (_pPhraseTableDictionaryEngine->GetDictionaryType() == TTS_DICTIONARY)
		{
			_pPhraseTableDictionaryEngine->SetSearchSection(SEARCH_SECTION_PHRASE);
		}
		_pPhraseTableDictionaryEngine->CollectWord(&searchText, pCandidateList);
	}
	//else // no phrase section, do wildcard text search
	//	_pPhraseTableDictionaryEngine->CollectWordFromConvertedStringForWildcard(&searchText, pCandidateList);

	if (IsKeystrokeSort())
		_pTableDictionaryEngine[Global::imeMode]->SortListItemByFindKeyCode(pCandidateList);

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
// IsSymbol
//
//----------------------------------------------------------------------------
BOOL CCompositionProcessorEngine::IsSymbol()
{
	if (_keystrokeBuffer.Get() == nullptr) return FALSE;
	if (Global::imeMode == IME_MODE_DAYI)
		return (_keystrokeBuffer.GetLength() < 3 && *_keystrokeBuffer.Get() == L'=');

	else if (Global::imeMode == IME_MODE_ARRAY)
		return (_keystrokeBuffer.GetLength() == 2 && towupper(*_keystrokeBuffer.Get()) == L'W'
		&& *(_keystrokeBuffer.Get() + 1) <= L'9' && *(_keystrokeBuffer.Get() + 1) >= L'0');

	else
		return FALSE;
}

//+---------------------------------------------------------------------------
//
// IsSymbolChar
//
//----------------------------------------------------------------------------
BOOL CCompositionProcessorEngine::IsSymbolChar(WCHAR wch)
{
	if (_keystrokeBuffer.Get() == nullptr) return FALSE;
	if ((_keystrokeBuffer.GetLength() == 1) &&
		(*_keystrokeBuffer.Get() == L'=') &&
		Global::imeMode == IME_MODE_DAYI && _pTableDictionaryEngine[IME_MODE_DAYI])
		//&&_pTableDictionaryEngine[IME_MODE_DAYI]->GetDictionaryType() == TTS_DICTIONARY)
	{
		for (UINT i = 0; i < wcslen(Global::DayiSymbolCharTable); i++)
		{
			if (Global::DayiSymbolCharTable[i] == wch)
			{
				return TRUE;
			}
		}

	}
	if ((_keystrokeBuffer.GetLength() == 1) && (towupper(*_keystrokeBuffer.Get()) == L'W') && Global::imeMode == IME_MODE_ARRAY)
	{
		if (wch >= L'0' && wch <= L'9')
		{
			return TRUE;
		}

	}
	return FALSE;
}

//+---------------------------------------------------------------------------
//
// IsDayiAddressChar
//
//----------------------------------------------------------------------------
BOOL CCompositionProcessorEngine::IsDayiAddressChar(WCHAR wch)
{
	if (Global::imeMode != IME_MODE_DAYI)
		//|| (_pTableDictionaryEngine[IME_MODE_DAYI] && _pTableDictionaryEngine[IME_MODE_DAYI]->GetDictionaryType() != TTS_DICTIONARY)) 
		return FALSE;

	for (int i = 0; i < ARRAYSIZE(Global::dayiAddressCharTable); i++)
	{
		if (Global::dayiAddressCharTable[i]._Code == wch)
		{
			return TRUE;
		}
	}

	return FALSE;
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
// IsArrayShortCode
//
//----------------------------------------------------------------------------
BOOL CCompositionProcessorEngine::IsArrayShortCode()
{
	if (Global::imeMode == IME_MODE_ARRAY && _keystrokeBuffer.GetLength() < 3) return TRUE;
	else
		return FALSE;
}

//+---------------------------------------------------------------------------
//
// checkArraySpeicalCode
//
//----------------------------------------------------------------------------
DWORD_PTR CCompositionProcessorEngine::CollectWordFromArraySpeicalCode(_Outptr_result_maybenull_ const WCHAR **ppwchSpecialCodeResultString)
{
	if (!IsDictionaryAvailable(Global::imeMode))
	{
		return 0;
	}
	*ppwchSpecialCodeResultString = nullptr;

	if (Global::imeMode != IME_MODE_ARRAY || _keystrokeBuffer.GetLength() != 2) return 0;

	CDIMEArray<CCandidateListItem> candidateList;

	if (_pArraySpecialCodeTableDictionaryEngine == nullptr)
	{
		_pTableDictionaryEngine[Global::imeMode]->SetSearchSection(SEARCH_SECTION_TEXT);
		_pTableDictionaryEngine[Global::imeMode]->CollectWord(&_keystrokeBuffer, &candidateList);
	}
	else
	{
		_pArraySpecialCodeTableDictionaryEngine->CollectWord(&_keystrokeBuffer, &candidateList);
	}

	if (candidateList.Count() == 1)
	{
		*ppwchSpecialCodeResultString = candidateList.GetAt(0)->_ItemString.Get();
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
BOOL CCompositionProcessorEngine::GetArraySpeicalCodeFromConvertedText(_In_ CStringRange *inword, _Out_ CStringRange *csrReslt)
{
	if (!IsDictionaryAvailable(Global::imeMode))
	{
		return FALSE;
	}

	if (Global::imeMode != IME_MODE_ARRAY || _pArraySpecialCodeTableDictionaryEngine == nullptr || inword == nullptr) return FALSE;

	CDIMEArray<CCandidateListItem> candidateList;
	_pArraySpecialCodeTableDictionaryEngine->CollectWordFromConvertedString(inword, &candidateList);
	if (candidateList.Count() == 1)
	{

		PWCHAR pwch;
		pwch = new (std::nothrow) WCHAR[MAX_READINGSTRING];
		*pwch = L'\0';
		if (_pTableDictionaryEngine[Global::imeMode]->GetRadicalMap() &&
			candidateList.GetAt(0)->_FindKeyCode.GetLength() && _pTableDictionaryEngine[Global::imeMode]->GetRadicalMap()->size())
		{
			for (UINT i = 0; i < candidateList.GetAt(0)->_FindKeyCode.GetLength(); i++)
			{ // query keyname from keymap
				_T_RadicalMap::iterator item =
					_pTableDictionaryEngine[Global::imeMode]->GetRadicalMap()->find(towupper(*(_keystrokeBuffer.Get() + i)));
				if (item != _pTableDictionaryEngine[Global::imeMode]->GetRadicalMap()->end())
				{
					assert(wcslen(pwch) + wcslen(item->second) < MAX_READINGSTRING - 1);
					StringCchCat(pwch, MAX_READINGSTRING, item->second);
				}
			}
			csrReslt->Set(pwch, candidateList.GetAt(0)->_FindKeyCode.GetLength());
			return TRUE;
		}
		else
		{
			delete pwch;
			csrReslt = inword;

		}
	}

	return FALSE;


}

IME_MODE CCompositionProcessorEngine::GetImeModeFromGuidProfile(REFGUID guidLanguageProfile)
{
	debugPrint(L"CCompositionProcessorEngine::GetImeModeFromGuidProfile() \n");
	IME_MODE imeMode = IME_MODE_NONE;
	if (guidLanguageProfile == Global::DIMEDayiGuidProfile)
	{
		debugPrint(L"CCompositionProcessorEngine::GetImeModeFromGuidProfile() : DAYI Mode");
		imeMode = IME_MODE_DAYI;
	}
	else if (guidLanguageProfile == Global::DIMEArrayGuidProfile)
	{
		debugPrint(L"CCompositionProcessorEngine::GetImeModeFromGuidProfile() : Array Mode");
		imeMode = IME_MODE_ARRAY;
	}
	else if (guidLanguageProfile == Global::DIMEPhoneticGuidProfile)
	{
		debugPrint(L"CCompositionProcessorEngine::GetImeModeFromGuidProfile() : Phonetic Mode");
		imeMode = IME_MODE_PHONETIC;
	}
	else if (guidLanguageProfile == Global::DIMEGenericGuidProfile)
	{
		debugPrint(L"CCompositionProcessorEngine::GetImeModeFromGuidProfile() : Generic Mode");
		imeMode = IME_MODE_GENERIC;
	}

	return imeMode;

}

HRESULT CCompositionProcessorEngine::GetReverseConversionResults(IME_MODE imeMode, _In_ LPCWSTR lpstrToConvert, _Inout_ CDIMEArray<CCandidateListItem> *pCandidateList)
{
	debugPrint(L"CCompositionProcessorEngine::GetReverseConversionResults() \n");


	if (_pTableDictionaryEngine[imeMode] == nullptr)
		return S_FALSE;

	_pTableDictionaryEngine[imeMode]->SetSearchSection(SEARCH_SECTION_TEXT);
	CStringRange csrToCovert;
	_pTableDictionaryEngine[imeMode]->CollectWordFromConvertedString(&csrToCovert.Set(lpstrToConvert, wcslen(lpstrToConvert)), pCandidateList);
	return S_OK;
}

//+---------------------------------------------------------------------------
//
// IsDoubleSingleByte
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::IsDoubleSingleByte(WCHAR wch)
{
	if (L' ' <= wch && wch <= L'~')
	{
		return TRUE;
	}
	return FALSE;
}

//+---------------------------------------------------------------------------
//
// SetupKeystroke
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::SetupKeystroke(IME_MODE imeMode)
{
	if (!IsDictionaryAvailable(imeMode))
	{
		return;
	}
	if (_pTableDictionaryEngine[imeMode] == nullptr || _pTableDictionaryEngine[imeMode]->GetRadicalMap() == nullptr ||
		_pTableDictionaryEngine[imeMode]->GetRadicalMap()->size() == 0 || _pTableDictionaryEngine[imeMode]->GetRadicalMap()->size() > MAX_RADICAL) return;

	_KeystrokeComposition.Clear();

	if (imeMode == IME_MODE_DAYI && _pTableDictionaryEngine[imeMode]->GetRadicalMap() &&
		(_pTableDictionaryEngine[imeMode]->GetRadicalMap()->find('=') == _pTableDictionaryEngine[imeMode]->GetRadicalMap()->end()))
	{ //dayi symbol prompt
		WCHAR *pwchEqual = new (std::nothrow) WCHAR[2];
		pwchEqual[0] = L'=';
		pwchEqual[1] = L'\0';
		(*_pTableDictionaryEngine[imeMode]->GetRadicalMap())['='] = pwchEqual;
	}

	for (_T_RadicalMap::iterator item = _pTableDictionaryEngine[imeMode]->GetRadicalMap()->begin(); item !=
		_pTableDictionaryEngine[imeMode]->GetRadicalMap()->end(); ++item)
	{
		_KEYSTROKE* pKS = nullptr;
		pKS = _KeystrokeComposition.Append();
		if (pKS == nullptr)
			break;

		pKS->Function = FUNCTION_INPUT;
		UINT vKey, modifier;
		WCHAR key = item->first;
		pKS->Printable = key;
		GetVKeyFromPrintable(key, &vKey, &modifier);
		pKS->VirtualKey = vKey;
		pKS->Modifiers = modifier;
	}

	return;
}

void CCompositionProcessorEngine::GetVKeyFromPrintable(WCHAR printable, UINT *vKey, UINT *modifier)
{
	/*
	#define VK_OEM_1          0xBA   // ';:' for US
	#define VK_OEM_PLUS       0xBB   // '+' any country
	#define VK_OEM_COMMA      0xBC   // ',' any country
	#define VK_OEM_MINUS      0xBD   // '-' any country
	#define VK_OEM_PERIOD     0xBE   // '.' any country
	#define VK_OEM_2          0xBF   // '/?' for US
	#define VK_OEM_3          0xC0   // '`/~' for US
	#define VK_OEM_PLUS       0xBB   // '+' any country
	#define VK_OEM_4          0xDB  //  '[{' for US
	#define VK_OEM_5          0xDC  //  '\|' for US
	#define VK_OEM_6          0xDD  //  ']}' for US
	#define VK_OEM_7          0xDE  //  ''"' for US
	*/
	*modifier = 0;

	if ((printable >= '0' && printable <= '9') || (printable >= 'A' && printable <= 'Z'))
	{
		*vKey = printable;
	}
	else if (printable == '!')
	{
		*vKey = '1';
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == '@')
	{
		*vKey = '2';
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == '#')
	{
		*vKey = '3';
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == '$')
	{
		*vKey = '4';
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == '%')
	{
		*vKey = '5';
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == '^')
	{
		*vKey = '6';
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == '&')
	{
		*vKey = '7';
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == '*')
	{
		*vKey = '8';
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == '(')
	{
		*vKey = '9';
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == ')')
	{
		*vKey = '0';
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == ',')
		*vKey = VK_OEM_COMMA;
	else if (printable == '<')
	{
		*vKey = VK_OEM_COMMA;
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == '.')
		*vKey = VK_OEM_PERIOD;
	else if (printable == '>')
	{
		*vKey = VK_OEM_PERIOD;
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == ';')
		*vKey = VK_OEM_1;
	else if (printable == ':')
	{
		*vKey = VK_OEM_1;
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == '/')
		*vKey = VK_OEM_2;
	else if (printable == '?')
	{
		*vKey = VK_OEM_2;
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == '`')
		*vKey = VK_OEM_3;
	else if (printable == '~')
	{
		*vKey = VK_OEM_3;
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == '[')
		*vKey = VK_OEM_4;
	else if (printable == '{')
	{
		*vKey = VK_OEM_4;
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == '\\')
		*vKey = VK_OEM_5;
	else if (printable == '|')
	{
		*vKey = VK_OEM_5;
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == ']')
		*vKey = VK_OEM_6;
	else if (printable == '}')
	{
		*vKey = VK_OEM_6;
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == '\'')
		*vKey = VK_OEM_7;
	else if (printable == '"')
	{
		*vKey = VK_OEM_7;
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == '-')
		*vKey = VK_OEM_MINUS;
	else if (printable == '_')
	{
		*vKey = VK_OEM_MINUS;
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == '=')
		*vKey = VK_OEM_PLUS;
	else if (printable == '+')
	{
		*vKey = VK_OEM_PLUS;
		*modifier = TF_MOD_SHIFT;
	}
	else
	{
		*vKey = printable;
	}

}

//+---------------------------------------------------------------------------
//
// SetupPreserved
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::SetupPreserved(_In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId)
{
	TF_PRESERVEDKEY preservedKeyImeMode;
	preservedKeyImeMode.uVKey = VK_SHIFT;
	preservedKeyImeMode.uModifiers = _TF_MOD_ON_KEYUP_SHIFT_ONLY;
	SetPreservedKey(Global::DIMEGuidImeModePreserveKey, preservedKeyImeMode, Global::ImeModeDescription, &_PreservedKey_IMEMode);

	TF_PRESERVEDKEY preservedKeyDoubleSingleByte;
	preservedKeyDoubleSingleByte.uVKey = VK_SPACE;
	preservedKeyDoubleSingleByte.uModifiers = TF_MOD_SHIFT;
	SetPreservedKey(Global::DIMEGuidDoubleSingleBytePreserveKey, preservedKeyDoubleSingleByte, Global::DoubleSingleByteDescription, &_PreservedKey_DoubleSingleByte);

	TF_PRESERVEDKEY preservedKeyConfig;
	preservedKeyConfig.uVKey = VK_OEM_5; // '\\'
	preservedKeyConfig.uModifiers = TF_MOD_CONTROL;
	SetPreservedKey(Global::DIMEGuidConfigPreserveKey, preservedKeyConfig, L"Show Config Pages", &_PreservedKey_Config);


	InitPreservedKey(&_PreservedKey_IMEMode, pThreadMgr, tfClientId);
	InitPreservedKey(&_PreservedKey_DoubleSingleByte, pThreadMgr, tfClientId);
	InitPreservedKey(&_PreservedKey_Config, pThreadMgr, tfClientId);

	return;
}

//+---------------------------------------------------------------------------
//
// SetKeystrokeTable
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::SetPreservedKey(const CLSID clsid, TF_PRESERVEDKEY & tfPreservedKey, _In_z_ LPCWSTR pwszDescription, _Out_ XPreservedKey *pXPreservedKey)
{
	if (pXPreservedKey == nullptr) return;
	pXPreservedKey->Guid = clsid;

	TF_PRESERVEDKEY *ptfPskey1 = pXPreservedKey->TSFPreservedKeyTable.Append();
	if (!ptfPskey1)
	{
		return;
	}
	*ptfPskey1 = tfPreservedKey;

	size_t srgKeystrokeBufLen = 0;
	if (StringCchLength(pwszDescription, STRSAFE_MAX_CCH, &srgKeystrokeBufLen) != S_OK)
	{
		return;
	}
	pXPreservedKey->Description = new (std::nothrow) WCHAR[srgKeystrokeBufLen + 1];
	if (!pXPreservedKey->Description)
	{
		return;
	}

	StringCchCopy((LPWSTR)pXPreservedKey->Description, srgKeystrokeBufLen, pwszDescription);

	return;
}
//+---------------------------------------------------------------------------
//
// InitPreservedKey
//
// Register a hot key.
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::InitPreservedKey(_In_ XPreservedKey *pXPreservedKey, _In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId)
{
	if (pThreadMgr == nullptr || pXPreservedKey == nullptr) return FALSE;
	ITfKeystrokeMgr *pKeystrokeMgr = nullptr;

	if (IsEqualGUID(pXPreservedKey->Guid, GUID_NULL))
	{
		return FALSE;
	}

	if (pThreadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void **)&pKeystrokeMgr) != S_OK || pKeystrokeMgr == nullptr)
	{
		return FALSE;
	}

	for (UINT i = 0; i < pXPreservedKey->TSFPreservedKeyTable.Count(); i++)
	{
		TF_PRESERVEDKEY preservedKey = *pXPreservedKey->TSFPreservedKeyTable.GetAt(i);
		preservedKey.uModifiers &= 0xffff;

		size_t lenOfDesc = 0;
		if (StringCchLength(pXPreservedKey->Description, STRSAFE_MAX_CCH, &lenOfDesc) != S_OK)
		{
			return FALSE;
		}
		pKeystrokeMgr->PreserveKey(tfClientId, pXPreservedKey->Guid, &preservedKey, pXPreservedKey->Description, static_cast<ULONG>(lenOfDesc));
	}

	pKeystrokeMgr->Release();

	return TRUE;
}

//+---------------------------------------------------------------------------
//
// CheckShiftKeyOnly
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::CheckShiftKeyOnly(_In_ CDIMEArray<TF_PRESERVEDKEY> *pTSFPreservedKeyTable)
{
	if (pTSFPreservedKeyTable == nullptr) return FALSE;
	for (UINT i = 0; i < pTSFPreservedKeyTable->Count(); i++)
	{
		TF_PRESERVEDKEY *ptfPskey = pTSFPreservedKeyTable->GetAt(i);

		if (((ptfPskey->uModifiers & (_TF_MOD_ON_KEYUP_SHIFT_ONLY & 0xffff0000)) && !Global::IsShiftKeyDownOnly) ||
			((ptfPskey->uModifiers & (_TF_MOD_ON_KEYUP_CONTROL_ONLY & 0xffff0000)) && !Global::IsControlKeyDownOnly) ||
			((ptfPskey->uModifiers & (_TF_MOD_ON_KEYUP_ALT_ONLY & 0xffff0000)) && !Global::IsAltKeyDownOnly))
		{
			return FALSE;
		}
	}

	return TRUE;
}

//+---------------------------------------------------------------------------
//
// OnPreservedKey
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::OnPreservedKey(REFGUID rguid, _Out_ BOOL *pIsEaten, _In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId)
{
	debugPrint(L"CCompositionProcessorEngine::OnPreservedKey() \n");
	if (IsEqualGUID(rguid, _PreservedKey_IMEMode.Guid))
	{
		if (!CheckShiftKeyOnly(&_PreservedKey_IMEMode.TSFPreservedKeyTable))
		{
			*pIsEaten = FALSE;
			return;
		}
		BOOL isOpen = FALSE;


		if (Global::isWindows8){
			isOpen = FALSE;
			CCompartment CompartmentKeyboardOpen(pThreadMgr, tfClientId, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
			CompartmentKeyboardOpen._GetCompartmentBOOL(isOpen);
			CompartmentKeyboardOpen._SetCompartmentBOOL(isOpen ? FALSE : TRUE);
		}
		else
		{
			CCompartment CompartmentIMEMode(pThreadMgr, tfClientId, Global::DIMEGuidCompartmentIMEMode);
			CompartmentIMEMode._GetCompartmentBOOL(isOpen);
			CompartmentIMEMode._SetCompartmentBOOL(isOpen ? FALSE : TRUE);
		}




		*pIsEaten = TRUE;
	}
	else if (IsEqualGUID(rguid, _PreservedKey_DoubleSingleByte.Guid))
	{
		if (!CheckShiftKeyOnly(&_PreservedKey_DoubleSingleByte.TSFPreservedKeyTable))
		{
			*pIsEaten = FALSE;
			return;
		}
		BOOL isDouble = FALSE;
		CCompartment CompartmentDoubleSingleByte(pThreadMgr, tfClientId, Global::DIMEGuidCompartmentDoubleSingleByte);
		CompartmentDoubleSingleByte._GetCompartmentBOOL(isDouble);
		CompartmentDoubleSingleByte._SetCompartmentBOOL(isDouble ? FALSE : TRUE);
		*pIsEaten = TRUE;
	}
	else if (IsEqualGUID(rguid, _PreservedKey_Config.Guid) && _pTextService)
	{
		// call config dialog
		_pTextService->Show(NULL, 0, GUID_NULL);
	}

	else
	{
		*pIsEaten = FALSE;
	}
	*pIsEaten = TRUE;
}

//+---------------------------------------------------------------------------
//
// SetupConfiguration
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::SetupConfiguration(IME_MODE imeMode)
{
	_isWildcard = TRUE;
	_isDisableWildcardAtFirst = TRUE;
	_isKeystrokeSort = FALSE;

	if (imeMode == IME_MODE_DAYI)
	{
		CConfig::SetAutoCompose(FALSE);
	}
	else if (imeMode == IME_MODE_ARRAY)
	{
		CConfig::SetSpaceAsPageDown(TRUE);
		CConfig::SetAutoCompose(TRUE);
	}
	else if (imeMode == IME_MODE_PHONETIC)
	{
		CConfig::SetSpaceAsPageDown(TRUE);
	}

	SetInitialCandidateListRange(imeMode);


	return;
}


//+---------------------------------------------------------------------------
//
// SetupDictionaryFile
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::SetupDictionaryFile(IME_MODE imeMode)
{
	debugPrint(L"CCompositionProcessorEngine::SetupDictionaryFile() \n");


	WCHAR wszProgramFiles[MAX_PATH];
	WCHAR wszAppData[MAX_PATH];

	if (GetEnvironmentVariable(L"ProgramW6432", wszProgramFiles, MAX_PATH) == 0)
	{//on 64-bit vista only 32bit app has this enviroment variable.  Which means the call failed when the apps running in 64-bit.
		//on 32-bit windows, this will definitely failed.  Get ProgramFiles enviroment variable now will retrive the correct program files path.
		GetEnvironmentVariable(L"ProgramFiles", wszProgramFiles, MAX_PATH);
	}

	//CSIDL_APPDATA  personal roadming application data.
	SHGetSpecialFolderPath(NULL, wszAppData, CSIDL_APPDATA, TRUE);

	debugPrint(L"CCompositionProcessorEngine::SetupDictionaryFile() :wszProgramFiles = %s", wszProgramFiles);

	WCHAR *pwszTTSFileName = new (std::nothrow) WCHAR[MAX_PATH];
	WCHAR *pwszCINFileName = new (std::nothrow) WCHAR[MAX_PATH];

	if (!pwszTTSFileName)  goto ErrorExit;
	if (!pwszCINFileName)  goto ErrorExit;

	*pwszTTSFileName = L'\0';
	*pwszCINFileName = L'\0';

	//tableTextService (TTS) dictionary file 
	if (imeMode != IME_MODE_ARRAY)
		StringCchPrintf(pwszTTSFileName, MAX_PATH, L"%s%s", wszProgramFiles, L"\\Windows NT\\TableTextService\\TableTextServiceDaYi.txt");
	else
		StringCchPrintf(pwszTTSFileName, MAX_PATH, L"%s%s", wszProgramFiles, L"\\Windows NT\\TableTextService\\TableTextServiceArray.txt");

	StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", wszAppData, L"\\DIME");

	if (!PathFileExists(pwszCINFileName))
	{
		//DIME roadming profile is not exist. Create one.
		CreateDirectory(pwszCINFileName, NULL);
	}
	// load main table file now
	if (imeMode == IME_MODE_DAYI) //dayi.cin in personal romaing profile
	{
		BOOL cinFound = FALSE;
		StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", wszAppData, L"\\DIME\\Dayi.cin");
		if (!PathFileExists(pwszCINFileName)) //failed back to try preload Dayi.cin in program files.
		{
			StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", wszProgramFiles, L"\\DIME\\Dayi.cin");
			if (PathFileExists(pwszCINFileName)) // failed to find Dayi.in in program files either
			{
				cinFound = TRUE;
			}
		}
		else
		{
			cinFound = TRUE;
		}

		if (cinFound &&
			_pTableDictionaryFile[imeMode] && _pTextService &&
			CompareString(_pTextService->GetLocale(), NORM_IGNORECASE, pwszCINFileName, -1, _pTableDictionaryFile[imeMode]->GetFileName(), -1) != CSTR_EQUAL)
		{ // cin found in Program files or in user roaming profile.
			delete _pTableDictionaryEngine[imeMode];
			_pTableDictionaryEngine[imeMode] = nullptr;
			delete _pTableDictionaryFile[imeMode];
			_pTableDictionaryFile[imeMode] = nullptr;
		}
	}
	if (imeMode == IME_MODE_ARRAY) //array.cin in personal romaing profile
	{
		BOOL cinFound = FALSE;
		StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", wszAppData, L"\\DIME\\Array.cin");
		if (!PathFileExists(pwszCINFileName)) //failed back to try preload Dayi.cin in program files.
		{
			StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", wszProgramFiles, L"\\DIME\\Array.cin");
			if (PathFileExists(pwszCINFileName)) // failed to find Array.in in program files either
			{
				cinFound = TRUE;
			}
		}
		else
		{
			cinFound = TRUE;
		}

		if (cinFound &&
			_pTableDictionaryFile[imeMode] && _pTextService &&
			CompareString(_pTextService->GetLocale(), NORM_IGNORECASE, pwszCINFileName, -1, _pTableDictionaryFile[imeMode]->GetFileName(), -1) != CSTR_EQUAL)
		{ //indicate the prevoius table is built with system preload file in program files, and now user provides their own.
			delete _pTableDictionaryEngine[imeMode];
			_pTableDictionaryEngine[imeMode] = nullptr;
			delete _pTableDictionaryFile[imeMode];
			_pTableDictionaryFile[imeMode] = nullptr;
		}
	}
	if (imeMode == IME_MODE_PHONETIC) //phone.cin in personal romaing profile
	{
		StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", wszAppData, L"\\DIME\\Phone.cin");
		if (!PathFileExists(pwszCINFileName)) //failed back to pre-install Phone.cin in program files.
			StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", wszProgramFiles, L"\\DIME\\Phone.cin");
		else if (_pTableDictionaryFile[imeMode] && _pTextService &&
			CompareString(_pTextService->GetLocale(), NORM_IGNORECASE, pwszCINFileName, -1, _pTableDictionaryFile[imeMode]->GetFileName(), -1) != CSTR_EQUAL)
		{ //indicate the prevoius table is built with system preload file in program files, and now user provides their own.
			delete _pTableDictionaryEngine[imeMode];
			_pTableDictionaryEngine[imeMode] = nullptr;
			delete _pTableDictionaryFile[imeMode];
			_pTableDictionaryFile[imeMode] = nullptr;
		}
	}
	if (imeMode == IME_MODE_GENERIC) //phone.cin in personal romaing profile
	{
		StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", wszAppData, L"\\DIME\\Generic.cin");
		// we don't provide preload Generic.cin in program files
	}

	if (PathFileExists(pwszCINFileName))  //create cin CFile object
	{
		//create CFile object
		if (_pTableDictionaryFile[imeMode] == nullptr)
		{
			_pTableDictionaryFile[imeMode] = new (std::nothrow) CFile();
			if ((_pTableDictionaryFile[imeMode]) && _pTextService &&
				(_pTableDictionaryFile[imeMode])->CreateFile(pwszCINFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))
			{
				_pTableDictionaryEngine[imeMode] = //cin files use tab as delimiter
					new (std::nothrow) CTableDictionaryEngine(_pTextService->GetLocale(), _pTableDictionaryFile[imeMode], CIN_DICTIONARY);
				_pTableDictionaryEngine[imeMode]->ParseConfig(imeMode); //parse config first.		

			}
			else
			{ // error on createfile. do cleanup
				delete _pTableDictionaryFile[imeMode];
				_pTableDictionaryFile[imeMode] = nullptr;
			}
		}
	}
	if (_pTableDictionaryEngine[imeMode] == nullptr && (imeMode == IME_MODE_DAYI || imeMode == IME_MODE_ARRAY))		//failed back to load windows preload tabletextservice table.
	{
		if (_pTableDictionaryEngine[imeMode] == nullptr)
		{
			_pTableDictionaryFile[imeMode] = new (std::nothrow) CFile();
			if (_pTableDictionaryFile[imeMode] == nullptr)  goto ErrorExit;

			if (!(_pTableDictionaryFile[imeMode])->CreateFile(pwszTTSFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))
			{
				goto ErrorExit;
			}
			else if (_pTextService)
			{
				_pTableDictionaryEngine[imeMode] = //TTS file use '=' as delimiter
					new (std::nothrow) CTableDictionaryEngine(_pTextService->GetLocale(), _pTableDictionaryFile[imeMode], TTS_DICTIONARY);
				if (_pTableDictionaryEngine[imeMode] == nullptr)  goto ErrorExit;

				_pTableDictionaryEngine[imeMode]->ParseConfig(imeMode); //parse config first.

			}
		}
	}

	// now load phrase table
	*pwszCINFileName = L'\0';
	StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", wszAppData, L"\\DIME\\Phrase.cin");
	if (PathFileExists(pwszCINFileName) && _pPhraseDictionaryFile == nullptr)
	{ //indicate the prevoius table is built with system preload tts file in program files, and now user provides their own.
		_pPhraseTableDictionaryEngine = nullptr;
	}
	//create CFile object
	if (_pPhraseTableDictionaryEngine == nullptr)
	{
		if (PathFileExists(pwszCINFileName))
		{
			_pPhraseDictionaryFile = new (std::nothrow) CFile();
			if (_pPhraseDictionaryFile && _pTextService && _pPhraseDictionaryFile->CreateFile(pwszCINFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))
			{
				_pPhraseTableDictionaryEngine =
					new (std::nothrow) CTableDictionaryEngine(_pTextService->GetLocale(), _pPhraseDictionaryFile, CIN_DICTIONARY); //cin files use tab as delimiter
				_pPhraseTableDictionaryEngine->ParseConfig(imeMode); //parse config first.				

			}
		}
		else if ((imeMode == IME_MODE_DAYI || imeMode == IME_MODE_ARRAY) && _pTableDictionaryFile[imeMode] &&
			_pTableDictionaryEngine[imeMode]->GetDictionaryType() == TTS_DICTIONARY)
		{
			_pPhraseTableDictionaryEngine = _pTableDictionaryEngine[imeMode];
		}
		else
		{
			_pPhraseDictionaryFile = new (std::nothrow) CFile();
			if (_pPhraseDictionaryFile == nullptr)  goto ErrorExit;

			if (!_pPhraseDictionaryFile->CreateFile(pwszTTSFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))
			{
				goto ErrorExit;
			}
			else if (_pTextService) // no user provided phrase table present and we are not in ARRAY or DAYI, thus we load TTS DAYI table to provide phrase table
			{
				_pPhraseTableDictionaryEngine =
					new (std::nothrow) CTableDictionaryEngine(_pTextService->GetLocale(), _pPhraseDictionaryFile, TTS_DICTIONARY); //TTS file use '=' as delimiter
				if (!_pPhraseTableDictionaryEngine)  goto ErrorExit;

				_pPhraseTableDictionaryEngine->ParseConfig(imeMode); //parse config first.

			}
		}
	}

	// now load Array unicode ext-b, ext-cd, ext-e , special code and short-code table
	if (imeMode == IME_MODE_ARRAY) //array-special.cin and array-shortcode.cin in personal romaing profile
	{
		//Ext-B
		StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", wszAppData, L"\\DIME\\Array-Ext-B.cin");
		if (!PathFileExists(pwszCINFileName)) //failed back to preload array-special.cin in program files.
			StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", wszProgramFiles, L"\\DIME\\Array-Ext-B.cin");
		else if (_pArrayExtBDictionaryFile && _pTextService &&
			CompareString(_pTextService->GetLocale(), NORM_IGNORECASE, pwszCINFileName, -1, _pArrayExtBDictionaryFile->GetFileName(), -1) != CSTR_EQUAL)
		{ //indicate the prevoius table is built with system preload file in program files, and now user provides their own.
			delete _pArrayExtBTableDictionaryEngine;
			_pArrayExtBTableDictionaryEngine = nullptr;
			delete _pArrayExtBDictionaryFile;
			_pArrayExtBDictionaryFile = nullptr;
		}

		if (_pArrayExtBDictionaryFile == nullptr)
		{
			_pArrayExtBDictionaryFile = new (std::nothrow) CFile();
			if (_pArrayExtBDictionaryFile && _pTextService &&
				_pArrayExtBDictionaryFile->CreateFile(pwszCINFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))
			{
				_pArrayExtBTableDictionaryEngine =
					new (std::nothrow) CTableDictionaryEngine(_pTextService->GetLocale(), _pArrayExtBDictionaryFile, CIN_DICTIONARY); //cin files use tab as delimiter
				if (_pArrayExtBTableDictionaryEngine)
					_pArrayExtBTableDictionaryEngine->ParseConfig(imeMode); // to release the file handle
			}
		}
		//Ext-CD
		StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", wszAppData, L"\\DIME\\Array-Ext-CD.cin");
		if (!PathFileExists(pwszCINFileName)) //failed back to preload array-special.cin in program files.
			StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", wszProgramFiles, L"\\DIME\\Array-Ext-CD.cin");
		else if (_pArrayExtCDDictionaryFile && _pTextService &&
			CompareString(_pTextService->GetLocale(), NORM_IGNORECASE, pwszCINFileName, -1, _pArrayExtCDDictionaryFile->GetFileName(), -1) != CSTR_EQUAL)
		{ //indicate the prevoius table is built with system preload file in program files, and now user provides their own.
			delete _pArrayExtCDTableDictionaryEngine;
			_pArrayExtCDTableDictionaryEngine = nullptr;
			delete _pArrayExtCDDictionaryFile;
			_pArrayExtCDDictionaryFile = nullptr;
		}

		if (_pArrayExtCDDictionaryFile == nullptr)
		{
			_pArrayExtCDDictionaryFile = new (std::nothrow) CFile();
			if (_pArrayExtCDDictionaryFile && _pTextService &&
				_pArrayExtCDDictionaryFile->CreateFile(pwszCINFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))
			{
				_pArrayExtCDTableDictionaryEngine =
					new (std::nothrow) CTableDictionaryEngine(_pTextService->GetLocale(), _pArrayExtCDDictionaryFile, CIN_DICTIONARY); //cin files use tab as delimiter
				if (_pArrayExtCDTableDictionaryEngine)
					_pArrayExtCDTableDictionaryEngine->ParseConfig(imeMode); // to release the file handle
			}
		}
		//Ext-E
		StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", wszAppData, L"\\DIME\\Array-Ext-E.cin");
		if (!PathFileExists(pwszCINFileName)) //failed back to preload array-special.cin in program files.
			StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", wszProgramFiles, L"\\DIME\\Array-Ext-E.cin");
		else if (_pArrayExtEDictionaryFile && _pTextService &&
			CompareString(_pTextService->GetLocale(), NORM_IGNORECASE, pwszCINFileName, -1, _pArrayExtEDictionaryFile->GetFileName(), -1) != CSTR_EQUAL)
		{ //indicate the prevoius table is built with system preload file in program files, and now user provides their own.
			delete _pArrayExtETableDictionaryEngine;
			_pArrayExtETableDictionaryEngine = nullptr;
			delete _pArrayExtEDictionaryFile;
			_pArrayExtEDictionaryFile = nullptr;
		}

		if (_pArrayExtEDictionaryFile == nullptr)
		{
			_pArrayExtEDictionaryFile = new (std::nothrow) CFile();
			if (_pArrayExtEDictionaryFile && _pTextService &&
				_pArrayExtEDictionaryFile->CreateFile(pwszCINFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))
			{
				_pArrayExtETableDictionaryEngine =
					new (std::nothrow) CTableDictionaryEngine(_pTextService->GetLocale(), _pArrayExtEDictionaryFile, CIN_DICTIONARY); //cin files use tab as delimiter
				if (_pArrayExtETableDictionaryEngine)
					_pArrayExtETableDictionaryEngine->ParseConfig(imeMode); // to release the file handle
			}
		}

		//Special
		StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", wszAppData, L"\\DIME\\Array-special.cin");
		if (!PathFileExists(pwszCINFileName)) //failed back to preload array-special.cin in program files.
			StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", wszProgramFiles, L"\\DIME\\Array-special.cin");
		else if (_pArraySpecialCodeDictionaryFile && _pTextService &&
			CompareString(_pTextService->GetLocale(), NORM_IGNORECASE, pwszCINFileName, -1, _pArraySpecialCodeDictionaryFile->GetFileName(), -1) != CSTR_EQUAL)
		{ //indicate the prevoius table is built with system preload file in program files, and now user provides their own.
			delete _pArraySpecialCodeTableDictionaryEngine;
			_pArraySpecialCodeTableDictionaryEngine = nullptr;
			delete _pArraySpecialCodeDictionaryFile;
			_pArraySpecialCodeDictionaryFile = nullptr;
		}

		if (_pArraySpecialCodeDictionaryFile == nullptr)
		{
			_pArraySpecialCodeDictionaryFile = new (std::nothrow) CFile();
			if (_pArraySpecialCodeDictionaryFile && _pTextService &&
				_pArraySpecialCodeDictionaryFile->CreateFile(pwszCINFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))
			{
				_pArraySpecialCodeTableDictionaryEngine =
					new (std::nothrow) CTableDictionaryEngine(_pTextService->GetLocale(), _pArraySpecialCodeDictionaryFile, CIN_DICTIONARY); //cin files use tab as delimiter
				if (_pArraySpecialCodeTableDictionaryEngine)
					_pArraySpecialCodeTableDictionaryEngine->ParseConfig(imeMode); // to release the file handle
			}
		}

		//Short-code
		StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", wszAppData, L"\\DIME\\Array-shortcode.cin");
		if (!PathFileExists(pwszCINFileName)) //failed back to preload array-shortcode.cin in program files.
			StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", wszProgramFiles, L"\\DIME\\Array-shortcode.cin");
		if (PathFileExists(pwszCINFileName) && _pArrayShortCodeDictionaryFile == nullptr)
		{
			_pArrayShortCodeDictionaryFile = new (std::nothrow) CFile();
			if (_pArrayShortCodeDictionaryFile && _pTextService &&
				_pArrayShortCodeDictionaryFile->CreateFile(pwszCINFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))
			{
				_pArrayShortCodeTableDictionaryEngine =
					new (std::nothrow) CTableDictionaryEngine(_pTextService->GetLocale(), _pArrayShortCodeDictionaryFile, CIN_DICTIONARY); //cin files use tab as delimiter
				if (_pArrayShortCodeTableDictionaryEngine)
					_pArrayShortCodeTableDictionaryEngine->ParseConfig(imeMode);// to release the file handle
			}

		}

	}

	// now load Dayi custom table
	if (imeMode == IME_MODE_DAYI)
		StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", wszAppData, L"\\DIME\\DAYI-CUSTOM.cin");
	else if (Global::imeMode == IME_MODE_ARRAY)
		StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", wszAppData, L"\\DIME\\ARRAY-CUSTOM.cin");
	else if (Global::imeMode == IME_MODE_PHONETIC)
		StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", wszAppData, L"\\DIME\\PHONETIC-CUSTOM.cin");
	else
		StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", wszAppData, L"\\DIME\\GENERIC-CUSTOM.cin");
	if (PathFileExists(pwszCINFileName) && _pCustomTableDictionaryEngine[imeMode] == nullptr)
	{
		_pCustomTableDictionaryFile[imeMode] = new (std::nothrow) CFile();
		if (_pCustomTableDictionaryFile[imeMode] && _pTextService &&
			_pCustomTableDictionaryFile[imeMode]->CreateFile(pwszCINFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))
		{
			_pCustomTableDictionaryEngine[imeMode] =
				new (std::nothrow) CTableDictionaryEngine(_pTextService->GetLocale(), _pCustomTableDictionaryFile[imeMode], CIN_DICTIONARY); //cin files use tab as delimiter
			if (_pCustomTableDictionaryEngine[imeMode])
				_pCustomTableDictionaryEngine[imeMode]->ParseConfig(imeMode);// to release the file handle
		}

	}



	delete[]pwszTTSFileName;
	delete[]pwszCINFileName;
	return TRUE;
ErrorExit:
	if (pwszTTSFileName)  delete[]pwszTTSFileName;
	if (pwszCINFileName)  delete[]pwszCINFileName;
	return FALSE;
}

BOOL CCompositionProcessorEngine::SetupHanCovertTable()
{
	debugPrint(L"CCompositionProcessorEngine::SetupHanCovertTable() \n");
	if (CConfig::GetDoHanConvert() && _pTCSCTableDictionaryEngine == nullptr)
	{
		WCHAR wszProgramFiles[MAX_PATH];

		if (GetEnvironmentVariable(L"ProgramW6432", wszProgramFiles, MAX_PATH) == 0)
		{//on 64-bit vista only 32bit app has this enviroment variable.  Which means the call failed when the apps running is 64-bit.
			//on 32-bit windows, this will definitely failed.  Get ProgramFiles enviroment variable now will retrive the correct program files path.
			GetEnvironmentVariable(L"ProgramFiles", wszProgramFiles, MAX_PATH);
		}


		debugPrint(L"CCompositionProcessorEngine::SetupDictionaryFile() :wszProgramFiles = %s", wszProgramFiles);

		WCHAR *pwszCINFileName = new (std::nothrow) WCHAR[MAX_PATH];
		if (!pwszCINFileName)  goto ErrorExit;
		*pwszCINFileName = L'\0';

		StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", wszProgramFiles, L"\\DIME\\TCSC.cin");
		if (_pTCSCTableDictionaryFile == nullptr)
		{
			_pTCSCTableDictionaryFile = new (std::nothrow) CFile();
			if (_pTCSCTableDictionaryFile && _pTextService &&
				(_pTCSCTableDictionaryFile)->CreateFile(pwszCINFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))
			{
				_pTCSCTableDictionaryEngine =
					new (std::nothrow) CTableDictionaryEngine(_pTextService->GetLocale(), _pTCSCTableDictionaryFile, CIN_DICTIONARY); //cin files use tab as delimiter
			}
		}


		delete[]pwszCINFileName;
		return TRUE;
	ErrorExit:
		if (pwszCINFileName)  delete[]pwszCINFileName;
	}
	return FALSE;
}
BOOL CCompositionProcessorEngine::GetTCFromSC(CStringRange* stringToConvert, CStringRange* convertedString)
{
	stringToConvert; convertedString;
	//not yet implemented
	return FALSE;
}
BOOL CCompositionProcessorEngine::GetSCFromTC(CStringRange* stringToConvert, CStringRange* convertedString)
{
	debugPrint(L"CCompositionProcessorEngine::GetSCFromTC()");
	if (!CConfig::GetDoHanConvert() || stringToConvert == nullptr || convertedString == nullptr) return FALSE;

	if (_pTCSCTableDictionaryEngine == nullptr) SetupHanCovertTable();
	if (_pTCSCTableDictionaryEngine == nullptr) return FALSE;

	UINT lenToConvert = (UINT)stringToConvert->GetLength();
	PWCHAR pwch = new (std::nothrow) WCHAR[lenToConvert + 1];
	*pwch = L'\0';
	CStringRange wcharToCover;

	for (UINT i = 0; i < lenToConvert; i++)
	{
		CDIMEArray<CCandidateListItem> candidateList;
		_pTCSCTableDictionaryEngine->CollectWord(&wcharToCover.Set(stringToConvert->Get() + i, 1), &candidateList);
		if (candidateList.Count() == 1)
			StringCchCatN(pwch, lenToConvert + 1, candidateList.GetAt(0)->_ItemString.Get(), 1);
		else
			StringCchCatN(pwch, lenToConvert + 1, wcharToCover.Get(), 1);
	}
	convertedString->Set(pwch, lenToConvert);
	return TRUE;
}

//+---------------------------------------------------------------------------
//
// GetDictionaryFile
//
//----------------------------------------------------------------------------

CFile* CCompositionProcessorEngine::GetDictionaryFile()
{
	return _pTableDictionaryFile[Global::imeMode];
}



//////////////////////////////////////////////////////////////////////
//
// XPreservedKey implementation.
//
//////////////////////////////////////////////////////////////////////+---------------------------------------------------------------------------
//
// UninitPreservedKey
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::XPreservedKey::UninitPreservedKey(_In_ ITfThreadMgr *pThreadMgr)
{
	ITfKeystrokeMgr* pKeystrokeMgr = nullptr;

	if (IsEqualGUID(Guid, GUID_NULL))
	{
		return FALSE;
	}

	if (pThreadMgr && FAILED(pThreadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void **)&pKeystrokeMgr)))
	{
		return FALSE;
	}

	if (pKeystrokeMgr == nullptr)
	{
		return FALSE;
	}

	for (UINT i = 0; i < TSFPreservedKeyTable.Count(); i++)
	{
		TF_PRESERVEDKEY pPreservedKey = *TSFPreservedKeyTable.GetAt(i);
		pPreservedKey.uModifiers &= 0xffff;

		pKeystrokeMgr->UnpreserveKey(Guid, &pPreservedKey);
	}

	pKeystrokeMgr->Release();

	return TRUE;
}

CCompositionProcessorEngine::XPreservedKey::XPreservedKey()
{
	Guid = GUID_NULL;
	Description = nullptr;
}

CCompositionProcessorEngine::XPreservedKey::~XPreservedKey()
{
	ITfThreadMgr* pThreadMgr = nullptr;

	HRESULT hr = CoCreateInstance(CLSID_TF_ThreadMgr, NULL, CLSCTX_INPROC_SERVER, IID_ITfThreadMgr, (void**)&pThreadMgr);
	if (SUCCEEDED(hr) && pThreadMgr)
	{
		UninitPreservedKey(pThreadMgr);
		pThreadMgr->Release();
		pThreadMgr = nullptr;
	}

	if (Description)
	{
		delete[] Description;
	}
}



void CCompositionProcessorEngine::SetInitialCandidateListRange(IME_MODE imeMode)
{
	_candidateListIndexRange.Clear();
	_phraseCandidateListIndexRange.Clear();
	for (DWORD i = 0; i < 10; i++)
	{
		DWORD* pNewIndexRange = nullptr;
		DWORD* pNewPhraseIndexRange = nullptr;

		pNewIndexRange = _candidateListIndexRange.Append();
		pNewPhraseIndexRange = _phraseCandidateListIndexRange.Append();
		if (pNewIndexRange != nullptr)
		{
			if (imeMode == IME_MODE_DAYI)	*pNewIndexRange = i;
			else
			{
				if (i != 9)
				{
					*pNewIndexRange = i + 1;
				}
				else
				{
					*pNewIndexRange = 0;
				}
			}
		}
		if (pNewPhraseIndexRange != nullptr)
		{
			if (i != 9)
			{
				*pNewPhraseIndexRange = i + 1;
			}
			else
			{
				*pNewPhraseIndexRange = 0;
			}

		}
	}
	_pActiveCandidateListIndexRange = &_candidateListIndexRange;  // Preset the active cand list range
}



//////////////////////////////////////////////////////////////////////
//
//    CCompositionProcessorEngine
//
//////////////////////////////////////////////////////////////////////+---------------------------------------------------------------------------
//
// CCompositionProcessorEngine::IsVirtualKeyNeed
//
// Test virtual key code need to the Composition Processor Engine.
// param
//     [in] uCode - Specify virtual key code.
//     [in/out] pwch       - char code
//     [in] fComposing     - Specified composing.
//     [in] fCandidateMode - Specified candidate mode.
//     [out] pKeyState     - Returns function regarding virtual key.
// returns
//     If engine need this virtual key code, returns true. Otherwise returns false.
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::IsVirtualKeyNeed(UINT uCode, _In_reads_(1) WCHAR *pwch, BOOL fComposing, CANDIDATE_MODE candidateMode, BOOL hasCandidateWithWildcard, UINT candiCount, _Out_opt_ _KEYSTROKE_STATE *pKeyState)
{
	if (pKeyState)
	{
		pKeyState->Category = CATEGORY_NONE;
		pKeyState->Function = FUNCTION_NONE;
	}

	if (candidateMode == CANDIDATE_ORIGINAL || candidateMode == CANDIDATE_PHRASE || candidateMode == CANDIDATE_WITH_NEXT_COMPOSITION)
	{
		fComposing = FALSE;
	}

	if (fComposing || candidateMode == CANDIDATE_INCREMENTAL || candidateMode == CANDIDATE_NONE)
	{
		if (IsVirtualKeyKeystrokeComposition(uCode, pKeyState, FUNCTION_NONE))
		{
			return TRUE;
		}
		else if ((IsWildcard() && IsWildcardChar(*pwch) && !IsDisableWildcardAtFirst()) ||
			(IsWildcard() && IsWildcardChar(*pwch) && IsDisableWildcardAtFirst() && _keystrokeBuffer.GetLength()))
		{
			if (pKeyState)
			{
				pKeyState->Category = CATEGORY_COMPOSING;
				pKeyState->Function = FUNCTION_INPUT;
			}
			return TRUE;
		}
		else if (_hasWildcardIncludedInKeystrokeBuffer && uCode == VK_SPACE)
		{
			if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_CONVERT_WILDCARD; } return TRUE;
		}
	}

	if (candidateMode == CANDIDATE_ORIGINAL || candidateMode == CANDIDATE_PHRASE || candidateMode == CANDIDATE_WITH_NEXT_COMPOSITION)
	{
		BOOL isRetCode = TRUE;
		if (IsVirtualKeyKeystrokeCandidate(uCode, pKeyState, candidateMode, &isRetCode, &_KeystrokeCandidate))
		{
			return isRetCode;
		}

		if (hasCandidateWithWildcard)
		{
			if (IsVirtualKeyKeystrokeCandidate(uCode, pKeyState, candidateMode, &isRetCode, &_KeystrokeCandidateWildcard))
			{
				return isRetCode;
			}
		}

		// Candidate list could not handle key. We can try to restart the composition.
		if (IsKeystrokeRange(uCode, pKeyState, candidateMode))
		{
			return TRUE;
		}
		if (IsVirtualKeyKeystrokeComposition(uCode, pKeyState, FUNCTION_INPUT))
		{
			if (candidateMode != CANDIDATE_ORIGINAL)
			{
				return TRUE;
			}
			else
			{
				if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_FINALIZE_CANDIDATELIST_AND_INPUT; }
				return TRUE;
			}
		}
	}

	// CANDIDATE_INCREMENTAL should process Keystroke.Candidate virtual keys.
	else if (candidateMode == CANDIDATE_INCREMENTAL)
	{
		if (IsArrayShortCode() && pKeyState && uCode == VK_SPACE)
		{
			pKeyState->Category = CATEGORY_COMPOSING;
			pKeyState->Function = FUNCTION_CONVERT;
			return TRUE;
		}
		BOOL isRetCode = TRUE;
		if (IsVirtualKeyKeystrokeCandidate(uCode, pKeyState, candidateMode, &isRetCode, &_KeystrokeCandidate))
		{
			return isRetCode;
		}
	}

	if (!fComposing && candidateMode != CANDIDATE_ORIGINAL && candidateMode != CANDIDATE_PHRASE && candidateMode != CANDIDATE_WITH_NEXT_COMPOSITION)
	{

		if (IsVirtualKeyKeystrokeComposition(uCode, pKeyState, FUNCTION_INPUT))
		{
			return TRUE;
		}
	}

	// System pre-defined keystroke
	if (fComposing)
	{
		if ((candidateMode != CANDIDATE_INCREMENTAL))
		{
			switch (uCode)
			{
			case VK_LEFT:   if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_MOVE_LEFT; } return TRUE;
			case VK_RIGHT:  if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_MOVE_RIGHT; } return TRUE;
			case VK_RETURN: if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_CONVERT; } return TRUE;
			case VK_INSERT:
			case VK_DELETE:
			case VK_ESCAPE: if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_CANCEL; } return TRUE;
			case VK_BACK:   if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_BACKSPACE; } return TRUE;
			case VK_UP:     if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_MOVE_UP; } return TRUE;
			case VK_DOWN:   if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_MOVE_DOWN; } return TRUE;
			case VK_PRIOR:  if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_MOVE_PAGE_UP; } return TRUE;
			case VK_NEXT:   if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_MOVE_PAGE_DOWN; } return TRUE;

			case VK_HOME:   if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_MOVE_PAGE_TOP; } return TRUE;
			case VK_END:    if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_MOVE_PAGE_BOTTOM; } return TRUE;

			case VK_SPACE:  if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_CONVERT; } return TRUE;
			}
		}
		else if (candidateMode == CANDIDATE_INCREMENTAL)
		{
			switch (uCode)
			{
				// VK_LEFT, VK_RIGHT - set *pIsEaten = FALSE for application could move caret left or right.
				// and for CUAS, invoke _HandleCompositionCancel() edit session due to ignore CUAS default key handler for send out terminate composition
			case VK_LEFT:
			case VK_RIGHT:
			{
							 if (pKeyState)
							 {
								 pKeyState->Category = CATEGORY_INVOKE_COMPOSITION_EDIT_SESSION;
								 pKeyState->Function = FUNCTION_CANCEL;
							 }
			}
				return FALSE;

			case VK_RETURN: if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_CONVERT; } return TRUE;
			case VK_ESCAPE: if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_CANCEL; } return TRUE;

				// VK_BACK - remove one char from reading string.
			case VK_BACK:   if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_BACKSPACE; } return TRUE;

			case VK_UP:     if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_MOVE_UP; } return TRUE;
			case VK_DOWN:   if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_MOVE_DOWN; } return TRUE;
			case VK_PRIOR:  if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_MOVE_PAGE_UP; } return TRUE;
			case VK_NEXT:   if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_MOVE_PAGE_DOWN; } return TRUE;

			case VK_HOME:   if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_MOVE_PAGE_TOP; } return TRUE;
			case VK_END:    if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_MOVE_PAGE_BOTTOM; } return TRUE;

			case VK_SPACE:  if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_CONVERT; } return TRUE;
			}
		}
	}

	if (candidateMode == CANDIDATE_ORIGINAL || candidateMode == CANDIDATE_PHRASE || candidateMode == CANDIDATE_WITH_NEXT_COMPOSITION)
	{
		switch (uCode)
		{
		case VK_UP:     if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_MOVE_UP; } return TRUE;
		case VK_DOWN:   if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_MOVE_DOWN; } return TRUE;
		case VK_PRIOR:  if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_MOVE_PAGE_UP; } return TRUE;
		case VK_NEXT:   if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_MOVE_PAGE_DOWN; } return TRUE;
		case VK_HOME:   if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_MOVE_PAGE_TOP; } return TRUE;
		case VK_END:    if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_MOVE_PAGE_BOTTOM; } return TRUE;
		case VK_LEFT:   if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_MOVE_LEFT; } return TRUE;
		case VK_RIGHT:  if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_MOVE_RIGHT; } return TRUE;
			/*
			case VK_LEFT:
			case VK_RIGHT:

			{
			if (pKeyState)
			{
			pKeyState->Category = CATEGORY_INVOKE_COMPOSITION_EDIT_SESSION;
			pKeyState->Function = FUNCTION_CANCEL;
			}
			}
			return FALSE;
			*/
		case VK_RETURN: if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_CONVERT; } return TRUE;
		case VK_SPACE:  if (pKeyState)
		{

							if ((candidateMode == CANDIDATE_WITH_NEXT_COMPOSITION)){ // space finalized the associate here instead of choose the first one (selected index = -1 for phrase candidates).
								if (pKeyState)
								{
									pKeyState->Category = CATEGORY_CANDIDATE;
									pKeyState->Function = FUNCTION_FINALIZE_CANDIDATELIST;
								}
							}
							else if (uCode == VK_SPACE && candidateMode != CANDIDATE_PHRASE && CConfig::GetSpaceAsPageDown() && candiCount > 10){
								pKeyState->Category = CATEGORY_CANDIDATE;
								pKeyState->Function = FUNCTION_MOVE_PAGE_DOWN;
							}
							else if (candidateMode == CANDIDATE_PHRASE){
								if (pKeyState)
								{
									pKeyState->Category = CATEGORY_INVOKE_COMPOSITION_EDIT_SESSION;
									pKeyState->Function = FUNCTION_CANCEL;
								}
								return FALSE;
							}
							else{
								pKeyState->Category = CATEGORY_CANDIDATE;
								pKeyState->Function = FUNCTION_CONVERT;
							}

							return TRUE;
		}
		case VK_BACK:   if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_CANCEL; } return TRUE;
		case VK_INSERT:
		case VK_DELETE:
		case VK_ESCAPE:
		{
						  if (candidateMode == CANDIDATE_WITH_NEXT_COMPOSITION)
						  {
							  if (pKeyState)
							  {
								  pKeyState->Category = CATEGORY_INVOKE_COMPOSITION_EDIT_SESSION;
								  pKeyState->Function = FUNCTION_FINALIZE_TEXTSTORE;
							  }
							  return TRUE;
						  }
						  else
						  {
							  if (pKeyState)
							  {
								  pKeyState->Category = CATEGORY_CANDIDATE;
								  pKeyState->Function = FUNCTION_CANCEL;
							  }
							  return TRUE;
						  }
		}

		}

		if (candidateMode == CANDIDATE_WITH_NEXT_COMPOSITION)
		{
			if (IsVirtualKeyKeystrokeComposition(uCode, NULL, FUNCTION_NONE))
			{
				if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_FINALIZE_TEXTSTORE_AND_INPUT; } return TRUE;
			}
		}
	}
	if (IsKeystrokeRange(uCode, pKeyState, candidateMode))
	{
		return TRUE;
	}
	else if (pKeyState && pKeyState->Category != CATEGORY_NONE)
	{
		return FALSE;
	}

	if (*pwch && !IsVirtualKeyKeystrokeComposition(uCode, pKeyState, FUNCTION_NONE))
	{
		if (pKeyState)
		{
			pKeyState->Category = CATEGORY_INVOKE_COMPOSITION_EDIT_SESSION;
			pKeyState->Function = FUNCTION_FINALIZE_TEXTSTORE;
		}
		return FALSE;
	}

	return FALSE;
}

//+---------------------------------------------------------------------------
//
// CCompositionProcessorEngine::IsVirtualKeyKeystrokeComposition
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::IsVirtualKeyKeystrokeComposition(UINT uCode, _Out_opt_ _KEYSTROKE_STATE *pKeyState, KEYSTROKE_FUNCTION function)
{
	if (pKeyState == nullptr)
	{
		return FALSE;
	}

	pKeyState->Category = CATEGORY_NONE;
	pKeyState->Function = FUNCTION_NONE;

	for (UINT i = 0; i < _KeystrokeComposition.Count(); i++)
	{
		_KEYSTROKE *pKeystroke = nullptr;

		pKeystroke = _KeystrokeComposition.GetAt(i);

		if ((pKeystroke->VirtualKey == uCode) && Global::CheckModifiers(Global::ModifiersValue, pKeystroke->Modifiers))
		{
			if (function == FUNCTION_NONE)
			{
				pKeyState->Category = CATEGORY_COMPOSING;
				pKeyState->Function = pKeystroke->Function;
				return TRUE;
			}
			else if (function == pKeystroke->Function)
			{
				pKeyState->Category = CATEGORY_COMPOSING;
				pKeyState->Function = pKeystroke->Function;
				return TRUE;
			}
		}
	}

	return FALSE;
}

//+---------------------------------------------------------------------------
//
// CCompositionProcessorEngine::IsVirtualKeyKeystrokeCandidate
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::IsVirtualKeyKeystrokeCandidate(UINT uCode, _In_ _KEYSTROKE_STATE *pKeyState, CANDIDATE_MODE candidateMode, _Out_ BOOL *pfRetCode, _In_ CDIMEArray<_KEYSTROKE> *pKeystrokeMetric)
{
	candidateMode;
	if (pfRetCode == nullptr || pKeystrokeMetric == nullptr)
	{
		return FALSE;
	}
	*pfRetCode = FALSE;

	for (UINT i = 0; i < pKeystrokeMetric->Count(); i++)
	{
		_KEYSTROKE *pKeystroke = nullptr;

		pKeystroke = pKeystrokeMetric->GetAt(i);

		if (pKeystroke && (pKeystroke->VirtualKey == uCode) && Global::CheckModifiers(Global::ModifiersValue, pKeystroke->Modifiers))
		{
			*pfRetCode = TRUE;
			if (pKeyState)
			{
				pKeyState->Category = CATEGORY_CANDIDATE;
				//(candidateMode == CANDIDATE_ORIGINAL ? CATEGORY_CANDIDATE :
				//candidateMode == CANDIDATE_PHRASE ? CATEGORY_PHRASE : CATEGORY_CANDIDATE);

				pKeyState->Function = pKeystroke->Function;
			}
			return TRUE;
		}
	}

	return FALSE;
}

//+---------------------------------------------------------------------------
//
// CCompositionProcessorEngine::IsKeyKeystrokeRange
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::IsKeystrokeRange(UINT uCode, _Out_ _KEYSTROKE_STATE *pKeyState, CANDIDATE_MODE candidateMode)
{
	if (pKeyState == nullptr)
	{
		return FALSE;
	}

	pKeyState->Category = CATEGORY_NONE;
	pKeyState->Function = FUNCTION_NONE;

	if (_pActiveCandidateListIndexRange->IsRange(uCode, candidateMode))
	{
		if (candidateMode == CANDIDATE_PHRASE)
		{
			// Candidate phrase could specify *modifier
			if ((GetCandidateListPhraseModifier() == 0 && (Global::ModifiersValue & (TF_MOD_LSHIFT | TF_MOD_SHIFT)) != 0) || //shift + 123...
				(GetCandidateListPhraseModifier() != 0 && Global::CheckModifiers(Global::ModifiersValue, GetCandidateListPhraseModifier())))
			{
				pKeyState->Category = CATEGORY_CANDIDATE;
				pKeyState->Function = FUNCTION_SELECT_BY_NUMBER;
				return TRUE;
			}
			else
			{
				pKeyState->Category = CATEGORY_INVOKE_COMPOSITION_EDIT_SESSION;
				pKeyState->Function = pKeyState->Function = FUNCTION_CANCEL;  //No shift present, cancel phrsae mode.
				return FALSE;
			}
		}
		else if (candidateMode == CANDIDATE_WITH_NEXT_COMPOSITION)
		{
			// Candidate phrase could specify *modifier
			if ((GetCandidateListPhraseModifier() == 0 && (Global::ModifiersValue & (TF_MOD_LSHIFT | TF_MOD_SHIFT)) != 0) || //shift + 123...
				(GetCandidateListPhraseModifier() != 0 && Global::CheckModifiers(Global::ModifiersValue, GetCandidateListPhraseModifier())))
			{
				pKeyState->Category = CATEGORY_CANDIDATE;
				pKeyState->Function = FUNCTION_SELECT_BY_NUMBER;
				return TRUE;
			}
			// else next composition
		}
		else if (candidateMode != CANDIDATE_NONE)
		{
			pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_SELECT_BY_NUMBER;
			return TRUE;
		}
	}
	return FALSE;
}




void CCompositionProcessorEngine::UpdateDictionaryFile()
{
	CFile* pCurrentDictioanryFile = _pTableDictionaryFile[Global::imeMode];

	SetupDictionaryFile(_imeMode);
	if (pCurrentDictioanryFile != _pTableDictionaryFile[Global::imeMode])
	{ // the table is loaded from TTS previously and now new cin is loaded.
		SetupKeystroke(Global::imeMode);
		SetupConfiguration(Global::imeMode);
	}

	if (_pTableDictionaryFile[Global::imeMode] && _pTableDictionaryEngine[Global::imeMode] &&
		_pTableDictionaryEngine[Global::imeMode]->GetDictionaryType() == CIN_DICTIONARY &&
		_pTableDictionaryFile[Global::imeMode]->IsFileUpdated())
	{
		// the table is loaded from .cin and the cin was updated.
		_pTableDictionaryEngine[Global::imeMode]->ParseConfig(Global::imeMode);
		SetupKeystroke(Global::imeMode);
		SetupConfiguration(Global::imeMode);

	}
}