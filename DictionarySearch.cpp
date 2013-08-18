//
//
// Derived from Microsoft Sample IME by Jeremy '13,7,17
//
//
#define DEBUG_PRING

#include "Private.h"
#include "DictionarySearch.h"
#include "BaseStructure.h"
#include "TSFTTS.h"

//+---------------------------------------------------------------------------
//
// ctor
//
//----------------------------------------------------------------------------

CDictionarySearch::CDictionarySearch(LCID locale, _In_ CFile *pFile, _In_ CStringRange *pSearchKeyCode, _In_ WCHAR keywordDelimiter) : CDictionaryParser(locale, keywordDelimiter)
{
	_pFile = pFile;
	_pSearchKeyCode = pSearchKeyCode;
	_charIndex = 0;
	_searchSection = SEARCH_SECTION_TEXT;
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
	debugPrint(L"CDictionarySearch::ParseConfig()");
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
			if(controlKeyType == NOT_CONTROLKEY) //Control key may have key without value.
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
						else if(_searchSection == SEARCH_SECTION_TEXT) searchMode = SEARCH_MAPPING;
						else searchMode = SEARCH_NONE;
					}
					else searchMode = SEARCH_NONE;
				}
				else if (CStringRange::Compare(_locale, &keyword, &controlKey.Set(L"[Phrase]", 8)) == CSTR_EQUAL)
				{ // in Phrase block
					if(parseConfig) Global::hasPhraseSection = TRUE;
					searchMode = (!parseConfig && isTextSearch)?SEARCH_PHRASE:SEARCH_NONE;
				}
				else if (parseConfig && CStringRange::Compare(_locale, &keyword, &controlKey.Set(L"[Radical]", 9)) == CSTR_EQUAL)
				{
					searchMode = (parseConfig)?SEARCH_RADICAL:SEARCH_NONE;
				}
				else if (CStringRange::Compare(_locale, &keyword, &controlKey.Set(L"[Symbol]", 8)) == CSTR_EQUAL)
				{
					searchMode = (!parseConfig && !isTextSearch && _searchSection == SEARCH_SECTION_SYMBOL)?SEARCH_SYMBOL:SEARCH_NONE;
				}
				else if (CStringRange::Compare(_locale, &keyword, &controlKey.Set(L"[PhraseFromKeystroke]", 21)) == CSTR_EQUAL)
				{
					searchMode = (!parseConfig && !isTextSearch && _searchSection == SEARCH_SECTION_PRHASE_FROM_KEYSTROKE)?SEARCH_PRHASE_FROM_KEYSTROKE:SEARCH_NONE;
				}
				else if (parseConfig && CStringRange::Compare(_locale, &keyword, &controlKey.Set(L"[Config]", 8)) == CSTR_EQUAL)
				{
					searchMode =  SEARCH_CONFIG;
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
						if(isTextSearch) 
						{
							if(Global::hasCINPhraseSection) searchMode = SEARCH_NONE;
							else searchMode = SEARCH_TEXT;
						}
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
				else if (CStringRange::WildcardCompare(_locale, &controlKey.Set(L"%phrasedef?begin", 16),&keyword))
				{
					if(parseConfig) Global::hasCINPhraseSection = TRUE;
					searchMode = (!parseConfig && isTextSearch)?SEARCH_PHRASE:SEARCH_NONE;
				}
				else if (parseConfig && CStringRange::WildcardCompare(_locale, &controlKey.Set(L"%phrasedef?end", 14), &keyword))
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
		//start searching keys
		if(searchMode == SEARCH_RADICAL || searchMode == SEARCH_CONFIG)
		{
			goto ReadValue;
		}
		else if(searchMode == SEARCH_MAPPING || searchMode == SEARCH_SYMBOL || searchMode == SEARCH_PHRASE || searchMode == SEARCH_PRHASE_FROM_KEYSTROKE) //compare key with searchcode
		{
			// Compare Dictionary key code and input key code
			if ((!isWildcardSearch) && (CStringRange::Compare(_locale, &keyword, _pSearchKeyCode) != CSTR_EQUAL))	goto FindNextLine;
			else if (!CStringRange::WildcardCompare(_locale, _pSearchKeyCode, &keyword))	goto FindNextLine; // Wildcard search
			goto ReadValue;
			
		}
		else if(searchMode == SEARCH_TEXT)  //compare value with searchcode
		{
			// Compare Dictionary converted string and input string
			CTSFTTSArray<CParserStringRange> convertedStrings;
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
		else	goto FindNextLine;  //bypassing all lines else
		
ReadValue:
		if(searchMode != SEARCH_NONE)
		{
			// Prepare return's CDictionaryResult
			if(!parseConfig) // when parseConfig TRUE, no results will be return and **ppdret is NULL
			{
				*ppdret = new (std::nothrow) CDictionaryResult();
				if (!*ppdret)	return FALSE;
			}
			
			CTSFTTSArray<CParserStringRange> valueStrings;
			BOOL isPhraseEntry = (searchMode == SEARCH_PHRASE) || (searchMode == SEARCH_SYMBOL) || (searchMode == SEARCH_PRHASE_FROM_KEYSTROKE);
			if (!ParseLine(&pwch[indexTrace], bufLenOneLine, &keyword, &valueStrings, isPhraseEntry,
				(isPhraseEntry)?_pSearchKeyCode:NULL))
			{
				if (!parseConfig && *ppdret)
				{
					delete *ppdret;
					*ppdret = nullptr;
				}
				if(controlKeyType == NOT_CONTROLKEY)
					return FALSE;
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
			if(searchMode == SEARCH_CONFIG)
			{
				CParserStringRange testKey, value;				
				if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"AutoCompose", 11)) == CSTR_EQUAL)
					CConfig::SetAutoCompose((CStringRange::Compare(_locale, valueStrings.GetAt(0), &value.Set(L"1", 1)) == CSTR_EQUAL));
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"ThreeCodeMode", 13)) == CSTR_EQUAL)
					CConfig::SetThreeCodeMode((CStringRange::Compare(_locale, valueStrings.GetAt(0), &value.Set(L"1", 1)) == CSTR_EQUAL));
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"ArrayForceSP", 12)) == CSTR_EQUAL)
					CConfig::SetArrayForceSP((CStringRange::Compare(_locale, valueStrings.GetAt(0), &value.Set(L"1", 1)) == CSTR_EQUAL));
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"ArrayNotifySP", 13)) == CSTR_EQUAL)
					CConfig::SetArrayNotifySP((CStringRange::Compare(_locale, valueStrings.GetAt(0), &value.Set(L"1", 1)) == CSTR_EQUAL));
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"SpaceAsPageDown", 15)) == CSTR_EQUAL)
					CConfig::SetSpaceAsPageDown((CStringRange::Compare(_locale, valueStrings.GetAt(0), &value.Set(L"1", 1)) == CSTR_EQUAL));
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"ArrowKeySWPages", 15)) == CSTR_EQUAL)
					CConfig::SetArrowKeySWPages((CStringRange::Compare(_locale, valueStrings.GetAt(0), &value.Set(L"1", 1)) == CSTR_EQUAL));
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"DoBeep", 6)) == CSTR_EQUAL)
					CConfig::SetDoBeep((CStringRange::Compare(_locale, valueStrings.GetAt(0), &value.Set(L"1", 1)) == CSTR_EQUAL));
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"FontSize", 10)) == CSTR_EQUAL)
					CConfig::SetFontSize(_wtoi(valueStrings.GetAt(0)->Get()));
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"FontWeight", 10)) == CSTR_EQUAL)
					CConfig::SetFontWeight(_wtoi(valueStrings.GetAt(0)->Get()));
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"FontItalic", 10)) == CSTR_EQUAL)
					CConfig::SetFontItalic((CStringRange::Compare(_locale, valueStrings.GetAt(0), &value.Set(L"1", 1)) == CSTR_EQUAL));
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"MaxCodes", 8)) == CSTR_EQUAL)
					CConfig::SetMaxCodes(_wtoi(valueStrings.GetAt(0)->Get()));
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"ActivatedKeyboardMode", 21)) == CSTR_EQUAL)
					CConfig::SetActivatedKeyboardMode((CStringRange::Compare(_locale, valueStrings.GetAt(0), &value.Set(L"1", 1)) == CSTR_EQUAL));
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"MakePhrase", 10)) == CSTR_EQUAL)
					CConfig::SetMakePhrase((CStringRange::Compare(_locale, valueStrings.GetAt(0), &value.Set(L"1", 1)) == CSTR_EQUAL));
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"ShowNotifyDesktop", 17)) == CSTR_EQUAL)
					CConfig::SetShowNotifyDesktop((CStringRange::Compare(_locale, valueStrings.GetAt(0), &value.Set(L"1", 1)) == CSTR_EQUAL));			
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"AppPermissionSet", 16)) == CSTR_EQUAL)
					CConfig::SetAppPermissionSet((CStringRange::Compare(_locale, valueStrings.GetAt(0), &value.Set(L"1", 1)) == CSTR_EQUAL));
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"ItemColor", 10)) == CSTR_EQUAL)
					CConfig::SetItemColor(wcstoul(valueStrings.GetAt(0)->Get(), NULL, 0));
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"PhraseColor", 12)) == CSTR_EQUAL)
					CConfig::SetPhraseColor(wcstoul(valueStrings.GetAt(0)->Get(), NULL, 0));
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"NumberColor", 12)) == CSTR_EQUAL)
					CConfig::SetNumberColor(wcstoul(valueStrings.GetAt(0)->Get(), NULL, 0));
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"ItemBGColor", 12)) == CSTR_EQUAL)
					CConfig::SetItemBGColor(wcstoul(valueStrings.GetAt(0)->Get(), NULL, 0));
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"SelectedItemColor", 18)) == CSTR_EQUAL)
					CConfig::SetSelectedColor(wcstoul(valueStrings.GetAt(0)->Get(), NULL, 0));
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"SelectedBGItemColor", 20)) == CSTR_EQUAL)
					CConfig::SetSelectedBGColor(wcstoul(valueStrings.GetAt(0)->Get(), NULL, 0));	
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"FontFaceName", 12)) == CSTR_EQUAL)
				{
					WCHAR *pwszFontFaceName = new (std::nothrow) WCHAR[32];
					StringCchCopyN(pwszFontFaceName, 32, valueStrings.GetAt(0)->Get(), valueStrings.GetAt(0)->GetLength());
					CConfig::SetFontFaceName(pwszFontFaceName);
				}
				goto FindNextLine;
			}
			else if(searchMode == SEARCH_CONTROLKEY && controlKeyType == CIN_CONTROLKEY)  // get value of cin control keys
			{
				CParserStringRange testKey, value;				
				if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"%autoCompose", 12)) == CSTR_EQUAL)
					CConfig::SetAutoCompose((CStringRange::Compare(_locale, valueStrings.GetAt(0), &value.Set(L"1", 1)) == CSTR_EQUAL));


				controlKeyType = NOT_CONTROLKEY;
				goto FindNextLine;
			}
			else if(!parseConfig)  //MAPPING SYMBOL PHRASE all go here.
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
