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


#include "Private.h"
#include "DictionaryParser.h"
#include "BaseStructure.h"

//---------------------------------------------------------------------
//
// ctor
//
//---------------------------------------------------------------------

CDictionaryParser::CDictionaryParser(LCID locale, _In_ WCHAR keywordDelimiter)
{
    _locale = locale;
	_keywordDelimiter = keywordDelimiter;
}

//---------------------------------------------------------------------
//
// dtor
//
//---------------------------------------------------------------------

CDictionaryParser::~CDictionaryParser()
{
}

//---------------------------------------------------------------------
//
// ParseLine
//
// dwBufLen - in character count
//
//---------------------------------------------------------------------

BOOL CDictionaryParser::ParseLine(_In_reads_(dwBufLen) LPCWSTR pwszBuffer, DWORD_PTR dwBufLen, _Out_ CParserStringRange *psrgKeyword, 
								  _Inout_opt_ CDIMEArray<CParserStringRange> *pValue, _In_opt_ BOOL ttsPhraseSearch, _In_opt_ CStringRange *searchText)
{
    LPCWSTR pwszKeyWordDelimiter = nullptr;
    pwszKeyWordDelimiter = GetToken(pwszBuffer, dwBufLen, _keywordDelimiter, psrgKeyword);
	if (!(pwszKeyWordDelimiter))
	{
		return FALSE;    // no delimiter found in line.
	}

    dwBufLen -= (pwszKeyWordDelimiter - pwszBuffer);
    pwszBuffer = pwszKeyWordDelimiter + 1;
    dwBufLen--;

    // Get value.
    if (pValue)
    {
        if (dwBufLen)
        {
			CParserStringRange localKeyword;
			if(ttsPhraseSearch)
			{
				
				do{
					pwszKeyWordDelimiter = GetToken(pwszBuffer, dwBufLen,L',', &localKeyword);
					CParserStringRange* psrgValue = pValue->Append();
					if (!psrgValue) 
						return FALSE;

					PWCHAR pwch = new (std::nothrow) WCHAR[psrgKeyword->GetLength() + localKeyword.GetLength() + 2];
					if (!pwch)   continue;
    				
					
					if ((pwszKeyWordDelimiter))
					{						
						dwBufLen -= (pwszKeyWordDelimiter - pwszBuffer);
						pwszBuffer = pwszKeyWordDelimiter + 1;
						dwBufLen--;

						StringCchCopyN(pwch, psrgKeyword->GetLength() + localKeyword.GetLength() + 2, localKeyword.Get(),localKeyword.GetLength()); 
						psrgValue->Set(pwch, wcslen(pwch));
						RemoveWhiteSpaceFromBegin(psrgValue);
						RemoveWhiteSpaceFromEnd(psrgValue);
						RemoveStringDelimiter(psrgValue);
					}else{ //end of line. append the last item

						localKeyword.Set(pwszBuffer, dwBufLen);
						RemoveWhiteSpaceFromBegin(&localKeyword);
						RemoveWhiteSpaceFromEnd(&localKeyword);
						RemoveStringDelimiter(&localKeyword);
						StringCchCopyN(pwch, psrgKeyword->GetLength() + localKeyword.GetLength() + 2, localKeyword.Get(),localKeyword.GetLength()); 
						psrgValue->Set(pwch, wcslen(pwch));
					}
					
					
				}while(pwszKeyWordDelimiter);
			}else
			{
				CParserStringRange* psrgValue = pValue->Append();
				if (!psrgValue)
					return FALSE;
				if(searchText == NULL)
				{
					psrgValue->Set(pwszBuffer, dwBufLen);
				}
				else
				{
					DWORD_PTR searchTextLen;
					searchTextLen =  searchText->GetLength();
					localKeyword.Set(pwszBuffer, dwBufLen);
					RemoveWhiteSpaceFromBegin(&localKeyword);
					RemoveWhiteSpaceFromEnd(&localKeyword);
					RemoveStringDelimiter(&localKeyword);
					if(searchTextLen >1)
						searchTextLen--;//search text always in the form xxx* here.  remove the * wildcard  in last word.
					if(localKeyword.GetLength() == searchTextLen)//the first one is itself, thus dwbuflen = searchTextLen here
						psrgValue->Set(localKeyword.Get(), localKeyword.GetLength());
					else
						psrgValue->Set(localKeyword.Get() + searchTextLen, localKeyword.GetLength() - searchTextLen);
					
				}
				RemoveWhiteSpaceFromBegin(psrgValue);
				RemoveWhiteSpaceFromEnd(psrgValue);
				RemoveStringDelimiter(psrgValue);
			}
        }
    }

    return TRUE;
}

