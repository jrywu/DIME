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
#ifndef TABLEDICTIONARYENGLINE_H
#define TABLEDICTIONARYENGLINE_H
#pragma once

#include "File.h"
#include "Define.h"
#include "BaseStructure.h"

class CDIME;
class CTableDictionaryEngine
{
public:
	CTableDictionaryEngine(LCID locale, _In_ CFile *pDictionaryFile, _In_ DICTIONARY_TYPE dictionaryType);
	virtual ~CTableDictionaryEngine()
	{
		if (_pRadicalMap)
		{
			delete _pRadicalMap;
			_pRadicalMap = nullptr;
		}
		if (_pRadicalIndexMap)
		{
			delete _pRadicalIndexMap;
			_pRadicalIndexMap = nullptr;
		}
	}
    // Collect word from phrase string.
    // param
    //     [in] psrgKeyCode - Specified key code pointer
    //     [out] pasrgWordString - Specified returns pointer of word as CStringRange.
    // returns
    //     none.
    VOID CollectWord(_In_ CStringRange *pKeyCode, _Inout_ CDIMEArray<CStringRange> *pWordStrings);
    VOID CollectWord(_In_ CStringRange *pKeyCode, _Inout_ CDIMEArray<CCandidateListItem> *pItemList);

    VOID CollectWordForWildcard(_In_ CStringRange *psrgKeyCode, _Inout_ CDIMEArray<CCandidateListItem> *pItemList, _In_opt_ CTableDictionaryEngine* wordFreqTableDictionaryEngine = nullptr);

	VOID CollectWordFromConvertedString(_In_ CStringRange *pString, _Inout_ CDIMEArray<CCandidateListItem> *pItemList);
    VOID CollectWordFromConvertedStringForWildcard(_In_ CStringRange *pString, _Inout_ CDIMEArray<CCandidateListItem> *pItemList);
	VOID ParseConfig(IME_MODE imeMode);
	VOID SetSearchSection(SEARCH_SECTION searchSection) { _searchSection =searchSection;}

	VOID SortListItemByFindKeyCode(_Inout_ CDIMEArray<CCandidateListItem> *pItemList);
	VOID SortListItemByWordFrequency(_Inout_ CDIMEArray<CCandidateListItem> *pItemList);

	_T_RadicalMap* GetRadicalMap() { return _pRadicalMap;};
	DICTIONARY_TYPE GetDictionaryType() { return _dictionaryType;};

	PWCH GetSelkey() { return _pSelkey; };
	PWCH GetEndkey() { return _pEndkey; };

private:
	CFile* _pDictionaryFile;
    LCID _locale;
	WCHAR _keywordDelimiter;
	CDIME *_pTextService;
	SEARCH_SECTION _searchSection;
	DICTIONARY_TYPE _dictionaryType;

	BOOL _sortedCIN;

	_T_RadicalMap* _pRadicalMap;
	_T_RadicalIndexMap* _pRadicalIndexMap;

	WCHAR _pSelkey[MAX_CAND_SELKEY]=L"";
	WCHAR _pEndkey[MAX_CAND_SELKEY]=L"";

	VOID MergeSortByFindKeyCode(_Inout_ CDIMEArray<CCandidateListItem> *pItemList, int leftRange, int rightRange);
	VOID MergeSortByWordFrequency(_Inout_ CDIMEArray<CCandidateListItem> *pItemList, int leftRange, int rightRange);

};
#endif