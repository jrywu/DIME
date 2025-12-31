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

#include "Private.h"
#include "DictionarySearch.h"
#include "BaseStructure.h"
#include "DIME.h"

//+---------------------------------------------------------------------------
//
// ctor
//
//----------------------------------------------------------------------------

CDictionarySearch::CDictionarySearch(LCID locale, _In_ CFile *pFile, _In_opt_ CStringRange *pSearchKeyCode, _In_ WCHAR keywordDelimiter) : CDictionaryParser(locale, keywordDelimiter)
{
	_pTextService = nullptr;
	_imeMode = IME_MODE::IME_MODE_NONE;
	_pFile = pFile;
	_pSearchKeyCode = pSearchKeyCode;
	_charIndex = 0;
	_searchSection = SEARCH_SECTION::SEARCH_SECTION_TEXT;
	_searchMode = SEARCH_MODE::SEARCH_NONE;
	_sortedSearchResultFound = FALSE;
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

BOOL CDictionarySearch::ParseConfig(IME_MODE imeMode, 
	_Inout_opt_ _T_RadicalMap* pRadicalMap, _Inout_opt_ _T_RadicalIndexMap* pRadicalIndexMap,
	_Inout_opt_ PWCH pSelkey, _Inout_opt_ PWCH pEndkey)
{
	debugPrint(L"CDictionarySearch::ParseConfig() imeMode = %d", imeMode);
	_imeMode = imeMode;

	return FindWorker(FALSE, NULL, FALSE, TRUE, pRadicalMap, pRadicalIndexMap, pSelkey, pEndkey); // parseConfig=TRUE;
}

//+---------------------------------------------------------------------------
//
// FindWorker
//
//----------------------------------------------------------------------------

BOOL CDictionarySearch::FindWorker(BOOL isTextSearch, _Out_opt_ CDictionaryResult **ppdret, _In_ BOOL isWildcardSearch, _In_ BOOL parseConfig,
	_Inout_opt_ _T_RadicalMap* pRadicalMap, _Inout_opt_ _T_RadicalIndexMap* pRadicalIndexMap,
	_Inout_opt_ PWCH pSelkey, _Inout_opt_ PWCH pEndkey)
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
	
	CONTROLKEY_TYPE controlKeyType = CONTROLKEY_TYPE::NOT_CONTROLKEY;

	BOOL isFound = FALSE;
	DWORD_PTR bufLenOneLine = 0;

	WCHAR lastInitial = '\0';
	BOOL cinSorted = FALSE;


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
		if (pwch[indexTrace] == 0xFEFF)
		{
			dwTotalBufLen--;
			indexTrace++; //bypass unicode byte order signature
		}
		WCHAR ch = pwch[indexTrace];
		switch (ch)
		{
		case (L'#'): // comment lines begins with #
			goto FindNextLine;
		case (L'%'): // .cin control key begins with %
			controlKeyType = CONTROLKEY_TYPE::CIN_CONTROLKEY;
			break;
		case (L'['):
			controlKeyType = CONTROLKEY_TYPE::TTS_CONTROLKEY;
			break;
		default:
			controlKeyType = CONTROLKEY_TYPE::NOT_CONTROLKEY;
		}
		if (!ParseLine(&pwch[indexTrace], bufLenOneLine, &keyword))
		{
			if (controlKeyType == CONTROLKEY_TYPE::NOT_CONTROLKEY) //Control key may have key without value.
			{
				if (controlKeyType == CONTROLKEY_TYPE::NOT_CONTROLKEY) //Control key may have key without value.
				{
					if (parseConfig)
						return cinSorted;
					else
							return FALSE;    // error for key without value
				}
			}
		}
		if (!isWildcardSearch && _sortedSearchResultFound && (CStringRange::Compare(_locale, &keyword, _pSearchKeyCode) != CSTR_EQUAL))
		{  //return FALSE to stop search if keyword is not same with the search keyword in sorted search mode with previous result found already.
			return FALSE;
		}

		if (controlKeyType != CONTROLKEY_TYPE::NOT_CONTROLKEY)
		{	
			CParserStringRange controlKey;
			if(controlKeyType == CONTROLKEY_TYPE::CIN_CONTROLKEY)
				controlKey.Set(L"%*", 2);	
			else if(controlKeyType == CONTROLKEY_TYPE::TTS_CONTROLKEY)
				controlKey.Set(L"[*", 2);
			CStringRange::WildcardCompare(_locale, &controlKey, &keyword); // read the .cin or tts control key
			// read cin control key here.
			if(controlKeyType == CONTROLKEY_TYPE::TTS_CONTROLKEY)
			{	
				if ( CStringRange::Compare(_locale, &keyword, &controlKey.Set(L"[Text]", 6)) == CSTR_EQUAL)
				{ // in Text block
					if(!parseConfig) 
					{
						if(isTextSearch)
						{
							if(_searchSection == SEARCH_SECTION::SEARCH_SECTION_PHRASE) _searchMode = SEARCH_MODE::SEARCH_NONE;  // use TTS phrase section in text search, thus set SERACH_NONE here.
							else _searchMode = SEARCH_MODE::SEARCH_TEXT;
						}
						else if(_searchSection == SEARCH_SECTION::SEARCH_SECTION_TEXT) _searchMode = SEARCH_MODE::SEARCH_MAPPING;
						else _searchMode = SEARCH_MODE::SEARCH_NONE;
					}
					else _searchMode = SEARCH_MODE::SEARCH_NONE;
				}
				else if (CStringRange::Compare(_locale, &keyword, &controlKey.Set(L"[Phrase]", 8)) == CSTR_EQUAL)
				{ // in Phrase block
					if(parseConfig) Global::hasPhraseSection = TRUE;
					_searchMode = (!parseConfig  && _searchSection == SEARCH_SECTION::SEARCH_SECTION_PHRASE)? SEARCH_MODE::SEARCH_PHRASE: SEARCH_MODE::SEARCH_NONE;
				}
				else if (parseConfig && CStringRange::Compare(_locale, &keyword, &controlKey.Set(L"[Radical]", 9)) == CSTR_EQUAL)
				{
					_searchMode = (parseConfig)? SEARCH_MODE::SEARCH_RADICAL: SEARCH_MODE::SEARCH_NONE;
				}
				else if (CStringRange::Compare(_locale, &keyword, &controlKey.Set(L"[Symbol]", 8)) == CSTR_EQUAL)
				{
					_searchMode = (!parseConfig && !isTextSearch && _searchSection == SEARCH_SECTION::SEARCH_SECTION_SYMBOL)? SEARCH_MODE::SEARCH_SYMBOL: SEARCH_MODE::SEARCH_NONE;
				}
				else if (CStringRange::Compare(_locale, &keyword, &controlKey.Set(L"[PhraseFromKeystroke]", 21)) == CSTR_EQUAL)
				{
					_searchMode = (!parseConfig && !isTextSearch && _searchSection == SEARCH_SECTION::SEARCH_SECTION_PRHASE_FROM_KEYSTROKE)? SEARCH_MODE::SEARCH_PRHASE_FROM_KEYSTROKE: SEARCH_MODE::SEARCH_NONE;
				}
				else if (parseConfig && CStringRange::Compare(_locale, &keyword, &controlKey.Set(L"[Config]", 8)) == CSTR_EQUAL)
				{
					_searchMode = SEARCH_MODE::SEARCH_CONFIG;
				}
	
				controlKeyType = CONTROLKEY_TYPE::NOT_CONTROLKEY;
				goto FindNextLine;
			}
			else if (controlKeyType == CONTROLKEY_TYPE::CIN_CONTROLKEY)
			{
				if (CStringRange::WildcardCompare(_locale, &controlKey.Set(L"%chardef?begin", 14), &keyword))
				{
					if (!parseConfig)
					{
						if (isTextSearch)
						{
							if (Global::hasCINPhraseSection) _searchMode = SEARCH_MODE::SEARCH_NONE;
							else _searchMode = SEARCH_MODE::SEARCH_TEXT;
						}
						else
						{
							_searchMode = SEARCH_MODE::SEARCH_MAPPING;
							lastInitial = '\00';
						}
					}
					else
					{
						_searchMode = cinSorted ? SEARCH_MODE::SEARCH_MAPPING : SEARCH_MODE::SEARCH_NONE;
						lastInitial = '\00';
					}

				}
				else if (CStringRange::WildcardCompare(_locale, &controlKey.Set(L"%chardef?end", 12), &keyword))
				{
					_searchMode = SEARCH_MODE::SEARCH_NONE;
				}
				else if (parseConfig && CStringRange::WildcardCompare(_locale, &controlKey.Set(L"%keyname?begin", 14), &keyword))
				{
					_searchMode = SEARCH_MODE::SEARCH_RADICAL;
					initialRadialIndexMap(pRadicalIndexMap);
				}
				else if (parseConfig && CStringRange::WildcardCompare(_locale, &controlKey.Set(L"%keyname?end", 12), &keyword))
				{
					_searchMode = SEARCH_MODE::SEARCH_NONE;
				}
				else if (parseConfig &&
					(
					CStringRange::WildcardCompare(_locale, &controlKey.Set(L"%autoCompose*", 13), &keyword) ||
					CStringRange::WildcardCompare(_locale, &controlKey.Set(L"%keyname*", 9), &keyword) ||
					CStringRange::WildcardCompare(_locale, &controlKey.Set(L"%chardef*", 9), &keyword) ||
					CStringRange::WildcardCompare(_locale, &controlKey.Set(L"%selkey*", 8), &keyword) ||
					CStringRange::WildcardCompare(_locale, &controlKey.Set(L"%endkey*", 8), &keyword) ||
					CStringRange::WildcardCompare(_locale, &controlKey.Set(L"%sorted*", 8), &keyword)) 
					)
				{
					_searchMode = SEARCH_MODE::SEARCH_CONTROLKEY;
					goto ReadValue;
				}

	

				controlKeyType = CONTROLKEY_TYPE::NOT_CONTROLKEY;
				goto FindNextLine;
			}
			goto FindNextLine;
		}
		//start searching keys
		if(_searchMode == SEARCH_MODE::SEARCH_RADICAL || _searchMode == SEARCH_MODE::SEARCH_CONFIG)
		{
			goto ReadValue;
		}
		else if(_searchMode == SEARCH_MODE::SEARCH_MAPPING || _searchMode == SEARCH_MODE::SEARCH_SYMBOL || _searchMode == SEARCH_MODE::SEARCH_PHRASE || _searchMode == SEARCH_MODE::SEARCH_PRHASE_FROM_KEYSTROKE) //compare key with searchcode
		{
			// Compare Dictionary key code and input key code
			if (_pSearchKeyCode && (!isWildcardSearch) &&
				(CStringRange::Compare(_locale, &keyword, _pSearchKeyCode) != CSTR_EQUAL))	goto FindNextLine;
			else if (_pSearchKeyCode &&
				!CStringRange::WildcardCompare(_locale, _pSearchKeyCode, &keyword))	goto FindNextLine; // Wildcard search
			goto ReadValue;
			
		}
		else if(_searchMode == SEARCH_MODE::SEARCH_TEXT)  //compare value with searchcode
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
		if(_searchMode != SEARCH_MODE::SEARCH_NONE)
		{
			// Prepare return's CDictionaryResult
			if(!parseConfig) // when parseConfig TRUE, no results will be return and **ppdret is NULL
			{
				*ppdret = new (std::nothrow) CDictionaryResult();
				if (!*ppdret)	return FALSE;
			}
			
			CDIMEArray<CParserStringRange> valueStrings;
			BOOL isPhraseEntry = (_searchMode == SEARCH_MODE::SEARCH_PHRASE) || (_searchMode == SEARCH_MODE::SEARCH_SYMBOL) || (_searchMode == SEARCH_MODE::SEARCH_PRHASE_FROM_KEYSTROKE);
			if (!ParseLine(&pwch[indexTrace], bufLenOneLine, &keyword, &valueStrings, isPhraseEntry,
				(isPhraseEntry)?_pSearchKeyCode:NULL))
			{
				if (!parseConfig && *ppdret)
				{
					delete *ppdret;
					*ppdret = nullptr;
				}
				if(controlKeyType == CONTROLKEY_TYPE::NOT_CONTROLKEY)
					return FALSE;
			}
			if(_searchMode == SEARCH_MODE::SEARCH_RADICAL && pRadicalMap)
			{
				WCHAR radicalChar = *keyword.Get();
				PWCHAR radical = new (std::nothrow) WCHAR[16];
				*radical = '\0';
				StringCchCopyN(radical, 16, valueStrings.GetAt(0)->Get(), valueStrings.GetAt(0)->GetLength());

				// Replace assert with runtime bounds checking
				if (pRadicalMap->size() >= MAX_RADICAL)
				{
					debugPrint(L"Warning: RadicalMap size exceeds MAX_RADICAL limit, skipping entry");
					goto FindNextLine;
				}
				(*pRadicalMap)[towupper(radicalChar)] = radical;
				goto FindNextLine;
			}
			if(_searchMode == SEARCH_MODE::SEARCH_CONFIG)
			{
				CParserStringRange testKey, value;				
				if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"AutoCompose", 11)) == CSTR_EQUAL)
					CConfig::SetAutoCompose((CStringRange::Compare(_locale, valueStrings.GetAt(0), &value.Set(L"1", 1)) == CSTR_EQUAL));
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"ArrayForceSP", 12)) == CSTR_EQUAL)
					CConfig::SetArrayForceSP((CStringRange::Compare(_locale, valueStrings.GetAt(0), &value.Set(L"1", 1)) == CSTR_EQUAL));
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"ArrayNotifySP", 13)) == CSTR_EQUAL)
					CConfig::SetArrayNotifySP((CStringRange::Compare(_locale, valueStrings.GetAt(0), &value.Set(L"1", 1)) == CSTR_EQUAL));
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"ArraySingleQuoteCustomPhrase", 28)) == CSTR_EQUAL)
					CConfig::SetArraySingleQuoteCustomPhrase((CStringRange::Compare(_locale, valueStrings.GetAt(0), &value.Set(L"1", 1)) == CSTR_EQUAL));
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"DayiArticleMode", 15)) == CSTR_EQUAL)
					CConfig::setDayiArticleMode((CStringRange::Compare(_locale, valueStrings.GetAt(0), &value.Set(L"1", 1)) == CSTR_EQUAL));
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"SpaceAsPageDown", 15)) == CSTR_EQUAL)
					CConfig::SetSpaceAsPageDown((CStringRange::Compare(_locale, valueStrings.GetAt(0), &value.Set(L"1", 1)) == CSTR_EQUAL));
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"SpaceAsFirstCandSelkey", 22)) == CSTR_EQUAL)
					CConfig::SetSpaceAsFirstCaniSelkey((CStringRange::Compare(_locale, valueStrings.GetAt(0), &value.Set(L"1", 1)) == CSTR_EQUAL));
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"ArrowKeySWPages", 15)) == CSTR_EQUAL)
					CConfig::SetArrowKeySWPages((CStringRange::Compare(_locale, valueStrings.GetAt(0), &value.Set(L"1", 1)) == CSTR_EQUAL));
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"ClearOnBeep", 11)) == CSTR_EQUAL)
					CConfig::SetClearOnBeep((CStringRange::Compare(_locale, valueStrings.GetAt(0), &value.Set(L"1", 1)) == CSTR_EQUAL));
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"DoBeep", 6)) == CSTR_EQUAL)
					CConfig::SetDoBeep((CStringRange::Compare(_locale, valueStrings.GetAt(0), &value.Set(L"1", 1)) == CSTR_EQUAL));
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"DoBeepNotify", 12)) == CSTR_EQUAL)
					CConfig::SetDoBeepNotify((CStringRange::Compare(_locale, valueStrings.GetAt(0), &value.Set(L"1", 1)) == CSTR_EQUAL));
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"DoBeepOnCandi", 13)) == CSTR_EQUAL)
					CConfig::SetDoBeepOnCandi((CStringRange::Compare(_locale, valueStrings.GetAt(0), &value.Set(L"1", 1)) == CSTR_EQUAL));
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"FontSize", 10)) == CSTR_EQUAL)
					CConfig::SetFontSize(_wtoi(valueStrings.GetAt(0)->Get()));
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"FontWeight", 10)) == CSTR_EQUAL)
					CConfig::SetFontWeight(_wtoi(valueStrings.GetAt(0)->Get()));
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"FontItalic", 10)) == CSTR_EQUAL)
					CConfig::SetFontItalic((CStringRange::Compare(_locale, valueStrings.GetAt(0), &value.Set(L"1", 1)) == CSTR_EQUAL));
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"DoubleSingleByteMode", 20)) == CSTR_EQUAL)
					CConfig::SetDoubleSingleByteMode((DOUBLE_SINGLE_BYTE_MODE)_wtoi(valueStrings.GetAt(0)->Get()));
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"IMEShiftMode", 12)) == CSTR_EQUAL)
					CConfig::SetIMEShiftMode((IME_SHIFT_MODE)_wtoi(valueStrings.GetAt(0)->Get()));
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"MaxCodes", 8)) == CSTR_EQUAL)
					CConfig::SetMaxCodes(_wtoi(valueStrings.GetAt(0)->Get()));
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"ActivatedKeyboardMode", 21)) == CSTR_EQUAL)
					CConfig::SetActivatedKeyboardMode((CStringRange::Compare(_locale, valueStrings.GetAt(0), &value.Set(L"1", 1)) == CSTR_EQUAL));
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"MakePhrase", 10)) == CSTR_EQUAL)
					CConfig::SetMakePhrase((CStringRange::Compare(_locale, valueStrings.GetAt(0), &value.Set(L"1", 1)) == CSTR_EQUAL));
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"DoHanConvert", 12)) == CSTR_EQUAL)
					CConfig::SetDoHanConvert((CStringRange::Compare(_locale, valueStrings.GetAt(0), &value.Set(L"1", 1)) == CSTR_EQUAL));
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"PhoneticKeyboardLayout", 22)) == CSTR_EQUAL)
					CConfig::setPhoneticKeyboardLayout((PHONETIC_KEYBOARD_LAYOUT) _wtoi(valueStrings.GetAt(0)->Get() ) );
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"ArrayUnicodeScope", 17)) == CSTR_EQUAL)
					CConfig::SetArrayScope((ARRAY_SCOPE)_wtoi(valueStrings.GetAt(0)->Get()));
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"ArrayScope", 10)) == CSTR_EQUAL)
					CConfig::SetArrayScope((ARRAY_SCOPE)_wtoi(valueStrings.GetAt(0)->Get()));
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"NumericPad", 10)) == CSTR_EQUAL)
					CConfig::SetNumericPad((NUMERIC_PAD)_wtoi(valueStrings.GetAt(0)->Get()));
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"ReloadReverseConversion", 23)) == CSTR_EQUAL)
					CConfig::SetReloadReverseConversion((CStringRange::Compare(_locale, valueStrings.GetAt(0), &value.Set(L"1", 1)) == CSTR_EQUAL));
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"ReverseConversionCLSID", 22)) == CSTR_EQUAL)
				{
					BSTR pbstr;
					pbstr = SysAllocStringLen( valueStrings.GetAt(0)->Get(), (UINT) valueStrings.GetAt(0)->GetLength());
					CLSID clsid;
					if(SUCCEEDED(CLSIDFromString(pbstr, &clsid)))
						CConfig::SetReverseConverstionCLSID(clsid);
					SysFreeString(pbstr);
				}
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"ReverseConversionGUIDProfile", 28)) == CSTR_EQUAL)
				{
					BSTR pbstr;
					pbstr = SysAllocStringLen( valueStrings.GetAt(0)->Get(), (UINT) valueStrings.GetAt(0)->GetLength());
					CLSID clsid;
					if(SUCCEEDED(CLSIDFromString(pbstr, &clsid)))
						CConfig::SetReverseConversionGUIDProfile(clsid);
					SysFreeString(pbstr);
				}
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"ReverseConversionDescription", 28)) == CSTR_EQUAL)
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
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"CustomTablePriority", 20)) == CSTR_EQUAL)
					CConfig::setCustomTablePriority((CStringRange::Compare(_locale, valueStrings.GetAt(0), &value.Set(L"1", 1)) == CSTR_EQUAL));
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"FontFaceName", 12)) == CSTR_EQUAL)
				{
					WCHAR *pwszFontFaceName = new (std::nothrow) WCHAR[LF_FACESIZE];
					assert(valueStrings.GetAt(0)->GetLength() < LF_FACESIZE-1);
					StringCchCopyN(pwszFontFaceName, LF_FACESIZE, valueStrings.GetAt(0)->Get(), valueStrings.GetAt(0)->GetLength());
					CConfig::SetFontFaceName(pwszFontFaceName);
				}
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"LoadTableMode", 13)) == CSTR_EQUAL)
					CConfig::SetLoadTableMode((CStringRange::Compare(_locale, valueStrings.GetAt(0), &value.Set(L"1", 1)) == CSTR_EQUAL));
				goto FindNextLine;
			}
			else if (_searchMode == SEARCH_MODE::SEARCH_CONTROLKEY && controlKeyType == CONTROLKEY_TYPE::CIN_CONTROLKEY)  // get value of cin control keys
			{
				CParserStringRange testKey, value;
				if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"%autoCompose", 12)) == CSTR_EQUAL)
				{
					CConfig::SetAutoCompose((CStringRange::Compare(_locale, valueStrings.GetAt(0), &value.Set(L"1", 1)) == CSTR_EQUAL));
				}
				else if (parseConfig && CStringRange::Compare(_locale, &keyword, &testKey.Set(L"%keyname", 8)) == CSTR_EQUAL)
				{
					if (CStringRange::Compare(_locale, valueStrings.GetAt(0), &value.Set(L"begin", 5)) == CSTR_EQUAL)
					{
						_searchMode = SEARCH_MODE::SEARCH_RADICAL;
						initialRadialIndexMap(pRadicalIndexMap);
					}
					else if (CStringRange::Compare(_locale, valueStrings.GetAt(0), &value.Set(L"end", 3)) == CSTR_EQUAL)
						_searchMode = SEARCH_MODE::SEARCH_NONE;
				}
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"%chardef", 8)) == CSTR_EQUAL)
				{
					if (CStringRange::Compare(_locale, valueStrings.GetAt(0), &value.Set(L"begin", 5)) == CSTR_EQUAL)
					{
						if (!parseConfig)
						{
							if (isTextSearch)
							{
								if (Global::hasCINPhraseSection) _searchMode = SEARCH_MODE::SEARCH_NONE;
								else _searchMode = SEARCH_MODE::SEARCH_TEXT;
							}
							else
							{
								_searchMode = SEARCH_MODE::SEARCH_MAPPING;
								lastInitial = '\00';
							}
						}
						else
						{
							_searchMode = cinSorted ? SEARCH_MODE::SEARCH_MAPPING : SEARCH_MODE::SEARCH_NONE;
							lastInitial = '\00';

						}
					}
					else if (CStringRange::Compare(_locale, valueStrings.GetAt(0), &value.Set(L"end", 3)) == CSTR_EQUAL)
						_searchMode = SEARCH_MODE::SEARCH_NONE;
				}
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"%selkey", 7)) == CSTR_EQUAL)
				{				
					size_t selkeylen= valueStrings.GetAt(0)->GetLength();
					assert(selkeylen <= MAX_CAND_SELKEY);
					StringCchCopyN(pSelkey, MAX_CAND_SELKEY, valueStrings.GetAt(0)->Get(), selkeylen);
					debugPrint(L"selkey  = %s, len = %d", pSelkey, selkeylen);

				}
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"%endkey", 7)) == CSTR_EQUAL)
				{
					size_t endkeylen = valueStrings.GetAt(0)->GetLength();
					assert(endkeylen <= MAX_CAND_SELKEY);
					StringCchCopyN(pEndkey, MAX_CAND_SELKEY, valueStrings.GetAt(0)->Get(), endkeylen);
					debugPrint(L"endkey  = %s, len = %d", pEndkey, endkeylen);
				}
				else if (CStringRange::Compare(_locale, &keyword, &testKey.Set(L"%sorted", 7)) == CSTR_EQUAL)
				{
					if (CStringRange::Compare(_locale, valueStrings.GetAt(0), &value.Set(L"1", 1)) == CSTR_EQUAL){
						cinSorted = true;
					}
				}

				controlKeyType = CONTROLKEY_TYPE::NOT_CONTROLKEY;
				goto FindNextLine;
			}
			else if (parseConfig && cinSorted && pRadicalIndexMap != nullptr && _searchMode == SEARCH_MODE::SEARCH_MAPPING)
			{  // build radial index map here.
				WCHAR currentInitial = *keyword.Get();
				if (currentInitial != lastInitial)
				{
					// Replace assert with runtime bounds checking
					if (pRadicalIndexMap->size() >= MAX_RADICAL)
					{
						debugPrint(L"Warning: RadicalIndexMap size exceeds MAX_RADICAL limit, skipping entry");
						goto FindNextLine;
					}
					(*pRadicalIndexMap)[towupper(currentInitial)] = indexTrace;
				}

				lastInitial = currentInitial;
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
	if (dwTotalBufLen == 0 )// End of file
	{
		indexTrace += bufLenOneLine;
		_charIndex += indexTrace;

		if ( !parseConfig && !isFound && *ppdret)
		{
			delete *ppdret;
			*ppdret = nullptr;
		}
		if (parseConfig)
		{
			return cinSorted;
		}
		else
		{
			return (isFound ? TRUE : FALSE);        
		}
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



void CDictionarySearch::initialRadialIndexMap(_Inout_ _T_RadicalIndexMap* pRadicalIndexMap)
{
	if (pRadicalIndexMap)
	{
		if (pRadicalIndexMap->size())
		{
			pRadicalIndexMap->clear();
		}
	}
}

void CDictionarySearch::setSearchOffset(DWORD_PTR offset)
{
	_charIndex = offset;
	_searchMode = SEARCH_MODE::SEARCH_MAPPING;
}