//---------------------------------------------------------------------
//
// GetToken
//
// dwBufLen - in character count
//
// return   - pointer of delimiter which specified chDelimiter
//
//---------------------------------------------------------------------
_Ret_maybenull_
LPCWSTR CDictionaryParser::GetToken(_In_reads_(dwBufLen) LPCWSTR pwszBuffer, DWORD_PTR dwBufLen, _In_ const WCHAR chDelimiter, _Out_ CParserStringRange *psrgValue)
{
    WCHAR ch = '\0';
	WCHAR pch = '\0';

    psrgValue->Set(pwszBuffer, dwBufLen);

    ch = *pwszBuffer;
	

    while ((ch) && (ch != chDelimiter) && dwBufLen)
    {
        dwBufLen--;
        pwszBuffer++;
		
        if ((ch == Global::StringDelimiter) && (pch != L'\\')) // ignore escaped quote \\" which is not a quote acutally
        {
            while (*pwszBuffer && (*pwszBuffer != Global::StringDelimiter || (*(pwszBuffer-1) == L'\\')&&(*(pwszBuffer-2) != L'\\')) && dwBufLen)
            {
                dwBufLen--;
                pwszBuffer++;

            }
            if (*pwszBuffer && dwBufLen)
            {
                dwBufLen--;
                pwszBuffer++;
            }
            else
            {
                return nullptr;
            }
        }
        ch = *pwszBuffer;
		pch = *(pwszBuffer-1);
    }

    if (*pwszBuffer && dwBufLen)
    {
        LPCWSTR pwszStart = psrgValue->Get();

        psrgValue->Set(pwszStart, pwszBuffer - pwszStart);

        RemoveWhiteSpaceFromBegin(psrgValue);
        RemoveWhiteSpaceFromEnd(psrgValue);
        RemoveStringDelimiter(psrgValue);

        return pwszBuffer;
    }

    RemoveWhiteSpaceFromBegin(psrgValue);
    RemoveWhiteSpaceFromEnd(psrgValue);
    RemoveStringDelimiter(psrgValue);

    return nullptr;
}

//---------------------------------------------------------------------
//
// RemoveWhiteSpaceFromBegin
// RemoveWhiteSpaceFromEnd
// RemoveStringDelimiter
//
//---------------------------------------------------------------------

BOOL CDictionaryParser::RemoveWhiteSpaceFromBegin(_Inout_opt_ CStringRange *pString)
{
    DWORD_PTR dwIndexTrace = 0;  // in char

    if (pString == nullptr)
    {
        return FALSE;
    }

    if (SkipWhiteSpace(_locale, pString->Get(), pString->GetLength(), &dwIndexTrace) != S_OK)
    {
        return FALSE;
    }
	
	pString->Set(pString->Get() + dwIndexTrace, pString->GetLength() - dwIndexTrace);
	return TRUE;
	
}

BOOL CDictionaryParser::RemoveWhiteSpaceFromEnd(_Inout_opt_ CStringRange *pString)
{
    if (pString == nullptr)
    {
        return FALSE;
    }

    DWORD_PTR dwTotalBufLen = pString->GetLength();
    LPCWSTR pwszEnd = pString->Get() + dwTotalBufLen - 1;

    while (dwTotalBufLen && (IsSpace(_locale, *pwszEnd) || *pwszEnd == L'\r' || *pwszEnd == L'\n'))
    {
        pwszEnd--;
        dwTotalBufLen--;
    }

    pString->Set(pString->Get(), dwTotalBufLen);
    return TRUE;
}

BOOL CDictionaryParser::RemoveStringDelimiter(_Inout_opt_ CStringRange *pString)
{
    if (pString == nullptr)
    {
        return FALSE;
    }

    if (pString->GetLength() >= 2)
    {
        if ((*pString->Get() == Global::StringDelimiter) && (*(pString->Get()+pString->GetLength()-1) == Global::StringDelimiter))
        {
            pString->Set(pString->Get()+1, pString->GetLength()-2);
			CParserStringRange localKeyword;
			LPCWSTR pwszKeyWordDelimiter = nullptr;
			pwszKeyWordDelimiter = GetToken(pString->Get(), pString->GetLength(),L'\\', &localKeyword);//check for backslash '\\' escape code and remove then.
			if(pwszKeyWordDelimiter)
			{	
				PWCHAR pwchNoEscape = new (std::nothrow) WCHAR[pString->GetLength() + 1];
				if (pwchNoEscape == nullptr) return FALSE;
				*pwchNoEscape = L'\0';
				const WCHAR *pwch = pString->Get();
				DWORD_PTR index = 0;
				for(UINT i=0;i < pString->GetLength(); i++)
				{
					WCHAR wch = pwch[i];
					if(wch == L'\\')
					{
						if(i > 0 && (pwch[i-1] == L'\\'))
						{
							StringCchCatN(pwchNoEscape, pString->GetLength() + 1, L"\\", 1); 
							index ++;
						}
					}
					else
					{
						StringCchCatN(pwchNoEscape, pString->GetLength() + 1, &wch, 1); 					
						index ++;
					}
				}
				pString->Set(pwchNoEscape, index);
			}
			return TRUE;
        }
    }

    return FALSE;
}

//---------------------------------------------------------------------
//
// GetOneLine
//
// dwBufLen - in character count
//
//---------------------------------------------------------------------

DWORD_PTR CDictionaryParser::GetOneLine(_In_z_ LPCWSTR pwszBuffer, DWORD_PTR dwBufLen)
{
    DWORD_PTR dwIndexTrace = 0;     // in char

	if(pwszBuffer == nullptr) return 0;

	while(dwIndexTrace <dwBufLen)
	{
		if (*(pwszBuffer + dwIndexTrace) == L'\r' || *(pwszBuffer + dwIndexTrace) == L'\n' || *(pwszBuffer + dwIndexTrace) == L'\0')
		{
			return dwIndexTrace;
		}
		dwIndexTrace++;
	}


    return dwIndexTrace;
}
