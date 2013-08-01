//
//
// Derived from Microsoft Sample IME by Jeremy '13,7,17
//
//


#pragma once

#include "BaseDictionaryEngine.h"

class CTableDictionaryEngine : public CBaseDictionaryEngine
{
public:
    CTableDictionaryEngine(LCID locale, _In_ CFile *pDictionaryFile, _In_ WCHAR keywordDelimiter) : CBaseDictionaryEngine(locale, pDictionaryFile, keywordDelimiter) { }
    virtual ~CTableDictionaryEngine() { }

    // Collect word from phrase string.
    // param
    //     [in] psrgKeyCode - Specified key code pointer
    //     [out] pasrgWordString - Specified returns pointer of word as CStringRange.
    // returns
    //     none.
    VOID CollectWord(_In_ CStringRange *pKeyCode, _Inout_ CTSFDayiArray<CStringRange> *pWordStrings);
    VOID CollectWord(_In_ CStringRange *pKeyCode, _Inout_ CTSFDayiArray<CCandidateListItem> *pItemList);

    VOID CollectWordForWildcard(_In_ CStringRange *psrgKeyCode, _Inout_ CTSFDayiArray<CCandidateListItem> *pItemList);

	VOID CollectWordFromConvertedString(_In_ CStringRange *pString, _Inout_ CTSFDayiArray<CCandidateListItem> *pItemList);
    VOID CollectWordFromConvertedStringForWildcard(_In_ CStringRange *pString, _Inout_ CTSFDayiArray<CCandidateListItem> *pItemList);
	VOID ParseConfig();
};
