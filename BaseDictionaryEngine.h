//
//
// Derived from Microsoft Sample IME by Jeremy '13,7,17
//
//
#ifndef BASEDICTINARYENGINE_H
#define BASEDICTINARYENGINE_H


#pragma once

#include "File.h"
#include "BaseStructure.h"


class CBaseDictionaryEngine
{
public:
	CBaseDictionaryEngine(LCID locale, _In_ CFile *pDictionaryFile, _In_ WCHAR keywordDelimiter);
    virtual ~CBaseDictionaryEngine();

    virtual VOID CollectWord(_In_ CStringRange *psrgKeyCode, _Out_ CTSFTTSArray<CStringRange> *pasrgWordString)
    { 
        psrgKeyCode; 
        pasrgWordString = nullptr;
    }

    virtual VOID CollectWord(_In_ CStringRange *psrgKeyCode, _Out_ CTSFTTSArray<CCandidateListItem> *pItemList)
    { 
        psrgKeyCode;
        pItemList = nullptr;
    }

    virtual VOID SortListItemByFindKeyCode(_Inout_ CTSFTTSArray<CCandidateListItem> *pItemList);

protected:
    CFile* _pDictionaryFile;
    LCID _locale;
	WCHAR _keywordDelimiter;

private:
    VOID MergeSortByFindKeyCode(_Inout_ CTSFTTSArray<CCandidateListItem> *pItemList, int leftRange, int rightRange);
    int CalculateCandidateCount(int leftRange,  int rightRange);
};
#endif