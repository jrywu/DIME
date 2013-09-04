//
//
// Derived from Microsoft Sample IME by Jeremy '13,7,17
//
//


#include "Private.h"
#include "TableDictionaryEngine.h"
#include "DictionarySearch.h"
#include "TSFTTS.h"

CTableDictionaryEngine::CTableDictionaryEngine(LCID locale, _In_ CFile *pDictionaryFile, _In_ WCHAR keywordDelimiter)
{
	_locale = locale;
    _pDictionaryFile = pDictionaryFile;
	_keywordDelimiter = keywordDelimiter;
	_searchSection = SEARCH_SECTION_TEXT;
}

//+---------------------------------------------------------------------------
//
// CollectWord
//
//----------------------------------------------------------------------------

VOID CTableDictionaryEngine::CollectWord(_In_ CStringRange *pKeyCode, _Inout_ CTSFTTSArray<CStringRange> *pWordStrings)
{
    CDictionaryResult* pdret = nullptr;
	CDictionarySearch dshSearch(_locale, _pDictionaryFile, pKeyCode, _keywordDelimiter);
	dshSearch.SetSearchSection(_searchSection);

    while (dshSearch.FindPhrase(&pdret))
    {
        for (UINT index = 0; index < pdret->_FindPhraseList.Count(); index++)
        {
            CStringRange* pPhrase = nullptr;
            pPhrase = pWordStrings->Append();
            if (pPhrase)
            {
                *pPhrase = *pdret->_FindPhraseList.GetAt(index);
            }
        }

        delete pdret;
        pdret = nullptr;
    }
}

VOID CTableDictionaryEngine::CollectWord(_In_ CStringRange *pKeyCode, _Inout_ CTSFTTSArray<CCandidateListItem> *pItemList)
{
    CDictionaryResult* pdret = nullptr;
    CDictionarySearch dshSearch(_locale, _pDictionaryFile, pKeyCode, _keywordDelimiter);
	dshSearch.SetSearchSection(_searchSection);

    while (dshSearch.FindPhrase(&pdret))
    {
        for (UINT iIndex = 0; iIndex < pdret->_FindPhraseList.Count(); iIndex++)
        {
            CCandidateListItem* pLI = nullptr;
            pLI = pItemList->Append();
            if (pLI)
            {
                pLI->_ItemString.Set(*pdret->_FindPhraseList.GetAt(iIndex));
                pLI->_FindKeyCode.Set(pdret->_FindKeyCode.Get(), pdret->_FindKeyCode.GetLength());
            }
        }

        delete pdret;
        pdret = nullptr;
    }
}

//+---------------------------------------------------------------------------
//
// CollectWordForWildcard
//
//----------------------------------------------------------------------------

VOID CTableDictionaryEngine::CollectWordForWildcard(_In_ CStringRange *pKeyCode, _Inout_ CTSFTTSArray<CCandidateListItem> *pItemList)
{
    CDictionaryResult* pdret = nullptr;
    CDictionarySearch dshSearch(_locale, _pDictionaryFile, pKeyCode, _keywordDelimiter);
	dshSearch.SetSearchSection(_searchSection);
    while (dshSearch.FindPhraseForWildcard(&pdret))
    {
        for (UINT iIndex = 0; iIndex < pdret->_FindPhraseList.Count(); iIndex++)
        {
            CCandidateListItem* pLI = nullptr;
            pLI = pItemList->Append();
            if (pLI)
            {
                pLI->_ItemString.Set(*pdret->_FindPhraseList.GetAt(iIndex));
                pLI->_FindKeyCode.Set(pdret->_FindKeyCode.Get(), pdret->_FindKeyCode.GetLength());
            }
        }

        delete pdret;
        pdret = nullptr;
    }
}

//+---------------------------------------------------------------------------
//
// CollectWordFromConvertedStringForWildcard
//
//----------------------------------------------------------------------------

VOID CTableDictionaryEngine::CollectWordFromConvertedStringForWildcard(_In_ CStringRange *pString, _Inout_ CTSFTTSArray<CCandidateListItem> *pItemList)
{
    CDictionaryResult* pdret = nullptr;
	CDictionarySearch dshSearch(_locale, _pDictionaryFile, pString, _keywordDelimiter);
	dshSearch.SetSearchSection(_searchSection);
    while (dshSearch.FindConvertedStringForWildcard(&pdret)) // TAIL ALL CHAR MATCH
    {
        for (UINT index = 0; index < pdret->_FindPhraseList.Count(); index++)
        {
            CCandidateListItem* pLI = nullptr;
            pLI = pItemList->Append();
            if (pLI)
            {
                pLI->_ItemString.Set(*pdret->_FindPhraseList.GetAt(index));
                pLI->_FindKeyCode.Set(pdret->_FindKeyCode.Get(), pdret->_FindKeyCode.GetLength());
            }
        }

        delete pdret;
        pdret = nullptr;
    }
}

