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
	BOOL cinControlKeyFound = FALSE;
	BOOL ttsControlKeyFound = FALSE;
	BOOL isFound = FALSE;
	DWORD_PTR bufLenOneLine = 0;

	BOOL searchMapping = TRUE;
	BOOL searchTTSPhrase = FALSE;
	BOOL searchRadical = FALSE;

TryAgain:
	bufLenOneLine = GetOneLine(&pwch[indexTrace], dwTotalBufLen);
	if (bufLenOneLine == 0)
	{
		goto FindNextLine;
	}
	else
	{
		CParserStringRange keyword;
		DWORD_PTR bufLen = 0;
		LPWSTR pText = nullptr;



		WCHAR ch = pwch[indexTrace];
		switch (ch)
		{
		case (L'#'): // comment lines begins with #
			goto FindNextLine;
		case (L'%'): // .cin control key begins with %
			Global::KeywordDelimiter = L'\t';  // set delimiter to tab for reading .cin files.
			cinControlKeyFound = TRUE;
			break;
		case (L'['):
			ttsControlKeyFound = TRUE;
			break;
		}
		if (!ParseLine(&pwch[indexTrace], bufLenOneLine, &keyword))
		{
			if(!(cinControlKeyFound||ttsControlKeyFound))
				return FALSE;    // error
		}

		if (cinControlKeyFound||ttsControlKeyFound) 
		{	
			CParserStringRange controlKey;
			if(cinControlKeyFound)
				controlKey.Set(L"%*", 2);	
			else if(ttsControlKeyFound)
				controlKey.Set(L"[*", 2);
			CStringRange::WildcardCompare(_locale, &controlKey, &keyword); // read the .cin or tts control key
			// read cin control key here.
			if(ttsControlKeyFound)
			{	
				if ( CStringRange::Compare(_locale, &keyword, &controlKey.Set(L"[Text]", 6)) == CSTR_EQUAL)
				{ // in Text block
					searchRadical = FALSE;
					searchMapping = !parseConfig;
					searchTTSPhrase = FALSE;
				}
				else if (CStringRange::Compare(_locale, &keyword, &controlKey.Set(L"[Phrase]", 8)) == CSTR_EQUAL)
				{ // in Phrase block
					searchRadical = FALSE;
					searchMapping = FALSE;
					if(parseConfig)
						Global::hasPhraseSection = TRUE;
					searchTTSPhrase = !parseConfig;
				}
				else if (parseConfig && CStringRange::Compare(_locale, &keyword, &controlKey.Set(L"[Radical]", 9)) == CSTR_EQUAL)
				{
					searchRadical = TRUE; // retrive the [Radical] Mapping Section.
					searchMapping = FALSE;
					searchTTSPhrase = FALSE;
				}
				else if (parseConfig && CStringRange::Compare(_locale, &keyword, &controlKey.Set(L"[AutoCompose]", 13)) == CSTR_EQUAL)
				{
					Global::autoCompose = TRUE;// autoCompose is off for TTS table unless see [AutoCompose] control key
					searchRadical = FALSE;
					searchMapping = FALSE;
					searchTTSPhrase = FALSE;
				}
				else
				{
					searchRadical = FALSE;
					searchMapping = FALSE;
					searchTTSPhrase = FALSE;
				}
				ttsControlKeyFound = FALSE;
				goto FindNextLine;
			}
			else if(cinControlKeyFound)
			{
				
				if (parseConfig && CStringRange::WildcardCompare(_locale, &controlKey.Set(L"%autoCompose*", 13), &keyword))
				{
					searchRadical = FALSE;
					searchMapping = TRUE;
					cinControlKeyFound = TRUE;
					goto ReadValue;
				}
				else if (CStringRange::WildcardCompare(_locale, &controlKey.Set(L"%chardef?begin", 14), &keyword))
				{
					searchRadical = FALSE;
					searchMapping = !parseConfig;
				}
				else if (CStringRange::WildcardCompare(_locale, &controlKey.Set(L"%chardef?end", 12),&keyword))
				{
					searchRadical = FALSE;
					searchMapping = FALSE;
				}
				else if (parseConfig && CStringRange::WildcardCompare(_locale, &controlKey.Set(L"%keyname?begin", 14), &keyword))
				{
					searchMapping = FALSE;
					searchRadical = TRUE;
				}
				else if (parseConfig && CStringRange::WildcardCompare(_locale, &controlKey.Set(L"%keyname?end", 14), &keyword))
				{
					searchMapping = FALSE;
					searchRadical = FALSE;
				}
				else
				{
					searchRadical = FALSE;
					searchMapping = FALSE;
				}
				cinControlKeyFound = FALSE;
				goto FindNextLine;

				

			}
			goto FindNextLine;
		}
		else if(searchRadical)
		{
			goto ReadValue;
		}
		else if (((!parseConfig) && (!isTextSearch) && (searchMapping||searchRadical)) //Oridanary mode
			|| ((!parseConfig) && isTextSearch && searchTTSPhrase))  // search TTS [Phrase] section mode
		{
			// Compare Dictionary key code and input key code
			if (!isWildcardSearch)
			{
				if (CStringRange::Compare(_locale, &keyword, _pSearchKeyCode) != CSTR_EQUAL)
				{
					if (bufLen)
					{
						delete [] pText;
					}
					goto FindNextLine;
				}
			}
			else
			{
				// Wildcard search
				if (!CStringRange::WildcardCompare(_locale, _pSearchKeyCode, &keyword))
				{
					if (bufLen)
					{
						delete [] pText;
					}
					goto FindNextLine;
				}
			}
		}
		else if(!parseConfig)
		{
			// Compare Dictionary converted string and input string
			CTSFDayiArray<CParserStringRange> convertedStrings;
			if (!ParseLine(&pwch[indexTrace], bufLenOneLine, &keyword, &convertedStrings))
			{
				if (bufLen)
				{
					delete [] pText;
				}
				return FALSE;
			}
			if (convertedStrings.Count() == 1)
			{
				CStringRange* pTempString = convertedStrings.GetAt(0);

				if (!isWildcardSearch)
				{
					if (CStringRange::Compare(_locale, pTempString, _pSearchKeyCode) != CSTR_EQUAL)
					{
						if (bufLen)
						{
							delete [] pText;
						}
						goto FindNextLine;
					}
				}
				else
				{
					// Wildcard search
					if (!CStringRange::WildcardCompare(_locale, _pSearchKeyCode, pTempString))
					{
						if (bufLen)
						{
							delete [] pText;
						}
						goto FindNextLine;
					}
				}
			}
			else
			{
				if (bufLen)
				{
					delete [] pText;
				}
				goto FindNextLine;
			}
		}
		else  //bypassing all lines for all lines except the radical section for pars 
		{
			goto FindNextLine;
		}

		if (bufLen)
		{
			delete [] pText;
		}
ReadValue:
		if(searchMapping || searchTTSPhrase || searchRadical)
		{
			// Prepare return's CDictionaryResult
			if(!parseConfig)
			{
				*ppdret = new (std::nothrow) CDictionaryResult();
				if (!*ppdret)
				{
					return FALSE;
				}
			}

			CTSFDayiArray<CParserStringRange> valueStrings;

			if (!ParseLine(&pwch[indexTrace], bufLenOneLine, &keyword, &valueStrings, searchTTSPhrase,
				(isTextSearch&&(!(cinControlKeyFound||searchRadical)))?_pSearchKeyCode:NULL))
			{
				if (!parseConfig && *ppdret)
				{
					delete *ppdret;
					*ppdret = nullptr;
				}
				if(!cinControlKeyFound)
					return FALSE;
			}
			if(searchRadical)
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
			if(cinControlKeyFound)  // get value of cin control keys
			{
				CParserStringRange testKey, value;
				
				
				testKey.Set(L"%autoCompose", 12);
				if (CStringRange::Compare(_locale, &keyword, &testKey) == CSTR_EQUAL)
				{
					value.Set(L"TRUE", 4);
					Global::autoCompose = (CStringRange::Compare(_locale, valueStrings.GetAt(0), &value) == CSTR_EQUAL);
				}

				cinControlKeyFound = FALSE;
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
