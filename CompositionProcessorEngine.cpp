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
	_pTextService = pTextService;
    _pTextService->AddRef();

    
	for (UINT i =0 ; i< IM_SLOTS ; i++)
	{
		_pTableDictionaryEngine[i] = nullptr;
		_pTTSTableDictionaryEngine[i] = nullptr;
		_pCINTableDictionaryEngine[i] = nullptr;
		_pTTSDictionaryFile[i] = nullptr;
		_pCINDictionaryFile[i] = nullptr;
	}


	_pArrayShortCodeTableDictionaryEngine = nullptr;
	_pArraySpecialCodeTableDictionaryEngine = nullptr;
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
	_pTextService->Release();
	for (UINT i =0 ; i < IM_SLOTS ; i++)
	{
		if (_pTTSTableDictionaryEngine[i])
		{
			delete _pTTSTableDictionaryEngine[i];
			_pTTSTableDictionaryEngine[i] = nullptr;
		}
		if (_pCINTableDictionaryEngine[i])
		{
			delete _pCINTableDictionaryEngine[i];
			_pCINTableDictionaryEngine[i] = nullptr;
		}
		if (_pTTSDictionaryFile[i])
		{
			delete _pTTSDictionaryFile[i];
			_pTTSDictionaryFile[i] = nullptr;
		}
		if (_pCINDictionaryFile[i])
		{
			delete _pTTSDictionaryFile[i];
			_pCINDictionaryFile[i] = nullptr;
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
		pwchRadical = new (std::nothrow) WCHAR[_keystrokeBuffer.GetLength() + 1];
		*pwchRadical = L'\0';
		
        for (DWORD index = 0; index < _keystrokeBuffer.GetLength(); index++)
        {
			if(Global::radicalMap[Global::imeMode].size() && !IsSymbol()) // if radicalMap is valid (size()>0), then convert the keystroke buffer 
			{
				map<WCHAR, WCHAR>::iterator item = 
					Global::radicalMap[Global::imeMode].find(towupper(*(_keystrokeBuffer.Get() + index)));
				if(item != Global::radicalMap[Global::imeMode].end() )
					StringCchCatN(pwchRadical, _keystrokeBuffer.GetLength() + 1, &item->second,1); 
			}

            oneKeystroke.Set(_keystrokeBuffer.Get() + index, 1);

            if (IsWildcard() && IsWildcardChar(*oneKeystroke.Get()))
            {
                _hasWildcardIncludedInKeystrokeBuffer = TRUE;
            }
        }
		if(Global::radicalMap[Global::imeMode].size()&& !IsSymbol())
		{
			pNewString->Set(pwchRadical, _keystrokeBuffer.GetLength());
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

	if(_pCINTableDictionaryEngine[Global::imeMode] && Global::hasCINPhraseSection) // do phrase lookup if CIN file has phrase section
		_pCINTableDictionaryEngine[Global::imeMode]->CollectWordFromConvertedString(&searchText, pCandidateList);
	else if(_pTTSTableDictionaryEngine[Global::imeMode] && Global::hasPhraseSection)// do phrase lookup in TTS file if CIN phrase section is not present
	{   _pTTSTableDictionaryEngine[Global::imeMode]->SetSearchSection(SEARCH_SECTION_PHRASE);
		_pTTSTableDictionaryEngine[Global::imeMode]->CollectWordFromConvertedString(&searchText, pCandidateList);
	}
	else // no phrase section, do wildcard text search
		_pTableDictionaryEngine[Global::imeMode]->CollectWordFromConvertedStringForWildcard(&searchText, pCandidateList);

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
		for (int i = 0; i < ARRAYSIZE(Global::symbolCharTable); i++)
		{
			if (Global::symbolCharTable[i] == wch)
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
		pwch = new (std::nothrow) WCHAR[candidateList.GetAt(0)->_FindKeyCode.GetLength()+1];
		*pwch=L'\0';
		if(candidateList.GetAt(0)->_FindKeyCode.GetLength() && Global::radicalMap[Global::imeMode].size())
		{
			for(UINT i=0; i <candidateList.GetAt(0)->_FindKeyCode.GetLength(); i++)
			{ // query keyname from keymap
				map<WCHAR, WCHAR>::iterator item = 
					Global::radicalMap[Global::imeMode].find(towupper(*(_keystrokeBuffer.Get() + i)));
				if(item != Global::radicalMap[Global::imeMode].end() )
					StringCchCatN(pwch, candidateList.GetAt(0)->_FindKeyCode.GetLength()+1, &item->second,1); 
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
		debugPrint(L"CCompositionProcessorEngine::GetImeModeFromGuidProfile() : Phonetc Mode");
		imeMode = IME_MODE_PHONETIC;
	}
	
	return imeMode;

}

HRESULT CCompositionProcessorEngine::GetReverConversionResults(REFGUID guidLanguageProfile, _In_ LPCWSTR lpstrToConvert, _Inout_ CTSFTTSArray<CCandidateListItem> *pCandidateList)
{
	debugPrint(L"CCompositionProcessorEngine::GetReverConversionResults() \n");
	IME_MODE imeMode = GetImeModeFromGuidProfile(guidLanguageProfile);

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

	if( Global::radicalMap[imeMode].size() == 0 || Global::radicalMap[imeMode].size() >50) return;

	_KeystrokeComposition.Clear();

	if((imeMode == IME_MODE_DAYI) && (Global::radicalMap[imeMode].find('=') == Global::radicalMap[imeMode].end() ))
	{ //dayi symbol prompt
		Global::radicalMap[imeMode]['='] = L'=';
	}
	for(map<WCHAR,WCHAR>::iterator item = Global::radicalMap[imeMode].begin(); item != Global::radicalMap[imeMode].end(); ++item) 
	{
		_KEYSTROKE* pKS = nullptr;
        pKS = _KeystrokeComposition.Append();
        if (!pKS)
            break;
  
		pKS->Function = FUNCTION_INPUT;
		pKS->Modifiers =0;
		WCHAR key = item->first;
		if( (key >= '0' && key <='9') || (key >= 'A' && key <= 'Z') )
			pKS->VirtualKey = key;
		else if( key == ',')
			pKS->VirtualKey = VK_OEM_COMMA;
		else if( key == '.')
			pKS->VirtualKey = VK_OEM_PERIOD;
		else if( key == '/')
			pKS->VirtualKey = VK_OEM_2;
		else if( key == ';')
			pKS->VirtualKey = VK_OEM_1;
		else if( key == '\'')
			pKS->VirtualKey = VK_OEM_7;
		else if( key == '[')
			pKS->VirtualKey = VK_OEM_4;
		else if( key == ']')
			pKS->VirtualKey = VK_OEM_6;
		else if( key == '`')
			pKS->VirtualKey = VK_OEM_3;
		else if( key == '-')
			pKS->VirtualKey = VK_OEM_MINUS;
		else if( key == '=')
			pKS->VirtualKey = VK_OEM_PLUS;
		
		pKS->Modifiers = 0;
		pKS->Function = FUNCTION_INPUT;

	}




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


    return;
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

    TF_PRESERVEDKEY *ptfPsvKey1 = pXPreservedKey->TSFPreservedKeyTable.Append();
    if (!ptfPsvKey1)
    {
        return;
    }
    *ptfPsvKey1 = tfPreservedKey;

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
        TF_PRESERVEDKEY *ptfPsvKey = pTSFPreservedKeyTable->GetAt(i);

        if (((ptfPsvKey->uModifiers & (_TF_MOD_ON_KEYUP_SHIFT_ONLY & 0xffff0000)) && !Global::IsShiftKeyDownOnly) ||
            ((ptfPsvKey->uModifiers & (_TF_MOD_ON_KEYUP_CONTROL_ONLY & 0xffff0000)) && !Global::IsControlKeyDownOnly) ||
            ((ptfPsvKey->uModifiers & (_TF_MOD_ON_KEYUP_ALT_ONLY & 0xffff0000)) && !Global::IsAltKeyDownOnly)         )
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

    WCHAR *pwszFileName = new (std::nothrow) WCHAR[MAX_PATH];
	WCHAR *pwszCINFileName = new (std::nothrow) WCHAR[MAX_PATH];
	
    if (!pwszFileName)  goto ErrorExit;
	if (!pwszCINFileName)  goto ErrorExit;

	*pwszFileName = L'\0';
	*pwszCINFileName = L'\0';

	//tableTextService (TTS) dictionary file 
	if(imeMode == IME_MODE_DAYI)
		StringCchPrintf(pwszFileName, MAX_PATH, L"%s%s", wszProgramFiles, L"\\Windows NT\\TableTextService\\TableTextServiceDaYi.txt");
	else if(imeMode == IME_MODE_ARRAY)
		StringCchPrintf(pwszFileName, MAX_PATH, L"%s%s", wszProgramFiles, L"\\Windows NT\\TableTextService\\TableTextServiceArray.txt");
	else
		StringCchPrintf(pwszFileName, MAX_PATH, L"%s%s", wszProgramFiles, L"\\Windows NT\\TableTextService\\TableTextServiceDaYi.txt"); // we need this to lookup phrase

	//create CFileMapping object
	if (_pTTSDictionaryFile[imeMode] == nullptr)
	{
		_pTTSDictionaryFile[imeMode] = new (std::nothrow) CFileMapping();
		if (!_pTTSDictionaryFile[imeMode])  goto ErrorExit;

		if (!(_pTTSDictionaryFile[imeMode])->CreateFile(pwszFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))	
		{
			goto ErrorExit;
		}
		else
		{
			_pTTSTableDictionaryEngine[imeMode] = new (std::nothrow) CTableDictionaryEngine(_pTextService->GetLocale(), _pTTSDictionaryFile[imeMode], L'='); //TTS file use '=' as delimiter
			if (!_pTTSTableDictionaryEngine[imeMode])  goto ErrorExit;
			Global::radicalMap[imeMode].clear();
			_pTTSTableDictionaryEngine[imeMode]->ParseConfig(imeMode); //parse config first.
		
		}
	}
	_pTableDictionaryEngine[imeMode] = _pTTSTableDictionaryEngine[imeMode];  //set TTS as default dictionary engine
	
	StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", wszAppData, L"\\TSFTTS");

	if(PathFileExists(pwszCINFileName))
	{
		if(imeMode == IME_MODE_DAYI) //dayi.cin in personal romaing profile
			StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", wszAppData, L"\\TSFTTS\\Dayi.cin");
		if(imeMode == IME_MODE_ARRAY) //array.cin in personal romaing profile
			StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", wszAppData, L"\\TSFTTS\\Array.cin");
		if(imeMode == IME_MODE_PHONETIC) //phone.cin in personal romaing profile
		{
			StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", wszAppData, L"\\TSFTTS\\Phone.cin");
			if(!PathFileExists(pwszCINFileName)) //failed back to pre-install array-special.cin in program files.
				StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", wszProgramFiles, L"\\TSFTTS\\Phone.cin");
		}
	
		if(PathFileExists(pwszCINFileName))  //create cin CFileMapping object
		{
			 //create CFileMapping object
			if (_pCINDictionaryFile[imeMode] == nullptr)
			{
				_pCINDictionaryFile[imeMode] = new (std::nothrow) CFileMapping();
				if ((_pCINDictionaryFile[imeMode])->CreateFile(pwszCINFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))	
				{
					_pCINTableDictionaryEngine[imeMode] = new (std::nothrow) CTableDictionaryEngine(_pTextService->GetLocale(), _pCINDictionaryFile[imeMode], L'\t'); //cin files use tab as delimiter

					if (_pCINTableDictionaryEngine[imeMode])  
					{
						Global::radicalMap[imeMode].clear();
						_pCINTableDictionaryEngine[imeMode]->ParseConfig(imeMode); //parse config first.
						
					}
				}
			}
			_pTableDictionaryEngine[imeMode] = _pCINTableDictionaryEngine[imeMode];  //set CIN as dictionary engine if avaialble
		}
		
		
	}
	else
	{
		//TSFTTS roadming profile is not exist. Create one.
		CreateDirectory(pwszCINFileName, NULL);	
	}

	if(Global::imeMode == IME_MODE_ARRAY) //array-special.cin and array-shortcode.cin in personal romaing profile
	{
		StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", wszAppData, L"\\TSFTTS\\array-special.cin");
		if(!PathFileExists(pwszCINFileName)) //failed back to pre-install array-special.cin in program files.
			StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", wszProgramFiles, L"\\TSFTTS\\array-special.cin");
		if (_pArraySpecialCodeDictionaryFile == nullptr)
		{
			_pArraySpecialCodeDictionaryFile = new (std::nothrow) CFileMapping();
			if ((_pArraySpecialCodeDictionaryFile)->CreateFile(pwszCINFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))	
			{
				_pArraySpecialCodeTableDictionaryEngine = 
					new (std::nothrow) CTableDictionaryEngine(_pTextService->GetLocale(), _pArraySpecialCodeDictionaryFile, L'\t'); //cin files use tab as delimiter
			}
		}

		StringCchPrintf(pwszCINFileName, MAX_PATH, L"%s%s", wszAppData, L"\\TSFTTS\\array-shortcode.cin");
		if(PathFileExists(pwszCINFileName))
		{
			if (_pArrayShortCodeDictionaryFile == nullptr)
			{
				_pArrayShortCodeDictionaryFile = new (std::nothrow) CFileMapping();
				if ((_pArrayShortCodeDictionaryFile)->CreateFile(pwszCINFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))	
				{
					_pArrayShortCodeTableDictionaryEngine = 
						new (std::nothrow) CTableDictionaryEngine(_pTextService->GetLocale(), _pArrayShortCodeDictionaryFile, L'\t'); //cin files use tab as delimiter
				}
			}
		}

	}
	
  
    delete []pwszFileName;
	delete []pwszCINFileName;
    return TRUE;
ErrorExit:
    if (pwszFileName)  delete []pwszFileName;
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
			_pTCSCTableDictionaryFile = new (std::nothrow) CFileMapping();
			if ((_pTCSCTableDictionaryFile)->CreateFile(pwszCINFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))	
			{
				_pTCSCTableDictionaryEngine = 
					new (std::nothrow) CTableDictionaryEngine(_pTextService->GetLocale(), _pTCSCTableDictionaryFile, L'\t'); //cin files use tab as delimiter
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
    return _pTTSDictionaryFile[Global::imeMode];
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
            // Candidate phrase could specify modifier
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
            // Candidate phrase could specify modifier
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