//+---------------------------------------------------------------------------
//
// CollectWordFromConvertedString
//
//----------------------------------------------------------------------------

VOID CTableDictionaryEngine::CollectWordFromConvertedString(_In_ CStringRange *pString, _Inout_ CTSFTTSArray<CCandidateListItem> *pItemList)
{
    CDictionaryResult* pdret = nullptr;
    CDictionarySearch dshSearch(_locale, _pDictionaryFile, pString, _keywordDelimiter);
	dshSearch.SetSearchSection(_searchSection);
    while (dshSearch.FindConvertedString(&pdret)) // TAIL ALL CHAR MATCH
    {
        for (UINT index = 0; index < pdret->_FindPhraseList.Count(); index++)
        {
            CCandidateListItem* pLI = nullptr;
            pLI = pItemList->Append();
            if (pLI)
            {
                pLI->_ItemString.Set(*pdret->_FindPhraseList.GetAt(index));
                pLI->_FindKeyCode.Set(pdret->_FindKeyCode.Get(), pdret->_FindKeyCode.GetLength());
            }
        }

        delete pdret;
        pdret = nullptr;
    }
	
}
VOID CTableDictionaryEngine::ParseConfig(IME_MODE imeMode)
{
	 CDictionarySearch dshSearch(_locale, _pDictionaryFile, NULL, _keywordDelimiter);
	 dshSearch.ParseConfig(imeMode);

}

//+---------------------------------------------------------------------------
// SortListItemByFindKeyCode
//----------------------------------------------------------------------------

VOID CTableDictionaryEngine::SortListItemByFindKeyCode(_Inout_ CTSFTTSArray<CCandidateListItem> *pItemList)
{
	if(pItemList->Count())
		MergeSortByFindKeyCode(pItemList, 0, pItemList->Count() - 1);
}

//+---------------------------------------------------------------------------
// MergeSortByFindKeyCode
//
//    Mergesort the array of element in CCandidateListItem::_FindKeyCode
//
//----------------------------------------------------------------------------

VOID CTableDictionaryEngine::MergeSortByFindKeyCode(_Inout_ CTSFTTSArray<CCandidateListItem> *pItemList, int leftRange, int rightRange)
{
	assert(leftRange >= 0);
    assert(rightRange >= 0);

    int candidateCount = (rightRange - leftRange + 1);

    if (candidateCount > 2)
    {
        int mid = leftRange + (candidateCount / 2);

        MergeSortByFindKeyCode(pItemList, leftRange, mid);
        MergeSortByFindKeyCode(pItemList, mid, rightRange);

        CTSFTTSArray<CCandidateListItem> ListItemTemp;

        int leftRangeTemp = 0;
        int midTemp = 0;
        for (leftRangeTemp = leftRange, midTemp = mid; leftRangeTemp != mid || midTemp != rightRange;)
        {
            CStringRange* psrgLeftTemp = nullptr;
            CStringRange* psrgMidTemp = nullptr;

            psrgLeftTemp = &pItemList->GetAt(leftRangeTemp)->_FindKeyCode;
            psrgMidTemp = &pItemList->GetAt(midTemp)->_FindKeyCode;

            CCandidateListItem* pLI = nullptr;
            pLI = ListItemTemp.Append();
            if (pLI)
            {
                if (leftRangeTemp == mid)
                {
                    *pLI = *pItemList->GetAt(midTemp++);
                }
                else if (midTemp == rightRange || CStringRange::Compare(_locale, psrgLeftTemp, psrgMidTemp) != CSTR_GREATER_THAN)
                {
                    *pLI = *pItemList->GetAt(leftRangeTemp++);
                }
                else
                {
                    *pLI = *pItemList->GetAt(midTemp++);
                }
            }
        }

        leftRangeTemp = leftRange;
        for (UINT count = 0; count < ListItemTemp.Count(); count++)
        {
            *pItemList->GetAt(leftRangeTemp++) = *ListItemTemp.GetAt(count);
        }
    }
    else if (candidateCount == 2)
    {
        CStringRange *psrgLeft = nullptr;
        CStringRange *psrgLeftNext = nullptr;

        psrgLeft = &pItemList->GetAt(leftRange )->_FindKeyCode;
        psrgLeftNext = &pItemList->GetAt(leftRange+1)->_FindKeyCode;

        if (CStringRange::Compare(_locale, psrgLeft, psrgLeftNext) == CSTR_GREATER_THAN)
        {
            CCandidateListItem ListItem;
            ListItem = *pItemList->GetAt(leftRange);
            *pItemList->GetAt(leftRange ) = *pItemList->GetAt(leftRange+1);
            *pItemList->GetAt(leftRange+1) = ListItem;
        }
    }
}

