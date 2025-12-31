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
#ifndef DICTIONARYSEARCH_H
#define DICTIONARYSEARCH_H


#pragma once

#include "File.h"
#include "DictionaryParser.h"
#include "BaseStructure.h"

class CDictionaryResult;

enum class SEARCH_MODE
{
    SEARCH_NONE,
    SEARCH_MAPPING,
    SEARCH_TEXT,
    SEARCH_PHRASE,
    SEARCH_RADICAL,
    SEARCH_CONFIG,
    SEARCH_CONTROLKEY,
    SEARCH_SYMBOL,
    SEARCH_PRHASE_FROM_KEYSTROKE
};
enum class CONTROLKEY_TYPE
{
    NOT_CONTROLKEY,
    CIN_CONTROLKEY,
    TTS_CONTROLKEY
};

//////////////////////////////////////////////////////////////////////
//
// CDictionarySearch declaration.
//
//////////////////////////////////////////////////////////////////////

class CDIME;
class CDictionarySearch : CDictionaryParser
{
public:
    CDictionarySearch(LCID locale, _In_ CFile *pFile, _In_opt_ CStringRange *pSearchKeyCode, _In_ WCHAR keywordDelimiter);
    virtual ~CDictionarySearch();

    BOOL FindPhrase(_Out_ CDictionaryResult **ppdret);
    BOOL FindPhraseForWildcard(_Out_ CDictionaryResult **ppdret);
	BOOL FindConvertedString(CDictionaryResult **ppdret);
    BOOL FindConvertedStringForWildcard(CDictionaryResult **ppdret);
	BOOL ParseConfig(IME_MODE imeMode, 
        _Inout_opt_ _T_RadicalMap* pRadicalMap = nullptr, _Inout_opt_ _T_RadicalIndexMap* pRadicalIndexMap = nullptr,
        _Inout_opt_ PWCH pSelkey = nullptr, _Inout_opt_ PWCH pEndkey = nullptr);
	VOID SetSearchSection(SEARCH_SECTION searchSection) { _searchSection = searchSection; }

    CStringRange* _pSearchKeyCode;

    DWORD_PTR _charIndex;      // in character. Always point start of line in dictionary file.

	void setSearchOffset(DWORD_PTR offset);
	void setSortedSearchResultFound(BOOL sortedSearchResultFound) { _sortedSearchResultFound = sortedSearchResultFound; }

private:
	SEARCH_SECTION _searchSection;
	BOOL FindWorker(BOOL isTextSearch, _Out_opt_ CDictionaryResult **ppdret, _In_ BOOL isWildcardSearch, _In_ BOOL parseConfig = FALSE
		, _Inout_opt_ _T_RadicalMap* pRadicalMap = nullptr, _Inout_opt_ _T_RadicalIndexMap* pRadicalIndexMap = nullptr
        , _Inout_opt_ PWCH pSelkey = nullptr, _Inout_opt_ PWCH pEndkey = nullptr);

    DWORD_PTR GetBufferInWCharLength()
    {
        return (_pFile->GetFileSize() / sizeof(WCHAR)) - _charIndex;     // in char count as a returned length.
    }

    const WCHAR* GetBufferInWChar(BOOL *fileReloaded)
    {
        return _pFile->GetReadBufferPointer(fileReloaded) + _charIndex;
    }
	void initialRadialIndexMap(_Inout_ _T_RadicalIndexMap* pRadicalIndexMap);

    CFile* _pFile;
	SEARCH_MODE _searchMode;
	BOOL _sortedSearchResultFound;
	CDIME *_pTextService;
	IME_MODE _imeMode;

};

//////////////////////////////////////////////////////////////////////
//
// CDictionaryResult declaration.
//
//////////////////////////////////////////////////////////////////////

class CDictionaryResult
{
public:
    CDictionaryResult() { }
    virtual ~CDictionaryResult() { }

    CDictionaryResult& operator=(CDictionaryResult& dret)
    {
        _FindKeyCode = dret._FindKeyCode;
        _FindPhraseList = dret._FindPhraseList;
        return *this;
    }

    CStringRange _SearchKeyCode;
    CStringRange _FindKeyCode;
    CDIMEArray<CStringRange> _FindPhraseList;
};


#endif