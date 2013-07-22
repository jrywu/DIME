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

//+---------------------------------------------------------------------------
//
// FindWorker
//
//----------------------------------------------------------------------------

BOOL CDictionarySearch::FindWorker(BOOL isTextSearch, _Out_ CDictionaryResult **ppdret, BOOL isWildcardSearch)
{
    DWORD_PTR dwTotalBufLen = GetBufferInWCharLength();        // in char
    if (dwTotalBufLen == 0)
    {
        return FALSE;
    }


    const WCHAR *pwch = GetBufferInWChar();
    DWORD_PTR indexTrace = 0;     // in char
    *ppdret = nullptr;
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
				controlKey.Set(L"[Text]", 6);	
				if (CStringRange::Compare(_locale, &keyword, &controlKey) == CSTR_EQUAL)
				{ // in Text block
					searchMapping = TRUE;
					Global::hasPhraseSection = FALSE;
					searchTTSPhrase = FALSE;
				}else
				{
					controlKey.Set(L"[Phrase]", 8);	
					if (CStringRange::Compare(_locale, &keyword, &controlKey) == CSTR_EQUAL)
					{ // in Phrase block
						searchMapping = FALSE;
						Global::hasPhraseSection = TRUE;
						searchTTSPhrase = TRUE;
					}
					else
					{
						controlKey.Set(L"[AutoCompose]", 13);	
						if (CStringRange::Compare(_locale, &keyword, &controlKey) == CSTR_EQUAL)
							Global::autoCompose = TRUE;// autoCompose is off for TTS table unless see [AutoCompose] control key
						controlKey.Set(L"[Radical]", 9);	
						if (CStringRange::Compare(_locale, &keyword, &controlKey) == CSTR_EQUAL)
							searchRadical = TRUE; // retrive the [Radical] Mapping Section.
						
						searchMapping = FALSE;
						Global::hasPhraseSection = TRUE;
						searchTTSPhrase = FALSE;
					}
			

				
				}
			ttsControlKeyFound = FALSE;
			goto FindNextLine;
			}
			
		}
		else if (((!isTextSearch) && searchMapping) //Oridanary mode
			|| (isTextSearch && searchTTSPhrase))  // search TTS [Phrase] section mode
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
		else 
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

        if (bufLen)
        {
            delete [] pText;
        }

		if(searchMapping || searchTTSPhrase)
		{
			// Prepare return's CDictionaryResult
			*ppdret = new (std::nothrow) CDictionaryResult();
			if (!*ppdret)
			{
				return FALSE;
			}

			CTSFDayiArray<CParserStringRange> valueStrings;
			if (!ParseLine(&pwch[indexTrace], bufLenOneLine, &keyword, &valueStrings, searchTTSPhrase))
			{
				if (*ppdret)
				{
					delete *ppdret;
					*ppdret = nullptr;
				}
				if(!cinControlKeyFound)
					return FALSE;
			}
			if(cinControlKeyFound)  // get value of cin control keys
			{
				CParserStringRange testKey;
				WCHAR* testKeyText= L"%autoCompose";testKey.Set(testKeyText, wcslen(testKeyText));
				if (CStringRange::Compare(_locale, &keyword, &testKey) == CSTR_EQUAL)
				{
					testKeyText = L"TRUE";testKey.Set(testKeyText, wcslen(testKeyText));
					Global::autoCompose = (CStringRange::Compare(_locale, valueStrings.GetAt(0), &testKey) == CSTR_EQUAL);
				}

				cinControlKeyFound = FALSE;
				goto FindNextLine;
			}

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

        if (!isFound && *ppdret)
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
