//
//
// Derived from Microsoft Sample IME by Jeremy '13,7,17
//
//


#include "Private.h"
#include "DictionarySearch.h"
#include "TSFDayiBaseStructure.h"

//+---------------------------------------------------------------------------
//
// ctor
//
//----------------------------------------------------------------------------

CDictionarySearch::CDictionarySearch(LCID locale, _In_ CFile *pFile, _In_ CStringRange *pSearchKeyCode) : CDictionaryParser(locale)
{
	_pFile = pFile;
	_pSearchKeyCode = pSearchKeyCode;
	_charIndex = 0;
}

//+---------------------------------------------------------------------------
//
// dtor
//
//----------------------------------------------------------------------------

CDictionarySearch::~CDictionarySearch()
{
}

//+---------------------------------------------------------------------------
//
// FindPhrase
//
//----------------------------------------------------------------------------

BOOL CDictionarySearch::FindPhrase(_Out_ CDictionaryResult **ppdret)
{
	return FindWorker(FALSE, ppdret, FALSE); // NO WILDCARD
}

//+---------------------------------------------------------------------------
//
// FindPhraseForWildcard
//
//----------------------------------------------------------------------------

BOOL CDictionarySearch::FindPhraseForWildcard(_Out_ CDictionaryResult **ppdret)
{
	return FindWorker(FALSE, ppdret, TRUE); // Wildcard
}

//+---------------------------------------------------------------------------
//
// FindConvertedStringForWildcard
//
//----------------------------------------------------------------------------

BOOL CDictionarySearch::FindConvertedString(CDictionaryResult **ppdret)
{
	return FindWorker(TRUE, ppdret, FALSE); //NO Wildcard
}


//+---------------------------------------------------------------------------
//
// FindConvertedStringForWildcard
//
//----------------------------------------------------------------------------

BOOL CDictionarySearch::FindConvertedStringForWildcard(CDictionaryResult **ppdret)
{
	return FindWorker(TRUE, ppdret, TRUE); // Wildcard
}

BOOL CDictionarySearch::ParseConfig()
{
	return FindWorker(FALSE, NULL, FALSE, TRUE); // parseConfig=TRUE;
}

//+---------------------------------------------------------------------------
//
// FindWorker
//
//----------------------------------------------------------------------------

