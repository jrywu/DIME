//
//
// Derived from Microsoft Sample IME by Jeremy '13,7,17
//
//
//#define DEBUG_PRINT
#include <Shlobj.h>
#include <Shlwapi.h>
#include "Private.h"
#include "TSFTTS.h"
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

CCompositionProcessorEngine::CCompositionProcessorEngine(_In_ CTSFTTS *pTextService)
{
	debugPrint(L"CCompositionProcessorEngine::CCompositionProcessorEngine() constructor");
	_pTextService = pTextService;
    _pTextService->AddRef();

	_imeMode = IME_MODE_NONE;

    
	for (UINT i =0 ; i< IM_SLOTS ; i++)
	{
		_pTableDictionaryEngine[i] = nullptr;
		_pTableDictionaryFile[i] = nullptr;
	}

	
	_pPhraseTableDictionaryEngine = nullptr;
	_pArrayShortCodeTableDictionaryEngine = nullptr;
	_pArraySpecialCodeTableDictionaryEngine = nullptr;
	
	_pPhraseDictionaryFile =nullptr;
	_pArraySpecialCodeDictionaryFile = nullptr;
	_pArrayShortCodeDictionaryFile = nullptr;

	_pTCSCTableDictionaryEngine = nullptr;
	_pTCSCTableDictionaryFile =nullptr;
   
    _tfClientId = TF_CLIENTID_NULL;

	
 
    _hasWildcardIncludedInKeystrokeBuffer = FALSE;

    _isWildcard = FALSE;
    _isDisableWildcardAtFirst = FALSE;
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
	if(_pTextService) _pTextService->Release();
	ReleaseDictionaryFiles();
}
void CCompositionProcessorEngine::ReleaseDictionaryFiles()
{
	
	for (UINT i =0 ; i < IM_SLOTS ; i++)
	{
		if (_pTableDictionaryFile[i])
		{
			delete _pTableDictionaryFile[i];
			_pTableDictionaryFile[i] = nullptr;
		}
		if (_pTableDictionaryEngine[i])
		{   // _pTableDictionaryEngine[i] is only a pointer to either _pTTSDictionaryFile[i] or _pCINTableDictionaryEngine. no need to delete it.
			_pTableDictionaryEngine[i] = nullptr;
		}

	}
	if (_pTCSCTableDictionaryEngine)
	{
		delete _pTCSCTableDictionaryEngine;
		_pTCSCTableDictionaryEngine = nullptr;
	}
	if (_pTCSCTableDictionaryFile)
	{
		delete _pTCSCTableDictionaryFile;
		_pTCSCTableDictionaryFile =nullptr;
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
	if((UINT)_keystrokeBuffer.GetLength() >= CConfig::GetMaxCodes() )  // do not eat the key if keystroke buffer length >= _maxcodes
	{
		DoBeep();
		return FALSE;
	}
    //
    // append one keystroke in buffer.
    //
    DWORD_PTR srgKeystrokeBufLen = _keystrokeBuffer.GetLength();
    PWCHAR pwch = new (std::nothrow) WCHAR[ srgKeystrokeBufLen + 1 ];
    if (!pwch)
    {
        return FALSE;
    }

    memcpy(pwch, _keystrokeBuffer.Get(), srgKeystrokeBufLen * sizeof(WCHAR));
    pwch[ srgKeystrokeBufLen ] = wch;

    if (_keystrokeBuffer.Get())
    {
        delete [] _keystrokeBuffer.Get();
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
    if ( _keystrokeBuffer.Get())
    {
        delete [] _keystrokeBuffer.Get();
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

void CCompositionProcessorEngine::GetReadingStrings(_Inout_ CTSFTTSArray<CStringRange> *pReadingStrings, _Out_ BOOL *pIsWildcardIncluded)
{
    CStringRange oneKeystroke;

    _hasWildcardIncludedInKeystrokeBuffer = FALSE;

    if (pReadingStrings->Count() == 0 && _keystrokeBuffer.GetLength())
    {
        CStringRange* pNewString = nullptr;

        pNewString = pReadingStrings->Append();
        if (pNewString)
        {
            *pNewString = _keystrokeBuffer;
        }

		PWCHAR pwchRadical;
		pwchRadical = new (std::nothrow) WCHAR[MAX_READINGSTRING];

		if(_pTableDictionaryEngine[Global::imeMode]->GetRadicalMap() && 
			_pTableDictionaryEngine[Global::imeMode]->GetRadicalMap()->size()&& !IsSymbol())
		{
			*pwchRadical = L'\0';
			for (DWORD index = 0; index < _keystrokeBuffer.GetLength(); index++)
			{
				if(_pTableDictionaryEngine[Global::imeMode]->GetRadicalMap()&&
					_pTableDictionaryEngine[Global::imeMode]->GetRadicalMap()->size() && !IsSymbol()) // if radicalMap is valid (size()>0), then convert the keystroke buffer 
				{
					_T_RacialMap::iterator item = 
						_pTableDictionaryEngine[Global::imeMode]->GetRadicalMap()->find(towupper(*(_keystrokeBuffer.Get() + index)));
					if(_pTableDictionaryEngine[Global::imeMode]->GetRadicalMap() &&
						item != _pTableDictionaryEngine[Global::imeMode]->GetRadicalMap()->end() )
					{
						assert(wcslen(pwchRadical) + wcslen(item->second) < MAX_READINGSTRING -1 );
						StringCchCat(pwchRadical, MAX_READINGSTRING, item->second); 
					}
				}

				oneKeystroke.Set(_keystrokeBuffer.Get() + index, 1);

				if (IsWildcard() && IsWildcardChar(*oneKeystroke.Get()))
				{
					_hasWildcardIncludedInKeystrokeBuffer = TRUE;
				}
			}
			pNewString->Set(pwchRadical, wcslen(pwchRadical));
		}
		else
		{
			delete [] pwchRadical;
		}
    }

    *pIsWildcardIncluded = _hasWildcardIncludedInKeystrokeBuffer;
}

//+---------------------------------------------------------------------------
//
// GetCandidateList
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::GetCandidateList(_Inout_ CTSFTTSArray<CCandidateListItem> *pCandidateList, BOOL isIncrementalWordSearch, BOOL isWildcardSearch)
{
    if (!IsDictionaryAvailable())
    {
        return;
    }

    if (isIncrementalWordSearch)
    {
        CStringRange wildcardSearch;
        DWORD_PTR keystrokeBufLen = _keystrokeBuffer.GetLength() + 3;
       

        // check keystroke buffer already has wildcard char which end user want wildcard serach
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

        PWCHAR pwch = new (std::nothrow) WCHAR[ keystrokeBufLen ];
        if (!pwch)  return;
		PWCHAR pwch3code = new (std::nothrow) WCHAR[ 6 ];
		if (!pwch3code) return;

		if (!isFindWildcard  && (Global::imeMode == IME_MODE_DAYI && CConfig::GetThreeCodeMode() && _keystrokeBuffer.GetLength() == 3))
        {
			StringCchCopyN(pwch, keystrokeBufLen, _keystrokeBuffer.Get(), _keystrokeBuffer.GetLength());
			StringCchCat(pwch, keystrokeBufLen, L"*");
			size_t len = 0;
			if (StringCchLength(pwch, STRSAFE_MAX_CCH, &len) == S_OK)
					wildcardSearch.Set(pwch, len);
			

			pwch3code[0] = *(_keystrokeBuffer.Get()); pwch3code[1] = *(_keystrokeBuffer.Get()+1);       
			pwch3code[2] = L'*';
			pwch3code[3] = *(_keystrokeBuffer.Get()+2);    
			pwch3code[4] = L'*'; pwch3code[5] = L'\0'; 
			
			CTSFTTSArray<CCandidateListItem> wCandidateList;
			CStringRange w3codeMode;		
			_pTableDictionaryEngine[Global::imeMode]->SetSearchSection(SEARCH_SECTION_TEXT);
			_pTableDictionaryEngine[Global::imeMode]->CollectWordForWildcard(&wildcardSearch, pCandidateList);
			if(pCandidateList->Count())	
			{
				_pTableDictionaryEngine[Global::imeMode]->SetSearchSection(SEARCH_SECTION_TEXT);
				_pTableDictionaryEngine[Global::imeMode]->CollectWordForWildcard(&w3codeMode.Set(pwch3code,4), &wCandidateList);
				if(wCandidateList.Count())
				{   //append the candidate items got with 3codemode wildcard string to the end of exact match items.
					for(UINT i = 0; i < wCandidateList.Count(); i++)
					{
						if(!CStringRange::WildcardCompare(_pTextService->GetLocale(), &wildcardSearch, &(wCandidateList.GetAt(i)->_FindKeyCode)))
						{
							CCandidateListItem* pLI = nullptr;
							pLI = pCandidateList->Append();
							if (pLI)
							{
								pLI->_ItemString.Set(wCandidateList.GetAt(i)->_ItemString);
								pLI->_FindKeyCode.Set(wCandidateList.GetAt(i)->_FindKeyCode);
							}
						}
					}
					wCandidateList.Clear();
				}
			}
			else
			{
				_pTableDictionaryEngine[Global::imeMode]->SetSearchSection(SEARCH_SECTION_TEXT);
				_pTableDictionaryEngine[Global::imeMode]->CollectWordForWildcard(&w3codeMode.Set(pwch3code,4), pCandidateList);
			}


        }
		else    // add wildcard char for incremental search
		{
			StringCchCopyN(pwch, keystrokeBufLen, _keystrokeBuffer.Get(), _keystrokeBuffer.GetLength());
			if(!isFindWildcard)
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
		}

        

        if (0 >= pCandidateList->Count())
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

            CStringRange newFindKeyCode;
            newFindKeyCode.Set(pLI->_FindKeyCode.Get() + keystrokeBufferLen, pLI->_FindKeyCode.GetLength() - keystrokeBufferLen);
            pLI->_FindKeyCode.Set(newFindKeyCode);
        }

        delete [] pwch;
		delete [] pwch3code;
    }
    else if (isWildcardSearch)
    {
		_pTableDictionaryEngine[Global::imeMode]->SetSearchSection(SEARCH_SECTION_TEXT);
        _pTableDictionaryEngine[Global::imeMode]->CollectWordForWildcard(&_keystrokeBuffer, pCandidateList);
    }
	else if (Global::imeMode == IME_MODE_DAYI && CConfig::GetThreeCodeMode() && _keystrokeBuffer.GetLength() == 3)
	{
		CStringRange wildcardSearch;
        PWCHAR pwch = new (std::nothrow) WCHAR[ 5 ];
        if (!pwch) return;
		pwch[0] = *(_keystrokeBuffer.Get());	pwch[1] = *(_keystrokeBuffer.Get()+1);       
		pwch[2] = L'?';		pwch[3] = *(_keystrokeBuffer.Get()+2);      
		wildcardSearch.Set(pwch, 4);
		CTSFTTSArray<CCandidateListItem> wCandidateList;
		
		_pTableDictionaryEngine[Global::imeMode]->SetSearchSection(SEARCH_SECTION_TEXT);
		_pTableDictionaryEngine[Global::imeMode]->CollectWordForWildcard(&_keystrokeBuffer, pCandidateList);
		if(pCandidateList->Count())
		{
			_pTableDictionaryEngine[Global::imeMode]->CollectWordForWildcard(&wildcardSearch, &wCandidateList);
			if(wCandidateList.Count())
			{   //append the candidate items got with 3codemode wildcard string to the end of exact match items.
				for(UINT i = 0; i < wCandidateList.Count(); i++)
				{
					if(!(CStringRange::Compare(_pTextService->GetLocale(), &_keystrokeBuffer, &(wCandidateList.GetAt(i)->_FindKeyCode)) == CSTR_EQUAL))
					{
						CCandidateListItem* pLI = nullptr;
						pLI = pCandidateList->Append();
						if (pLI)
						{
							pLI->_ItemString.Set(wCandidateList.GetAt(i)->_ItemString);
							pLI->_FindKeyCode.Set(wCandidateList.GetAt(i)->_FindKeyCode);
						}
					}
				}
				wCandidateList.Clear();
			}
		}
		else //if no exact match items found, send the results from 3codemode 
		{
			_pTableDictionaryEngine[Global::imeMode]->CollectWordForWildcard(&wildcardSearch, pCandidateList);
		}
		delete [] pwch;
	}
    else if(IsSymbol())
	{
		_pTableDictionaryEngine[Global::imeMode]->SetSearchSection(SEARCH_SECTION_SYMBOL);
		_pTableDictionaryEngine[Global::imeMode]->CollectWord(&_keystrokeBuffer, pCandidateList);
    }
	else if(Global::imeMode== IME_MODE_ARRAY && (_keystrokeBuffer.GetLength()<3)) //array short code mode
	{
		if(_pArrayShortCodeTableDictionaryEngine == nullptr)
		{
			_pTableDictionaryEngine[Global::imeMode]->SetSearchSection(SEARCH_SECTION_PRHASE_FROM_KEYSTROKE);
			_pTableDictionaryEngine[Global::imeMode]->CollectWord(&_keystrokeBuffer, pCandidateList);
		}
		else
		{
			_pArrayShortCodeTableDictionaryEngine->CollectWord(&_keystrokeBuffer, pCandidateList);
		}
	}
	else
	{
		_pTableDictionaryEngine[Global::imeMode]->SetSearchSection(SEARCH_SECTION_TEXT);
        _pTableDictionaryEngine[Global::imeMode]->CollectWord(&_keystrokeBuffer, pCandidateList);
	}

	_candidateWndWidth = DEFAULT_CAND_ITEM_LENGTH + TRAILING_SPACE;
    for (UINT index = 0; index < pCandidateList->Count();)
    {
        CCandidateListItem *pLI = pCandidateList->GetAt(index);
        CStringRange startItemString;
        CStringRange endItemString;

        startItemString.Set(pLI->_ItemString.Get(), 1);
        endItemString.Set(pLI->_ItemString.Get() + pLI->_ItemString.GetLength() - 1, 1);

		if(pLI->_ItemString.GetLength() > _candidateWndWidth - TRAILING_SPACE )
		{
			_candidateWndWidth = (UINT) pLI->_ItemString.GetLength() + TRAILING_SPACE;
		}
        index++;
    }
}

//+---------------------------------------------------------------------------
//
// GetCandidateStringInConverted
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::GetCandidateStringInConverted(CStringRange &searchString, _In_ CTSFTTSArray<CCandidateListItem> *pCandidateList)
{
    if (!IsDictionaryAvailable())
    {
        return;
    }

    // Search phrase from SECTION_TEXT's converted string list
    CStringRange searchText;
    DWORD_PTR srgKeystrokeBufLen = searchString.GetLength() + 2;
    PWCHAR pwch = new (std::nothrow) WCHAR[ srgKeystrokeBufLen ];
    if (!pwch)
    {
        return;
    }

    StringCchCopyN(pwch, srgKeystrokeBufLen, searchString.Get(), searchString.GetLength());
	if(!(Global::hasPhraseSection || Global::hasCINPhraseSection)) StringCchCat(pwch, srgKeystrokeBufLen, L"*");  // do wild search if no TTS [Phrase]section present

    // add wildcard char
	size_t len = 0;
	if (StringCchLength(pwch, STRSAFE_MAX_CCH, &len) != S_OK)
    {
        return;
    }
    searchText.Set(pwch, len);

	if(_pPhraseTableDictionaryEngine) // do phrase lookup if CIN file has phrase section
	{
		if(_pPhraseTableDictionaryEngine->GetDictionaryType() == TTS_DICTIONARY)
		{
			_pPhraseTableDictionaryEngine->SetSearchSection(SEARCH_SECTION_PHRASE);
		}
		_pPhraseTableDictionaryEngine->CollectWord(&searchText, pCandidateList);
	}	
	else // no phrase section, do wildcard text search
		_pPhraseTableDictionaryEngine->CollectWordFromConvertedStringForWildcard(&searchText, pCandidateList);

	if (IsKeystrokeSort())
		_pTableDictionaryEngine[Global::imeMode]->SortListItemByFindKeyCode(pCandidateList);
	
	_candidateWndWidth = DEFAULT_CAND_ITEM_LENGTH + TRAILING_SPACE;
	for (UINT index = 0; index < pCandidateList->Count();)
    {
        CCandidateListItem *pLI = pCandidateList->GetAt(index);
        
		if(pLI->_ItemString.GetLength() > _candidateWndWidth - TRAILING_SPACE )
		{
			_candidateWndWidth = (UINT) pLI->_ItemString.GetLength() + TRAILING_SPACE;
		}
		
	    index++;
    }

    searchText.Clear();
    delete [] pwch;
}

//+---------------------------------------------------------------------------
//
// IsSymbol
//
//----------------------------------------------------------------------------
BOOL CCompositionProcessorEngine::IsSymbol()
{
	if(_keystrokeBuffer.Get() == nullptr) return FALSE;
	if(Global::imeMode==IME_MODE_DAYI)
		return (_keystrokeBuffer.GetLength()<3 && *_keystrokeBuffer.Get()==L'=');	
	else if(Global::imeMode==IME_MODE_ARRAY)
		return (_keystrokeBuffer.GetLength()<3 && towupper(*_keystrokeBuffer.Get())==L'W');	
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
	if(_keystrokeBuffer.Get() == nullptr) return FALSE;
	if((_keystrokeBuffer.GetLength() == 1) && (*_keystrokeBuffer.Get() == L'=') && Global::imeMode==IME_MODE_DAYI) 
	{
		for (UINT i = 0; i < wcslen(Global::DayiSymbolCharTable); i++)
		{
			if (Global::DayiSymbolCharTable[i] == wch)
			{
				return TRUE;
			}
		}

	}
	if((_keystrokeBuffer.GetLength() == 1) && (towupper(*_keystrokeBuffer.Get()) == L'W') && Global::imeMode==IME_MODE_ARRAY) 
	{
		for (int i = 0; i < 10; i++)
		{
			if(wch == ('0' + i))
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
	if(Global::imeMode != IME_MODE_DAYI) return FALSE;
	if(_keystrokeBuffer.Get() == nullptr || (_keystrokeBuffer.Get() && (_keystrokeBuffer.GetLength() == 0))) 
	{
		for (int i = 0; i < ARRAYSIZE(Global::dayiAddressCharTable); i++)
		{
			if (Global::dayiAddressCharTable[i]._Code == wch)
			{
				return TRUE;
			}
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
	if(Global::imeMode == IME_MODE_ARRAY && _keystrokeBuffer.GetLength() <3) return TRUE;
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
	*ppwchSpecialCodeResultString = nullptr;

	if(Global::imeMode!= IME_MODE_ARRAY || _keystrokeBuffer.GetLength() !=2 ) return 0;
	
	CTSFTTSArray<CCandidateListItem> candidateList;

	if(_pArraySpecialCodeTableDictionaryEngine == nullptr)
	{
		_pTableDictionaryEngine[Global::imeMode]->SetSearchSection(SEARCH_SECTION_TEXT);
		_pTableDictionaryEngine[Global::imeMode]->CollectWord(&_keystrokeBuffer, &candidateList);
	}
	else
	{
		_pArraySpecialCodeTableDictionaryEngine->CollectWord(&_keystrokeBuffer, &candidateList);
	}

	if(candidateList.Count() == 1)
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

	if(Global::imeMode!= IME_MODE_ARRAY || _pArraySpecialCodeTableDictionaryEngine == nullptr || inword == nullptr ) return FALSE; 

	CTSFTTSArray<CCandidateListItem> candidateList;
	_pArraySpecialCodeTableDictionaryEngine->CollectWordFromConvertedString(inword, &candidateList);
	if(candidateList.Count() == 1)
	{

		PWCHAR pwch;
		pwch = new (std::nothrow) WCHAR[MAX_READINGSTRING];
		*pwch=L'\0';
		if(_pTableDictionaryEngine[Global::imeMode]->GetRadicalMap() &&
			candidateList.GetAt(0)->_FindKeyCode.GetLength() && _pTableDictionaryEngine[Global::imeMode]->GetRadicalMap()->size())
		{
			for(UINT i=0; i <candidateList.GetAt(0)->_FindKeyCode.GetLength(); i++)
			{ // query keyname from keymap
				_T_RacialMap::iterator item = 
					_pTableDictionaryEngine[Global::imeMode]->GetRadicalMap()->find(towupper(*(_keystrokeBuffer.Get() + i)));
				if(item != _pTableDictionaryEngine[Global::imeMode]->GetRadicalMap()->end() )
				{
					assert(wcslen(pwch) + wcslen(item->second) < MAX_READINGSTRING -1);
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
	if(guidLanguageProfile == Global::TSFDayiGuidProfile)
	{
		debugPrint(L"CCompositionProcessorEngine::GetImeModeFromGuidProfile() : DAYI Mode");
		imeMode = IME_MODE_DAYI;
	}
	else if(guidLanguageProfile == Global::TSFArrayGuidProfile)
	{
		debugPrint(L"CCompositionProcessorEngine::GetImeModeFromGuidProfile() : Array Mode");
		imeMode = IME_MODE_ARRAY;
	}
	else if(guidLanguageProfile == Global::TSFPhoneticGuidProfile)
	{
		debugPrint(L"CCompositionProcessorEngine::GetImeModeFromGuidProfile() : Phonetic Mode");
		imeMode = IME_MODE_PHONETIC;
	}
	else if(guidLanguageProfile == Global::TSFGenericGuidProfile)
	{
		debugPrint(L"CCompositionProcessorEngine::GetImeModeFromGuidProfile() : Generic Mode");
		imeMode = IME_MODE_GENERIC;
	}
	
	return imeMode;

}

HRESULT CCompositionProcessorEngine::GetReverConversionResults(IME_MODE imeMode, _In_ LPCWSTR lpstrToConvert, _Inout_ CTSFTTSArray<CCandidateListItem> *pCandidateList)
{
	debugPrint(L"CCompositionProcessorEngine::GetReverConversionResults() \n");
	

	if(_pTableDictionaryEngine[imeMode] == nullptr)
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

	if( _pTableDictionaryEngine[imeMode] == nullptr ||_pTableDictionaryEngine[imeMode]->GetRadicalMap() == nullptr ||
		_pTableDictionaryEngine[imeMode]->GetRadicalMap()->size() == 0 || _pTableDictionaryEngine[imeMode]->GetRadicalMap()->size() > MAX_RADICAL) return;

	_KeystrokeComposition.Clear();

	if(imeMode == IME_MODE_DAYI && _pTableDictionaryEngine[imeMode]->GetRadicalMap() &&
		(_pTableDictionaryEngine[imeMode]->GetRadicalMap()->find('=') == _pTableDictionaryEngine[imeMode]->GetRadicalMap()->end() ))
	{ //dayi symbol prompt
		WCHAR *pwchEqual = new (std::nothrow) WCHAR[2];
		pwchEqual[0] = L'=';
		pwchEqual[1] = L'\0';
		(*_pTableDictionaryEngine[imeMode]->GetRadicalMap())['='] = pwchEqual;
	}

	for(_T_RacialMap::iterator item = _pTableDictionaryEngine[imeMode]->GetRadicalMap()->begin(); item != 
		_pTableDictionaryEngine[imeMode]->GetRadicalMap()->end(); ++item) 
	{
		_KEYSTROKE* pKS = nullptr;
        pKS = _KeystrokeComposition.Append();
        if (!pKS)
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

	if( (printable >= '0' && printable <='9') || (printable >= 'A' && printable <= 'Z') )
	{
		*vKey = printable;
	}
	else if( printable == '!')
	{
		*vKey = '1';
		*modifier = TF_MOD_SHIFT;
	}
	else if( printable == '@')
	{
		*vKey = '2';
		*modifier = TF_MOD_SHIFT;
	}
	else if( printable == '#')
	{
		*vKey = '3';
		*modifier = TF_MOD_SHIFT;
	}
	else if( printable == '$')
	{
		*vKey = '4';
		*modifier = TF_MOD_SHIFT;
	}
	else if( printable == '%')
	{
		*vKey = '5';
		*modifier = TF_MOD_SHIFT;
	}
	else if( printable == '^')
	{
		*vKey = '6';
		*modifier = TF_MOD_SHIFT;
	}
	else if( printable == '&')
	{
		*vKey = '7';
		*modifier = TF_MOD_SHIFT;
	}
	else if( printable == '*')
	{
		*vKey = '8';
		*modifier = TF_MOD_SHIFT;
	}
	else if( printable == '(')
	{
		*vKey = '9';
		*modifier = TF_MOD_SHIFT;
	}
	else if( printable == ')')
	{
		*vKey = '0';
		*modifier = TF_MOD_SHIFT;
	}
	else if( printable == ',')
		*vKey = VK_OEM_COMMA;
	else if( printable == '<')
	{
		*vKey = VK_OEM_COMMA;
		*modifier = TF_MOD_SHIFT;
	}
	else if( printable == '.')
		*vKey = VK_OEM_PERIOD;
	else if( printable == '>')
	{
		*vKey = VK_OEM_PERIOD;
		*modifier = TF_MOD_SHIFT;
	}
	else if( printable == ';')
		*vKey = VK_OEM_1;
	else if( printable == ':')
	{
		*vKey = VK_OEM_1;
		*modifier = TF_MOD_SHIFT;
	}
	else if( printable == '/')
		*vKey = VK_OEM_2;
	else if( printable == '?')
	{
		*vKey = VK_OEM_2;
		*modifier = TF_MOD_SHIFT;
	}
	else if( printable == '`')
		*vKey = VK_OEM_3;
	else if( printable == '~')
	{
		*vKey = VK_OEM_3;
		*modifier = TF_MOD_SHIFT;
	}
	else if( printable == '[')
		*vKey = VK_OEM_4;
	else if( printable == '{')
	{
		*vKey = VK_OEM_4;
		*modifier = TF_MOD_SHIFT;
	}
	else if( printable == '\\')
		*vKey = VK_OEM_5;
	else if( printable == '|')
	{
		*vKey = VK_OEM_5;
		*modifier = TF_MOD_SHIFT;
	}
	else if( printable == ']')
		*vKey = VK_OEM_6;
	else if( printable == '}')
	{
		*vKey = VK_OEM_6;
		*modifier = TF_MOD_SHIFT;
	}
	else if( printable == '\'')
		*vKey = VK_OEM_7;
	else if( printable == '"')
	{
		*vKey = VK_OEM_7;
		*modifier = TF_MOD_SHIFT;
	}
	else if( printable == '-')
		*vKey = VK_OEM_MINUS;
	else if( printable == '_')
	{
		*vKey = VK_OEM_MINUS;
		*modifier = TF_MOD_SHIFT;
	}
	else if( printable == '=')
		*vKey = VK_OEM_PLUS;
	else if( printable == '+')
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
    SetPreservedKey(Global::TSFTTSGuidImeModePreserveKey, preservedKeyImeMode, Global::ImeModeDescription, &_PreservedKey_IMEMode);

    TF_PRESERVEDKEY preservedKeyDoubleSingleByte;
    preservedKeyDoubleSingleByte.uVKey = VK_SPACE;
    preservedKeyDoubleSingleByte.uModifiers = TF_MOD_SHIFT;
    SetPreservedKey(Global::TSFTTSGuidDoubleSingleBytePreserveKey, preservedKeyDoubleSingleByte, Global::DoubleSingleByteDescription, &_PreservedKey_DoubleSingleByte);
	
	TF_PRESERVEDKEY preservedKeyConfig;
    preservedKeyConfig.uVKey = VK_OEM_5; // '\\'
    preservedKeyConfig.uModifiers = TF_MOD_CONTROL;
    SetPreservedKey(Global::TSFTTSGuidConfigPreserveKey, preservedKeyConfig, L"Show Config Pages", &_PreservedKey_Config);


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
	if(pThreadMgr == nullptr) return FALSE;
    ITfKeystrokeMgr *pKeystrokeMgr = nullptr;

    if (IsEqualGUID(pXPreservedKey->Guid, GUID_NULL))
    {
        return FALSE;
    }

    if (pThreadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void **)&pKeystrokeMgr) != S_OK)
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

BOOL CCompositionProcessorEngine::CheckShiftKeyOnly(_In_ CTSFTTSArray<TF_PRESERVEDKEY> *pTSFPreservedKeyTable)
{
    for (UINT i = 0; i < pTSFPreservedKeyTable->Count(); i++)
    {
        TF_PRESERVEDKEY *ptfPskey = pTSFPreservedKeyTable->GetAt(i);

        if (((ptfPskey->uModifiers & (_TF_MOD_ON_KEYUP_SHIFT_ONLY & 0xffff0000)) && !Global::IsShiftKeyDownOnly) ||
            ((ptfPskey->uModifiers & (_TF_MOD_ON_KEYUP_CONTROL_ONLY & 0xffff0000)) && !Global::IsControlKeyDownOnly) ||
            ((ptfPskey->uModifiers & (_TF_MOD_ON_KEYUP_ALT_ONLY & 0xffff0000)) && !Global::IsAltKeyDownOnly)         )
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
		
        
		if(Global::isWindows8){
			isOpen = FALSE;
			CCompartment CompartmentKeyboardOpen(pThreadMgr, tfClientId, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
			CCompartment CompartmentIMEMode(pThreadMgr, tfClientId, Global::TSFTTSGuidCompartmentIMEMode);
			CompartmentKeyboardOpen._GetCompartmentBOOL(isOpen);
			CompartmentKeyboardOpen._SetCompartmentBOOL(isOpen ? FALSE : TRUE);
		}
		else
		{
			CCompartment CompartmentIMEMode(pThreadMgr, tfClientId, Global::TSFTTSGuidCompartmentIMEMode);
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
        CCompartment CompartmentDoubleSingleByte(pThreadMgr, tfClientId, Global::TSFTTSGuidCompartmentDoubleSingleByte);
        CompartmentDoubleSingleByte._GetCompartmentBOOL(isDouble);
        CompartmentDoubleSingleByte._SetCompartmentBOOL(isDouble ? FALSE : TRUE);
        *pIsEaten = TRUE;
    }
	else if (IsEqualGUID(rguid, _PreservedKey_Config.Guid))
	{
		// call config dialog
		_pTextService->Show(NULL, 0,  GUID_NULL);
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

void CCompositionProcessorEngine::SetupConfiguration()
{
    _isWildcard = TRUE;
    _isDisableWildcardAtFirst = TRUE;
    _isKeystrokeSort = FALSE;

	if(Global::imeMode == IME_MODE_DAYI)
	{
		CConfig::SetThreeCodeMode(TRUE);
	}
	else if(Global::imeMode == IME_MODE_ARRAY)
	{
		CConfig::SetSpaceAsPageDown(TRUE);
	}
	else if(Global::imeMode == IME_MODE_PHONETIC)
	{
		CConfig::SetSpaceAsPageDown(TRUE);
	}

    SetInitialCandidateListRange();


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
		
	if(GetEnvironmentVariable(L"ProgramW6432", wszProgramFiles, MAX_PATH) ==0)
	{//on 64-bit vista only 32bit app has this enviroment variable.  Which means the call failed when the apps running is 64-bit.
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
	if(imeMode != IME_MODE_ARRAY)
		StringCchPrintf(pwszTTSFileName, MAX_PATH, L"%s%s", wszProgramFiles, L"\\Windows NT\\TableTextService\\TableTextServiceDaYi.txt");
	else 
		StringCchPrintf(pwszTTSFileName, MAX_PATH, L"%s%s", wszProgramFiles, L"\\Windows NT\\TableTextService\\TableTextServiceArray.txt");
	
	StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", wszAppData, L"\\TSFTTS");

	if(!PathFileExists(pwszCINFileName))
	{
	//TSFTTS roadming profile is not exist. Create one.
		CreateDirectory(pwszCINFileName, NULL);	
	}
	// load main table file now
	if(imeMode == IME_MODE_DAYI) //dayi.cin in personal romaing profile
	{
		StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", wszAppData, L"\\TSFTTS\\Dayi.cin");
		if(PathFileExists(pwszCINFileName) &&
			_pTableDictionaryFile[imeMode] &&
			CompareString(_pTextService->GetLocale(), NORM_IGNORECASE, pwszCINFileName, -1, _pTableDictionaryFile[imeMode]->GetFileName(), -1) != CSTR_EQUAL)
		{ //indicate the prevoius table is built with system preload file in program files, and now user provides their own.
			delete _pTableDictionaryEngine[imeMode];
			_pTableDictionaryEngine[imeMode] = nullptr;
			delete _pTableDictionaryFile[imeMode];
			_pTableDictionaryFile[imeMode] = nullptr;
		}
	}
	if(imeMode == IME_MODE_ARRAY) //array.cin in personal romaing profile
	{
		StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", wszAppData, L"\\TSFTTS\\Array.cin");
		if(PathFileExists(pwszCINFileName) &&
			_pTableDictionaryFile[imeMode] &&
			CompareString(_pTextService->GetLocale(), NORM_IGNORECASE, pwszCINFileName, -1, _pTableDictionaryFile[imeMode]->GetFileName(), -1) != CSTR_EQUAL)
		{ //indicate the prevoius table is built with system preload file in program files, and now user provides their own.
			delete _pTableDictionaryEngine[imeMode];
			_pTableDictionaryEngine[imeMode] = nullptr;
			delete _pTableDictionaryFile[imeMode];
			_pTableDictionaryFile[imeMode] = nullptr;
		}
	}
	if(imeMode == IME_MODE_PHONETIC) //phone.cin in personal romaing profile
	{
		StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", wszAppData, L"\\TSFTTS\\Phone.cin");
		if(!PathFileExists(pwszCINFileName)) //failed back to pre-install Phone.cin in program files.
			StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", wszProgramFiles, L"\\TSFTTS\\Phone.cin");
		else if( _pTableDictionaryFile[imeMode] &&
			CompareString(_pTextService->GetLocale(), NORM_IGNORECASE, pwszCINFileName, -1, _pTableDictionaryFile[imeMode]->GetFileName(), -1) != CSTR_EQUAL)
		{ //indicate the prevoius table is built with system preload file in program files, and now user provides their own.
			delete _pTableDictionaryEngine[imeMode];
			_pTableDictionaryEngine[imeMode] = nullptr;
			delete _pTableDictionaryFile[imeMode];
			_pTableDictionaryFile[imeMode] = nullptr;
		}
	}
	if(imeMode == IME_MODE_GENERIC) //phone.cin in personal romaing profile
	{
		StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", wszAppData, L"\\TSFTTS\\Generic.cin");
		// we don't provide preload Generic.cin in program files
	}

	if(PathFileExists(pwszCINFileName))  //create cin CFileMapping object
	{
		//create CFile object
		if (_pTableDictionaryFile[imeMode] == nullptr)
		{
			_pTableDictionaryFile[imeMode] = new (std::nothrow) CFile();
			if ((_pTableDictionaryFile[imeMode])->CreateFile(pwszCINFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))	
			{
				_pTableDictionaryEngine[imeMode] = //cin files use tab as delimiter
					new (std::nothrow) CTableDictionaryEngine(_pTextService->GetLocale(), _pTableDictionaryFile[imeMode], CIN_DICTIONARY); 
				_pTableDictionaryEngine[imeMode]->ParseConfig(imeMode); //parse config first.		

			}
		}
	}
	else if(imeMode == IME_MODE_DAYI || imeMode == IME_MODE_ARRAY)		//failed back to load windows preload tabletextservice table.
	{
		if (_pTableDictionaryEngine[imeMode] == nullptr)
		{
			_pTableDictionaryFile[imeMode] = new (std::nothrow) CFile();
			if (!_pTableDictionaryFile[imeMode])  goto ErrorExit;

			if (!(_pTableDictionaryFile[imeMode])->CreateFile(pwszTTSFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))	
			{
				goto ErrorExit;
			}
			else
			{
				_pTableDictionaryEngine[imeMode] = //TTS file use '=' as delimiter
					new (std::nothrow) CTableDictionaryEngine(_pTextService->GetLocale(), _pTableDictionaryFile[imeMode], TTS_DICTIONARY); 
				if (!_pTableDictionaryEngine[imeMode])  goto ErrorExit;

				_pTableDictionaryEngine[imeMode]->ParseConfig(imeMode); //parse config first.

			}
		}
	}

	// now load phrase table
	*pwszCINFileName = L'\0';
	StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", wszAppData, L"\\TSFTTS\\Phrase.cin");
	if(PathFileExists(pwszCINFileName) &&	_pPhraseDictionaryFile == nullptr)
	{ //indicate the prevoius table is built with system preload tts file in program files, and now user provides their own.
		_pPhraseTableDictionaryEngine = nullptr;
	}	
	//create CFile object
	if (_pPhraseTableDictionaryEngine == nullptr)
	{
		if(PathFileExists(pwszCINFileName))
		{
			_pPhraseDictionaryFile = new (std::nothrow) CFile();
			if (_pPhraseDictionaryFile->CreateFile(pwszCINFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))	
			{
				_pPhraseTableDictionaryEngine = 
					new (std::nothrow) CTableDictionaryEngine(_pTextService->GetLocale(), _pPhraseDictionaryFile, CIN_DICTIONARY); //cin files use tab as delimiter
				_pPhraseTableDictionaryEngine->ParseConfig(imeMode); //parse config first.				
				
			}
		}
		else if((imeMode == IME_MODE_DAYI || imeMode == IME_MODE_ARRAY) && _pTableDictionaryFile[imeMode]  &&
			_pTableDictionaryEngine[imeMode]-> GetDictionaryType() == TTS_DICTIONARY)
		{
			_pPhraseTableDictionaryEngine = _pTableDictionaryEngine[imeMode];
		}
		else
		{
			_pPhraseDictionaryFile = new (std::nothrow) CFile();
			if (!_pPhraseDictionaryFile)  goto ErrorExit;

			if (!_pPhraseDictionaryFile->CreateFile(pwszTTSFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))	
			{
				goto ErrorExit;
			}
			else // no user provided phrase table present and we are not in ARRAY or DAYI, thus we load TTS DAYI table to provide phrase table
			{
				_pPhraseTableDictionaryEngine = 
					new (std::nothrow) CTableDictionaryEngine(_pTextService->GetLocale(), _pPhraseDictionaryFile, TTS_DICTIONARY); //TTS file use '=' as delimiter
				if (!_pPhraseTableDictionaryEngine)  goto ErrorExit;

				_pPhraseTableDictionaryEngine->ParseConfig(imeMode); //parse config first.

			}
		}
	}

	// now load array special code and short-code table
	if(imeMode == IME_MODE_ARRAY) //array-special.cin and array-shortcode.cin in personal romaing profile
	{

		StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", wszAppData, L"\\TSFTTS\\Array-special.cin");
		if(!PathFileExists(pwszCINFileName)) //failed back to preload array-special.cin in program files.
			StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", wszProgramFiles, L"\\TSFTTS\\Array-special.cin");
		else if( _pArraySpecialCodeDictionaryFile &&
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
			if (_pArraySpecialCodeDictionaryFile->CreateFile(pwszCINFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))	
			{
				_pArraySpecialCodeTableDictionaryEngine = 
					new (std::nothrow) CTableDictionaryEngine(_pTextService->GetLocale(), _pArraySpecialCodeDictionaryFile, CIN_DICTIONARY); //cin files use tab as delimiter
				if(_pArraySpecialCodeTableDictionaryEngine) 
					_pArraySpecialCodeTableDictionaryEngine->ParseConfig(imeMode); // to release the file handle
			}
		}

		StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", wszAppData, L"\\TSFTTS\\Array-shortcode.cin");
		if(PathFileExists(pwszCINFileName) && _pArrayShortCodeDictionaryFile == nullptr)
		{
			_pArrayShortCodeDictionaryFile = new (std::nothrow) CFile();
			if (_pArrayShortCodeDictionaryFile->CreateFile(pwszCINFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))	
			{
				_pArrayShortCodeTableDictionaryEngine = 
					new (std::nothrow) CTableDictionaryEngine(_pTextService->GetLocale(), _pArrayShortCodeDictionaryFile, CIN_DICTIONARY); //cin files use tab as delimiter
				if(_pArrayShortCodeTableDictionaryEngine)
					_pArrayShortCodeTableDictionaryEngine->ParseConfig(imeMode);// to release the file handle
			}

		}

	}
	
  
    delete []pwszTTSFileName;
	delete []pwszCINFileName;
    return TRUE;
ErrorExit:
    if (pwszTTSFileName)  delete []pwszTTSFileName;
    if (pwszCINFileName)  delete []pwszCINFileName;
    return FALSE;
}

BOOL CCompositionProcessorEngine::SetupHanCovertTable()
{	
	debugPrint(L"CCompositionProcessorEngine::SetupHanCovertTable() \n");
	if(CConfig::GetDoHanConvert() &&  _pTCSCTableDictionaryEngine == nullptr)
	{
		WCHAR wszProgramFiles[MAX_PATH];

		if(GetEnvironmentVariable(L"ProgramW6432", wszProgramFiles, MAX_PATH) ==0)
		{//on 64-bit vista only 32bit app has this enviroment variable.  Which means the call failed when the apps running is 64-bit.
			//on 32-bit windows, this will definitely failed.  Get ProgramFiles enviroment variable now will retrive the correct program files path.
			GetEnvironmentVariable(L"ProgramFiles", wszProgramFiles, MAX_PATH); 
		}


		debugPrint(L"CCompositionProcessorEngine::SetupDictionaryFile() :wszProgramFiles = %s", wszProgramFiles);

		WCHAR *pwszCINFileName = new (std::nothrow) WCHAR[MAX_PATH];
		if (!pwszCINFileName)  goto ErrorExit;
		*pwszCINFileName = L'\0';

		StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", wszProgramFiles, L"\\TSFTTS\\TCSC.cin");
		if (_pTCSCTableDictionaryFile == nullptr)
		{
			_pTCSCTableDictionaryFile = new (std::nothrow) CFile();
			if ((_pTCSCTableDictionaryFile)->CreateFile(pwszCINFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))	
			{
				_pTCSCTableDictionaryEngine = 
					new (std::nothrow) CTableDictionaryEngine(_pTextService->GetLocale(), _pTCSCTableDictionaryFile, CIN_DICTIONARY); //cin files use tab as delimiter
			}
		}


		delete []pwszCINFileName;
		return TRUE;
ErrorExit:
		if (pwszCINFileName)  delete []pwszCINFileName;
	}
    return FALSE;
}
BOOL CCompositionProcessorEngine::GetTCFromSC(CStringRange* stringToConvert, CStringRange* convertedString)
{
	stringToConvert;convertedString;
	//not yet implemented
	return FALSE;
}
BOOL CCompositionProcessorEngine::GetSCFromTC(CStringRange* stringToConvert, CStringRange* convertedString)
{
	debugPrint(L"CCompositionProcessorEngine::GetSCFromTC()");
	if(!CConfig::GetDoHanConvert()) return FALSE;

	if(_pTCSCTableDictionaryEngine == nullptr) SetupHanCovertTable();

	UINT lenToConvert = (UINT) stringToConvert->GetLength();
	PWCHAR pwch = new (std::nothrow) WCHAR[lenToConvert +1];
	*pwch = L'\0';
	CStringRange wcharToCover;

	for (UINT i = 0;i <lenToConvert; i++)
	{
		CTSFTTSArray<CCandidateListItem> candidateList;
		_pTCSCTableDictionaryEngine->CollectWord(&wcharToCover.Set(stringToConvert->Get() + i, 1), &candidateList);
		if(candidateList.Count() == 1)
			StringCchCatN(pwch, lenToConvert +1, candidateList.GetAt(0)->_ItemString.Get(),1); 
		else
			StringCchCatN(pwch, lenToConvert +1, wcharToCover.Get(),1); 
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

    if (FAILED(pThreadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void **)&pKeystrokeMgr)))
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
    if (SUCCEEDED(hr))
    {
        UninitPreservedKey(pThreadMgr);
        pThreadMgr->Release();
        pThreadMgr = nullptr;
    }

    if (Description)
    {
        delete [] Description;
    }
}



void CCompositionProcessorEngine::SetInitialCandidateListRange()
{
	_candidateListIndexRange.Clear();
    for (DWORD i = 1; i <= 10; i++)
    {
        DWORD* pNewIndexRange = nullptr;

        pNewIndexRange = _candidateListIndexRange.Append();
        if (pNewIndexRange != nullptr)
        {
            if (i != 10)
            {
                *pNewIndexRange = i;
            }
            else
            {
                *pNewIndexRange = 0;
            }
        }
    }
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

BOOL CCompositionProcessorEngine::IsVirtualKeyNeed(UINT uCode, _In_reads_(1) WCHAR *pwch, BOOL fComposing, CANDIDATE_MODE candidateMode, BOOL hasCandidateWithWildcard, _Out_opt_ _KEYSTROKE_STATE *pKeyState)
{
    if (pKeyState)
    {
        pKeyState->Category = CATEGORY_NONE;
        pKeyState->Function = FUNCTION_NONE;
    }

    if (candidateMode == CANDIDATE_ORIGINAL || candidateMode == CANDIDATE_PHRASE || candidateMode == CANDIDATE_WITH_NEXT_COMPOSITION)
    {        fComposing = FALSE;
    }

    if (fComposing || candidateMode == CANDIDATE_INCREMENTAL || candidateMode == CANDIDATE_NONE)
    {
        if (IsVirtualKeyKeystrokeComposition(uCode, pKeyState, FUNCTION_NONE))
        {
            return TRUE;
        }
        else if ((IsWildcard() && IsWildcardChar(*pwch) && !IsDisableWildcardAtFirst()) ||
            (IsWildcard() && IsWildcardChar(*pwch) &&  IsDisableWildcardAtFirst() && _keystrokeBuffer.GetLength()))
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

            case VK_RETURN: if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_CONVERT;} return TRUE;
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
        case VK_RETURN: 
        case VK_SPACE:  if (pKeyState) 
						{ 
							if ( (candidateMode == CANDIDATE_WITH_NEXT_COMPOSITION)){ // space finalized the associate here instead of choose the first one (selected index = -1 for phrase candidates).
								if (pKeyState)
								{
									pKeyState->Category = CATEGORY_CANDIDATE;
									pKeyState->Function = FUNCTION_FINALIZE_CANDIDATELIST;
								}
							}else if(uCode == VK_SPACE && CConfig::GetSpaceAsPageDown()){
								pKeyState->Category = CATEGORY_CANDIDATE; 
								pKeyState->Function = FUNCTION_MOVE_PAGE_DOWN; 
							}else{
								pKeyState->Category = CATEGORY_CANDIDATE; 
								pKeyState->Function = FUNCTION_CONVERT; 
							}

							return TRUE;
						}
        case VK_BACK:   if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_CANCEL; } return TRUE;

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

BOOL CCompositionProcessorEngine::IsVirtualKeyKeystrokeCandidate(UINT uCode, _In_ _KEYSTROKE_STATE *pKeyState, CANDIDATE_MODE candidateMode, _Out_ BOOL *pfRetCode, _In_ CTSFTTSArray<_KEYSTROKE> *pKeystrokeMetric)
{
	candidateMode;
    if (pfRetCode == nullptr)
    {
        return FALSE;
    }
    *pfRetCode = FALSE;

    for (UINT i = 0; i < pKeystrokeMetric->Count(); i++)
    {
        _KEYSTROKE *pKeystroke = nullptr;

        pKeystroke = pKeystrokeMetric->GetAt(i);

        if ((pKeystroke->VirtualKey == uCode) && Global::CheckModifiers(Global::ModifiersValue, pKeystroke->Modifiers))
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

    if (_candidateListIndexRange.IsRange(uCode, candidateMode))
    {
        if (candidateMode == CANDIDATE_PHRASE)
        {
            // Candidate phrase could specify *modifier
             if ((GetCandidateListPhraseModifier() == 0 && (Global::ModifiersValue & (TF_MOD_LSHIFT | TF_MOD_SHIFT) )!= 0) || //shift + 123...
                (GetCandidateListPhraseModifier() != 0 && Global::CheckModifiers(Global::ModifiersValue, GetCandidateListPhraseModifier())))
            {
				pKeyState->Category = CATEGORY_CANDIDATE;
				pKeyState->Function = FUNCTION_SELECT_BY_NUMBER;
                return TRUE;
            }
            else
            {
                pKeyState->Category = CATEGORY_INVOKE_COMPOSITION_EDIT_SESSION; pKeyState->Function = FUNCTION_FINALIZE_TEXTSTORE_AND_INPUT;
                return FALSE;
            }
        }
        else if (candidateMode == CANDIDATE_WITH_NEXT_COMPOSITION)
        {
            // Candidate phrase could specify *modifier
            if ((GetCandidateListPhraseModifier() == 0 && (Global::ModifiersValue & (TF_MOD_LSHIFT | TF_MOD_SHIFT) )!= 0) || //shift + 123...
                (GetCandidateListPhraseModifier() != 0 && Global::CheckModifiers(Global::ModifiersValue, GetCandidateListPhraseModifier())))
            {
                pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_SELECT_BY_NUMBER;
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

//+---------------------------------------------------------------------------
//
// CCompositionProcessorEngine::doBeep
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::DoBeep()
{
	if(CConfig::GetDoBeep())
		MessageBeep(MB_ICONASTERISK);
}


void CCompositionProcessorEngine::UpdateDictionaryFile()
{
	CFile* pCurrentDictioanryFile = _pTableDictionaryFile[Global::imeMode];

	SetupDictionaryFile(_imeMode);
	if(pCurrentDictioanryFile != _pTableDictionaryFile[Global::imeMode])
	{
		SetupKeystroke(Global::imeMode);
		SetupConfiguration();
	}

	if(_pTableDictionaryFile[Global::imeMode] && _pTableDictionaryEngine[Global::imeMode] &&
		_pTableDictionaryEngine[Global::imeMode]->GetDictionaryType() == TTS_DICTIONARY &&
		_pTableDictionaryFile[Global::imeMode]->IsFileUpdated())
	{
		if(_pTableDictionaryEngine[Global::imeMode])
		{   // the table is loaded from .cin and the cin was updated.
			_pTableDictionaryEngine[Global::imeMode]->ParseConfig(Global::imeMode);
			SetupKeystroke(Global::imeMode);
			SetupConfiguration();
		}
	}
}