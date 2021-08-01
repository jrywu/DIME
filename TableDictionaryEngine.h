//
//
// Derived from Microsoft Sample IME by Jeremy '13,7,17
//
//
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
	virtual ~CTableDictionaryEngine(){}
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