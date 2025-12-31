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



#pragma once

#include "Globals.h"

//////////////////////////////////////////////////////////////////////
//

class CParserStringRange : public CStringRange
{
public:
    CParserStringRange() : CStringRange()
    {
        _fEscapeIncluded = FALSE;
    }

    BOOL _fEscapeIncluded;         // flag. This string range included escape sequence.
};

//////////////////////////////////////////////////////////////////////
//

class CDictionaryParser
{
public:
    CDictionaryParser(LCID locale, _In_ WCHAR keywordDelimiter);
    virtual ~CDictionaryParser();

    BOOL ParseLine(_In_reads_(dwBufLen) LPCWSTR pwszBuffer, DWORD_PTR dwBufLen, _Out_ CParserStringRange *psrgKeyword, 
		_Inout_opt_ CDIMEArray<CParserStringRange> *pValue = nullptr, _In_opt_ BOOL ttsPhraseMode = FALSE, _In_opt_ CStringRange *searchText = NULL );

    // dwBufLen - in character count
    _Ret_maybenull_
    LPCWSTR GetToken(_In_reads_(dwBufLen) LPCWSTR pwszBuffer, DWORD_PTR dwBufLen, _In_ const WCHAR chDelimiter, _Out_ CParserStringRange *srgKeyWord);

protected:
    BOOL RemoveWhiteSpaceFromBegin(_Inout_opt_ CStringRange *pString);
    BOOL RemoveWhiteSpaceFromEnd(_Inout_opt_ CStringRange *pString);
    BOOL RemoveStringDelimiter(_Inout_opt_ CStringRange *pString);

    DWORD_PTR GetOneLine(_In_z_ LPCWSTR pwszBuffer, DWORD_PTR dwBufLen);

    LCID _locale;   // used for CompareString

	WCHAR _keywordDelimiter;
};
