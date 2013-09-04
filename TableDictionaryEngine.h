//
//
// Derived from Microsoft Sample IME by Jeremy '13,7,17
//
//
#ifndef TABLEDICTIONARYENGLINE_H
#define TABLEDICTIONARYENGLINE_H
#pragma once

#include "File.h"
#include "BaseStructure.h"

class CTSFTTS;
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
    VOID CollectWord(_In_ CStringRange *pKeyCode, _Inout_ CTSFTTSArray<CStringRange> *pWordStrings);
    VOID CollectWord(_In_ CStringRange *pKeyCode, _Inout_ CTSFTTSArray<CCandidateListItem> *pItemList);

    VOID CollectWordForWildcard(_In_ CStringRange *psrgKeyCode, _Inout_ CTSFTTSArray<CCandidateListItem> *pItemList);

	VOID CollectWordFromConvertedString(_In_ CStringRange *pString, _Inout_ CTSFTTSArray<CCandidateListItem> *pItemList);
    VOID CollectWordFromConvertedStringForWildcard(_In_ CStringRange *pString, _Inout_ CTSFTTSArray<CCandidateListItem> *pItemList);
	VOID ParseConfig(IME_MODE imeMode);
	VOID SetSearchSection(SEARCH_SECTION searchSection) { _searchSection =searchSection;}

	VOID SortListItemByFindKeyCode(_Inout_ CTSFTTSArray<CCandidateListItem> *pItemList);

	_T_RacialMap* GetRadicalMap() { return _pRadicalMap;};
	DICTIONARY_TYPE GetDictionaryType() { return _dictionaryType;};
private:
	CFile* _pDictionaryFile;
    LCID _locale;
	WCHAR _keywordDelimiter;
	CTSFTTS *_pTextService;
	SEARCH_SECTION _searchSection;
	DICTIONARY_TYPE _dictionaryType;

	_T_RacialMap* _pRadicalMap;

	VOID MergeSortByFindKeyCode(_Inout_ CTSFTTSArray<CCandidateListItem> *pItemList, int leftRange, int rightRange);

};
#endif