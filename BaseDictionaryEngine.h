//
//
// Derived from Microsoft Sample IME by Jeremy '13,7,17
//
//



#pragma once

#include "File.h"
#include "TSFDayiBaseStructure.h"

class CBaseDictionaryEngine
{
public:
    CBaseDictionaryEngine(LCID locale, _In_ CFile *pDictionaryFile, _In_ WCHAR keywordDelimiter);
    virtual ~CBaseDictionaryEngine();

    virtual VOID CollectWord(_In_ CStringRange *psrgKeyCode, _Out_ CTSFDayiArray<CStringRange> *pasrgWordString)
    { 
        psrgKeyCode; 
        pasrgWordString = nullptr;
    }

    virtual VOID CollectWord(_In_ CStringRange *psrgKeyCode, _Out_ CTSFDayiArray<CCandidateListItem> *pItemList)
    { 
        psrgKeyCode;
        pItemList = nullptr;
    }

    virtual VOID SortListItemByFindKeyCode(_Inout_ CTSFDayiArray<CCandidateListItem> *pItemList);

protected:
    CFile* _pDictionaryFile;
    LCID _locale;
	WCHAR _keywordDelimiter;
private:
    VOID MergeSortByFindKeyCode(_Inout_ CTSFDayiArray<CCandidateListItem> *pItemList, int leftRange, int rightRange);
    int CalculateCandidateCount(int leftRange,  int rightRange);
};
