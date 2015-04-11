//
//
// Derived from Microsoft Sample IME by Jeremy '13,7,17
//
//
#define DEBUG_PRING

#include "Private.h"
#include "DictionarySearch.h"
#include "BaseStructure.h"
#include "DIME.h"

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
	_searchMode = SEARCH_NONE;
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

BOOL CDictionarySearch::ParseConfig(IME_MODE imeMode, _T_RacialMap* pRadicalMap)
{
	debugPrint(L"CDictionarySearch::ParseConfig() imeMode = %d", imeMode);
	_imeMode = imeMode;

	return FindWorker(FALSE, NULL, FALSE, TRUE, pRadicalMap); // parseConfig=TRUE;
}

//+---------------------------------------------------------------------------
//
// FindWorker
//
//----------------------------------------------------------------------------

BOOL CDictionarySearch::FindWorker(BOOL isTextSearch, _Out_ CDictionaryResult **ppdret, BOOL isWildcardSearch, _In_opt_ BOOL parseConfig, _In_opt_ _T_RacialMap* pRadicalMap)
{
	
	BOOL fileReloaded;
	const WCHAR *pwch = GetBufferInWChar(&fileReloaded);
	if(fileReloaded)
	{
		_charIndex = 0;
	}

	DWORD_PTR dwTotalBufLen = GetBufferInWCharLength();        // in char
	if (dwTotalBufLen == 0)
	{
		return FALSE;
	}

	DWORD_PTR indexTrace = 0;     // in char
	if(!parseConfig) *ppdret = nullptr;
	
	enum CONTROLKEY_TYPE controlKeyType = NOT_CONTROLKEY;

	BOOL isFound = FALSE;
	DWORD_PTR bufLenOneLine = 0;


TryAgain:
	bufLenOneLine = GetOneLine(&pwch[indexTrace], dwTotalBufLen);
	if(pwch == nullptr) return FALSE;
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
							if(_searchSection == SEARCH_SECTION_PHRASE) _searchMode = SEARCH_NONE;  // use TTS phrase section in text search, thus set SERACH_NONE here.
							else _searchMode = SEARCH_TEXT;
						}
						else if(_searchSection == SEARCH_SECTION_TEXT) _searchMode = SEARCH_MAPPING;
						else _searchMode = SEARCH_NONE;
					}
					else _searchMode = SEARCH_NONE;
				}
				else if (CStringRange::Compare(_locale, &keyword, &controlKey.Set(L"[Phrase]", 8)) == CSTR_EQUAL)
				{ // in Phrase block
					if(parseConfig) Global::hasPhraseSection = TRUE;
					_searchMode = (!parseConfig  && _searchSection == SEARCH_SECTION_PHRASE)?SEARCH_PHRASE:SEARCH_NONE;
				}
				else if (parseConfig && CStringRange::Compare(_locale, &keyword, &controlKey.Set(L"[Radical]", 9)) == CSTR_EQUAL)
				{
					_searchMode = (parseConfig)?SEARCH_RADICAL:SEARCH_NONE;
				}
				else if (CStringRange::Compare(_locale, &keyword, &controlKey.Set(L"[Symbol]", 8)) == CSTR_EQUAL)
				{
					_searchMode = (!parseConfig && !isTextSearch && _searchSection == SEARCH_SECTION_SYMBOL)?SEARCH_SYMBOL:SEARCH_NONE;
				}
				else if (CStringRange::Compare(_locale, &keyword, &controlKey.Set(L"[PhraseFromKeystroke]", 21)) == CSTR_EQUAL)
				{
					_searchMode = (!parseConfig && !isTextSearch && _searchSection == SEARCH_SECTION_PRHASE_FROM_KEYSTROKE)?SEARCH_PRHASE_FROM_KEYSTROKE:SEARCH_NONE;
				}
				else if (parseConfig && CStringRange::Compare(_locale, &keyword, &controlKey.Set(L"[Config]", 8)) == CSTR_EQUAL)
				{
					_searchMode =  SEARCH_CONFIG;
				}
	
				controlKeyType = NOT_CONTROLKEY;
				goto FindNextLine;
			}
			else if(controlKeyType == CIN_CONTROLKEY)
			{
				if (parseConfig && CStringRange::WildcardCompare(_locale, &controlKey.Set(L"%autoCompose*", 13), &keyword))
				{
					_searchMode = SEARCH_CONTROLKEY;
					goto ReadValue;
				}
				else if (parseConfig && CStringRange::WildcardCompare(_locale, &controlKey.Set(L"%autoCompose*", 13), &keyword))
				{
					_searchMode = SEARCH_CONTROLKEY;
					goto ReadValue;
				}
				else if (CStringRange::WildcardCompare(_locale, &controlKey.Set(L"%chardef?begin", 14), &keyword))
				{
					if(!parseConfig)
					{
						if(isTextSearch) 
						{
							if(Global::hasCINPhraseSection) _searchMode = SEARCH_NONE;
							else _searchMode = SEARCH_TEXT;
						}
						else _searchMode = SEARCH_MAPPING;
					}
					else
						_searchMode = SEARCH_NONE;
				}
				else if (CStringRange::WildcardCompare(_locale, &controlKey.Set(L"%chardef?end", 12),&keyword))
				{
					_searchMode = SEARCH_NONE;
				}
				else if (parseConfig && CStringRange::WildcardCompare(_locale, &controlKey.Set(L"%keyname?begin", 14), &keyword))
				{
					_searchMode = (parseConfig)?SEARCH_RADICAL:SEARCH_NONE;
				}
				else if (parseConfig && CStringRange::WildcardCompare(_locale, &controlKey.Set(L"%keyname?end", 12), &keyword))
				{
					_searchMode = SEARCH_NONE;
				}
				else if (parseConfig &&
					 (CStringRange::Compare(_locale, &keyword, &controlKey.Set(L"%keyname", 8)) == CSTR_EQUAL ||
					  CStringRange::WildcardCompare(_locale, &controlKey.Set(L"%keyname*", 9), &keyword)  ))
				{
					_searchMode = SEARCH_CONTROLKEY;
					goto ReadValue;
				}
				else if (!parseConfig && 
					(CStringRange::Compare(_locale, &keyword, &controlKey.Set(L"%chardef", 8)) == CSTR_EQUAL ||
					 CStringRange::WildcardCompare(_locale, &controlKey.Set(L"%chardef*", 9), &keyword)  ))
				{
					_searchMode = SEARCH_CONTROLKEY;
					goto ReadValue;
				}
	
				controlKeyType = NOT_CONTROLKEY;
				goto FindNextLine;
			}
			goto FindNextLine;
		}
		//start searching keys
		if(_searchMode == SEARCH_RADICAL || _searchMode == SEARCH_CONFIG)
		{
			goto ReadValue;
		}
		else if(_searchMode == SEARCH_MAPPING || _searchMode == SEARCH_SYMBOL || _searchMode == SEARCH_PHRASE || _searchMode == SEARCH_PRHASE_FROM_KEYSTROKE) //compare key with searchcode
		{
			// Compare Dictionary key code and input key code
			if (_pSearchKeyCode && (!isWildcardSearch) &&
				(CStringRange::Compare(_locale, &keyword, _pSearchKeyCode) != CSTR_EQUAL))	goto FindNextLine;
			else if (_pSearchKeyCode &&
				!CStringRange::WildcardCompare(_locale, _pSearchKeyCode, &keyword))	goto FindNextLine; // Wildcard search
			goto ReadValue;
			
		}
		else if(_searchMode == SEARCH_TEXT)  //compare value with searchcode
		{
			// Compare Dictionary converted string and input string
			CDIMEArray<CParserStringRange> convertedStrings;
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
		if(_searchMode != SEARCH_NONE)
		{
			// Prepare return's CDictionaryResult
			if(!parseConfig) // when parseConfig TRUE, no results will be return and **ppdret is NULL
			{
				*ppdret = new (std::nothrow) CDictionaryResult();
				if (!*ppdret)	return FALSE;
			}
			
			CDIMEArray<CParserStringRange> valueStrings;
			BOOL isPhraseEntry = (_searchMode == SEARCH_PHRASE) || (_searchMode == SEARCH_SYMBOL) || (_searchMode == SEARCH_PRHASE_FROM_KEYSTROKE);
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
			if(_searchMode == SEARCH_RADICAL && pRadicalMap)
			{
				WCHAR radicalChar = *keyword.Get();
				PWCHAR radical = new (std::nothrow) WCHAR[16];
				*radical = '\0';
				StringCchCopyN(radical, 16, valueStrings.GetAt(0)->Get(), valueStrings.GetAt(0)->GetLength());

				assert(pRadicalMap->size() < MAX_RADICAL);
				(*pRadicalMap)[towupper(radicalChar)] = radical;
				goto FindNextLine;
			}
			if(_searchMode == SEARCH_CONFIG)
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
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"DayiArticleMode", 15)) == CSTR_EQUAL)
					CConfig::setDayiArticleMode((CStringRange::Compare(_locale, valueStrings.GetAt(0), &value.Set(L"1", 1)) == CSTR_EQUAL));
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
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"DoHanConvert", 12)) == CSTR_EQUAL)
					CConfig::SetDoHanConvert((CStringRange::Compare(_locale, valueStrings.GetAt(0), &value.Set(L"1", 1)) == CSTR_EQUAL));
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"ReloadReversionConversion", 25)) == CSTR_EQUAL)
					CConfig::SetReloadReverseConversion((CStringRange::Compare(_locale, valueStrings.GetAt(0), &value.Set(L"1", 1)) == CSTR_EQUAL));
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"ReversionConversionCLSID", 24)) == CSTR_EQUAL)
				{
					BSTR pbstr;
					pbstr = SysAllocStringLen( valueStrings.GetAt(0)->Get(), (UINT) valueStrings.GetAt(0)->GetLength());
					CLSID clsid;
					if(SUCCEEDED(CLSIDFromString(pbstr, &clsid)))
						CConfig::SetReverseConverstionCLSID(clsid);
					SysFreeString(pbstr);
				}
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"ReversionConversionGUIDProfile", 30)) == CSTR_EQUAL)
				{
					BSTR pbstr;
					pbstr = SysAllocStringLen( valueStrings.GetAt(0)->Get(), (UINT) valueStrings.GetAt(0)->GetLength());
					CLSID clsid;
					if(SUCCEEDED(CLSIDFromString(pbstr, &clsid)))
						CConfig::SetReverseConversionGUIDProfile(clsid);
					SysFreeString(pbstr);
				}
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"ReversionConversionDescription", 30)) == CSTR_EQUAL)
				{
					WCHAR *pwszReverseConversionDescription = new (std::nothrow) WCHAR[valueStrings.GetAt(0)->GetLength() + 1];
					StringCchCopyN(pwszReverseConversionDescription, valueStrings.GetAt(0)->GetLength() +1, valueStrings.GetAt(0)->Get(), valueStrings.GetAt(0)->GetLength());
					CConfig::SetReverseConversionDescription(pwszReverseConversionDescription);
				}
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
					WCHAR *pwszFontFaceName = new (std::nothrow) WCHAR[LF_FACESIZE];
					assert(valueStrings.GetAt(0)->GetLength() < LF_FACESIZE-1);
					StringCchCopyN(pwszFontFaceName, LF_FACESIZE, valueStrings.GetAt(0)->Get(), valueStrings.GetAt(0)->GetLength());
					CConfig::SetFontFaceName(pwszFontFaceName);
				}
				goto FindNextLine;
			}
			else if(_searchMode == SEARCH_CONTROLKEY && controlKeyType == CIN_CONTROLKEY)  // get value of cin control keys
			{
				CParserStringRange testKey, value;				
				if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"%autoCompose", 12)) == CSTR_EQUAL)
				{
					CConfig::SetAutoCompose((CStringRange::Compare(_locale, valueStrings.GetAt(0), &value.Set(L"1", 1)) == CSTR_EQUAL));
				}
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"%keyname", 8)) == CSTR_EQUAL)
				{
					if(CStringRange::Compare(_locale, valueStrings.GetAt(0), &value.Set(L"begin", 5)) == CSTR_EQUAL)
						_searchMode = (parseConfig)?SEARCH_RADICAL:SEARCH_NONE;
					else if(CStringRange::Compare(_locale, valueStrings.GetAt(0), &value.Set(L"end", 3)) == CSTR_EQUAL)
						_searchMode = SEARCH_NONE;
				}
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"%chardef", 8)) == CSTR_EQUAL)
				{
					if(CStringRange::Compare(_locale, valueStrings.GetAt(0), &value.Set(L"begin", 5)) == CSTR_EQUAL)
					{
						if(!parseConfig)
						{
							if(isTextSearch) 
							{
								if(Global::hasCINPhraseSection) _searchMode = SEARCH_NONE;
								else _searchMode = SEARCH_TEXT;
							}
							else _searchMode = SEARCH_MAPPING;
						}
						else
							_searchMode = SEARCH_NONE;
					}
					else if(CStringRange::Compare(_locale, valueStrings.GetAt(0), &value.Set(L"end", 3)) == CSTR_EQUAL)
						_searchMode = SEARCH_NONE;
				}
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"%selkey", 7)) == CSTR_EQUAL)
				{
				}
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"%endkey", 7)) == CSTR_EQUAL)
				{
				}

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