BOOL CDictionarySearch::FindWorker(BOOL isTextSearch, _Out_ CDictionaryResult **ppdret, BOOL isWildcardSearch, _In_opt_ BOOL parseConfig)
{
	DWORD_PTR dwTotalBufLen = GetBufferInWCharLength();        // in char
	if (dwTotalBufLen == 0)
	{
		return FALSE;
	}

	const WCHAR *pwch = GetBufferInWChar();
	DWORD_PTR indexTrace = 0;     // in char
	if(!parseConfig) *ppdret = nullptr;
	
	enum CONTROLKEY_TYPE controlKeyType = NOT_CONTROLKEY;

	BOOL isFound = FALSE;
	DWORD_PTR bufLenOneLine = 0;


TryAgain:
	bufLenOneLine = GetOneLine(&pwch[indexTrace], dwTotalBufLen);
	if (bufLenOneLine == 0)
	{
		goto FindNextLine;
	}
	else
	{
		CParserStringRange keyword;
		WCHAR ch = pwch[indexTrace];
		switch (ch)
		{
		case (L'#'): // comment lines begins with #
			goto FindNextLine;
		case (L'%'): // .cin control key begins with %
			Global::KeywordDelimiter = L'\t';  // set delimiter to tab for reading .cin files.
			controlKeyType = CIN_CONTROLKEY;
			break;
		case (L'['):
			controlKeyType = TTS_CONTROLKEY;
			break;
		default:
			controlKeyType = NOT_CONTROLKEY;
		}
		if (!ParseLine(&pwch[indexTrace], bufLenOneLine, &keyword))
		{
			if(controlKeyType == NOT_CONTROLKEY && !(searchMode == SEARCH_SYMBOL)) //Control key may have key without value.
				return FALSE;    // error
		}

		if (controlKeyType != NOT_CONTROLKEY)
		{	
			CParserStringRange controlKey;
			if(controlKeyType == CIN_CONTROLKEY)
				controlKey.Set(L"%*", 2);	
			else if(controlKeyType == TTS_CONTROLKEY)
				controlKey.Set(L"[*", 2);
			CStringRange::WildcardCompare(_locale, &controlKey, &keyword); // read the .cin or tts control key
			// read cin control key here.
			if(controlKeyType == TTS_CONTROLKEY)
			{	
				if ( CStringRange::Compare(_locale, &keyword, &controlKey.Set(L"[Text]", 6)) == CSTR_EQUAL)
				{ // in Text block
					if(!parseConfig) 
					{
						if(isTextSearch)
						{
							if(Global::hasPhraseSection) searchMode = SEARCH_NONE;  // use TTS phrase section in text search, thus set SERACH_NONE here.
							else searchMode = SEARCH_TEXT;
						}
						else searchMode = SEARCH_MAPPING;
					}
					else searchMode = SEARCH_NONE;
				}
				else if (CStringRange::Compare(_locale, &keyword, &controlKey.Set(L"[Phrase]", 8)) == CSTR_EQUAL)
				{ // in Phrase block
					if(parseConfig) Global::hasPhraseSection = TRUE;
					searchMode = (!parseConfig && isTextSearch && Global::hasPhraseSection)?SEARCH_TEXT_TTS_PHRASE:SEARCH_NONE;
				}
				else if (parseConfig && CStringRange::Compare(_locale, &keyword, &controlKey.Set(L"[Radical]", 9)) == CSTR_EQUAL)
				{
					searchMode = (parseConfig)?SEARCH_RADICAL:SEARCH_NONE;
				}
				else if (CStringRange::Compare(_locale, &keyword, &controlKey.Set(L"[Symbol]", 8)) == CSTR_EQUAL)
				{
					searchMode = (!parseConfig && !isTextSearch)?SEARCH_SYMBOL:SEARCH_NONE;
				}
				else if (parseConfig && CStringRange::Compare(_locale, &keyword, &controlKey.Set(L"[AutoCompose]", 13)) == CSTR_EQUAL)
				{
					Global::autoCompose = TRUE;// autoCompose is off for TTS table unless see [AutoCompose] control key
					searchMode =  SEARCH_NONE;
				}
				else
				{
					searchMode =  SEARCH_NONE;
				}
				controlKeyType = NOT_CONTROLKEY;
				goto FindNextLine;
			}
			else if(controlKeyType == CIN_CONTROLKEY)
			{
				if (parseConfig && CStringRange::WildcardCompare(_locale, &controlKey.Set(L"%autoCompose*", 13), &keyword))
				{
					searchMode = SEARCH_CONTROLKEY;
					goto ReadValue;
				}
				else if (CStringRange::WildcardCompare(_locale, &controlKey.Set(L"%chardef?begin", 14), &keyword))
				{
					if(!parseConfig)
					{
						if(isTextSearch) searchMode = SEARCH_TEXT;
						else searchMode = SEARCH_MAPPING;
					}
					else
						searchMode = SEARCH_NONE;
				}
				else if (CStringRange::WildcardCompare(_locale, &controlKey.Set(L"%chardef?end", 12),&keyword))
				{
					searchMode = SEARCH_NONE;
				}
				else if (parseConfig && CStringRange::WildcardCompare(_locale, &controlKey.Set(L"%keyname?begin", 14), &keyword))
				{
					searchMode = (parseConfig)?SEARCH_RADICAL:SEARCH_NONE;
				}
				else if (parseConfig && CStringRange::WildcardCompare(_locale, &controlKey.Set(L"%keyname?end", 14), &keyword))
				{
					searchMode = SEARCH_NONE;
				}
				else
				{
					searchMode = SEARCH_NONE;
				}
				controlKeyType = NOT_CONTROLKEY;
				goto FindNextLine;
			}
			goto FindNextLine;
		}
		
		if(searchMode == SEARCH_RADICAL)
		{
			goto ReadValue;
		}
		else if(searchMode == SEARCH_MAPPING || searchMode == SEARCH_SYMBOL || searchMode == SEARCH_TEXT_TTS_PHRASE) //compare key with searchcode
		{
			// Compare Dictionary key code and input key code
			if ((!isWildcardSearch) && (CStringRange::Compare(_locale, &keyword, _pSearchKeyCode) != CSTR_EQUAL))	goto FindNextLine;
			else if (!CStringRange::WildcardCompare(_locale, _pSearchKeyCode, &keyword))	goto FindNextLine; // Wildcard search
			goto ReadValue;
			
		}
		else if(searchMode == SEARCH_TEXT)  //compare value with searchcode
		{
			// Compare Dictionary converted string and input string
			CTSFDayiArray<CParserStringRange> convertedStrings;
			if (!ParseLine(&pwch[indexTrace], bufLenOneLine, &keyword, &convertedStrings)) //get value
				return FALSE;
			
			if (convertedStrings.Count() == 1)
			{
				CStringRange* pTempString = convertedStrings.GetAt(0);
				if ((!isWildcardSearch) && (CStringRange::Compare(_locale, pTempString, _pSearchKeyCode) != CSTR_EQUAL))	goto FindNextLine;
				else if (!CStringRange::WildcardCompare(_locale, _pSearchKeyCode, pTempString))	goto FindNextLine; // Wildcard search
				goto ReadValue;
			}
			else goto FindNextLine;
			
		}
		else	goto FindNextLine;  //bypassing all lines for all lines except the radical section for pars 
		
ReadValue:
		if(searchMode != SEARCH_NONE)
		{
			// Prepare return's CDictionaryResult
			if(!parseConfig)
			{
				*ppdret = new (std::nothrow) CDictionaryResult();
				if (!*ppdret)	return FALSE;
			}
			
			CTSFDayiArray<CParserStringRange> valueStrings;
			BOOL isPhraseEntry = (searchMode == SEARCH_TEXT_TTS_PHRASE) || (searchMode == SEARCH_SYMBOL);
			if (!ParseLine(&pwch[indexTrace], bufLenOneLine, &keyword, &valueStrings, isPhraseEntry,
				(isPhraseEntry)?_pSearchKeyCode:NULL))
			{
				if (!parseConfig && *ppdret)
				{
					delete *ppdret;
					*ppdret = nullptr;
				}
				if(controlKeyType == NOT_CONTROLKEY && !(searchMode == SEARCH_SYMBOL)) return FALSE;
			}
			if(searchMode == SEARCH_RADICAL)
			{
				PWCHAR radicalChar = new (std::nothrow) WCHAR[2];
				PWCHAR radical = new (std::nothrow) WCHAR[2];
				*radicalChar=L'0';
				*radical=L'0';
				StringCchCopyN(radicalChar,  2, keyword.Get(),1); 
				StringCchCopyN(radical, 2, valueStrings.GetAt(0)->Get(), 1);
				Global::radicalMap[towupper(*radicalChar)] = *radical;
				goto FindNextLine;
			}
			else if(searchMode == SEARCH_CONTROLKEY && controlKeyType == CIN_CONTROLKEY)  // get value of cin control keys
			{
				CParserStringRange testKey, value;
				
				
				testKey.Set(L"%autoCompose", 12);
				if (CStringRange::Compare(_locale, &keyword, &testKey) == CSTR_EQUAL)
				{
					value.Set(L"TRUE", 4);
					Global::autoCompose = (CStringRange::Compare(_locale, valueStrings.GetAt(0), &value) == CSTR_EQUAL);
				}

				controlKeyType = NOT_CONTROLKEY;
				goto FindNextLine;
			}
			else if(!parseConfig)
			{
				(*ppdret)->_FindKeyCode = keyword;
				(*ppdret)->_SearchKeyCode = *_pSearchKeyCode;

				for (UINT i = 0; i < valueStrings.Count(); i++)
				{
					CStringRange* findPhrase = (*ppdret)->_FindPhraseList.Append();
					if (findPhrase)
					{
						*findPhrase = *valueStrings.GetAt(i);
					}
				}
			}
			// Seek to next line
			isFound = TRUE;
		}
	}

FindNextLine:
	dwTotalBufLen -= bufLenOneLine;
	if (dwTotalBufLen == 0 )
	{
		indexTrace += bufLenOneLine;
		_charIndex += indexTrace;

		if ( !parseConfig && !isFound && *ppdret)
		{
			delete *ppdret;
			*ppdret = nullptr;
		}
		return (isFound ? TRUE : FALSE);        // End of file
	}

	indexTrace += bufLenOneLine;
	if (pwch[indexTrace] == L'\r' || pwch[indexTrace] == L'\n' || pwch[indexTrace] == L'\0')
	{
		bufLenOneLine = 1;
		goto FindNextLine;
	}

	if (isFound)
	{
		_charIndex += indexTrace;
		return TRUE;
	}

	goto TryAgain;
}
