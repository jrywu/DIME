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
#include "TableDictionaryEngine.h"
#include "DictionarySearch.h"
#include "DIME.h"


CTableDictionaryEngine::CTableDictionaryEngine(LCID locale, _In_ CFile *pDictionaryFile, _In_ DICTIONARY_TYPE dictionaryType )
{
	_locale = locale;
    _pDictionaryFile = pDictionaryFile;
	_searchSection = SEARCH_SECTION::SEARCH_SECTION_TEXT;
	_dictionaryType = dictionaryType;

	_pRadicalMap = new _T_RadicalMap();

	// initialize _pRadicalIndexMap if the dictionary is in cin format
	_pRadicalIndexMap = (dictionaryType == DICTIONARY_TYPE::CIN_DICTIONARY) ? new _T_RadicalIndexMap() : nullptr;

	_sortedCIN = FALSE;

	if(dictionaryType == DICTIONARY_TYPE::TTS_DICTIONARY || dictionaryType == DICTIONARY_TYPE::INI_DICTIONARY)
		_keywordDelimiter = '=';
	else if (dictionaryType == DICTIONARY_TYPE::CIN_DICTIONARY)
		_keywordDelimiter = '\t';
	else if (dictionaryType == DICTIONARY_TYPE::LIME_DICTIONARY)
		_keywordDelimiter = '|';
}


//+---------------------------------------------------------------------------
//
// CollectWord
//
//----------------------------------------------------------------------------

VOID CTableDictionaryEngine::CollectWord(_In_ CStringRange *pKeyCode, _Inout_ CDIMEArray<CStringRange> *pWordStrings)
{
    CDictionaryResult* pdret = nullptr;
	CDictionarySearch dshSearch(_locale, _pDictionaryFile, pKeyCode, _keywordDelimiter);

	if(_dictionaryType == DICTIONARY_TYPE::TTS_DICTIONARY)
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

VOID CTableDictionaryEngine::CollectWord(_In_ CStringRange *pKeyCode, _Inout_ CDIMEArray<CCandidateListItem> *pItemList)
{
    CDictionaryResult* pdret = nullptr;
    CDictionarySearch dshSearch(_locale, _pDictionaryFile, pKeyCode, _keywordDelimiter);

	if(_dictionaryType == DICTIONARY_TYPE::TTS_DICTIONARY)
		dshSearch.SetSearchSection(_searchSection);

	if (_sortedCIN)
	{
		WCHAR initial = towupper(*pKeyCode->Get());
		_T_RadicalIndexMap::iterator it = _pRadicalIndexMap->find(initial);
		if (it != _pRadicalIndexMap->end())
		{
			dshSearch.setSearchOffset(it->second);
		}
	}

    while (dshSearch.FindPhrase(&pdret))
    {
		if (_sortedCIN && pdret->_FindPhraseList.Count()) dshSearch.setSortedSearchResultFound(TRUE);

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

VOID CTableDictionaryEngine::CollectWordForWildcard(_In_ CStringRange *pKeyCode, _Inout_ CDIMEArray<CCandidateListItem> *pItemList, _In_opt_ CTableDictionaryEngine* wordFreqTableDictionaryEngine)
{
    CDictionaryResult* pdret = nullptr;
    CDictionarySearch dshSearch(_locale, _pDictionaryFile, pKeyCode, _keywordDelimiter);
	
	BOOL dupped = FALSE;

	
	if(_dictionaryType == DICTIONARY_TYPE::TTS_DICTIONARY)
		dshSearch.SetSearchSection(_searchSection);

    while (dshSearch.FindPhraseForWildcard(&pdret))
    {
		dupped = FALSE;
        for (UINT iIndex = 0; iIndex < pdret->_FindPhraseList.Count(); iIndex++)
        {
			//check if the itemText is dupped.
			for (UINT i = 0; i < pItemList->Count(); i++)
			{
				if (CStringRange::Compare(_locale, &pItemList->GetAt(i)->_ItemString, pdret->_FindPhraseList.GetAt(iIndex)) == CSTR_EQUAL)
				{
					dupped = TRUE;
					break;
				}
			}


			if (!dupped)
			{
				CCandidateListItem* pLI = nullptr;
				pLI = pItemList->Append();
				if (pLI)
				{
					pLI->_ItemString.Set(*pdret->_FindPhraseList.GetAt(iIndex));
					pLI->_FindKeyCode.Set(pdret->_FindKeyCode.Get(), pdret->_FindKeyCode.GetLength());
					if (wordFreqTableDictionaryEngine)
					{
						if (pLI->_ItemString.GetLength() > 1)
							pLI->_WordFrequency = (int)pLI->_ItemString.GetLength();
						else
						{
							CDIMEArray<CCandidateListItem> candidateList;
							wordFreqTableDictionaryEngine->CollectWord(&pLI->_ItemString, &candidateList);
							pLI->_WordFrequency = (candidateList.Count() == 1)
								? _wtoi(candidateList.GetAt(0)->_ItemString.Get()) : 0;
						}
					}

				}
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

VOID CTableDictionaryEngine::CollectWordFromConvertedStringForWildcard(_In_ CStringRange *pString, _Inout_ CDIMEArray<CCandidateListItem> *pItemList)
{
    CDictionaryResult* pdret = nullptr;
	CDictionarySearch dshSearch(_locale, _pDictionaryFile, pString, _keywordDelimiter);

	if(_dictionaryType == DICTIONARY_TYPE::TTS_DICTIONARY)
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

VOID CTableDictionaryEngine::CollectWordFromConvertedString(_In_ CStringRange *pString, _Inout_ CDIMEArray<CCandidateListItem> *pItemList)
{
    CDictionaryResult* pdret = nullptr;
    CDictionarySearch dshSearch(_locale, _pDictionaryFile, pString, _keywordDelimiter);
	
	if(_dictionaryType == DICTIONARY_TYPE::TTS_DICTIONARY)
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
	if ( _dictionaryType != DICTIONARY_TYPE::INI_DICTIONARY && _pRadicalMap->size())
	{
		for(_T_RadicalMap::iterator item = _pRadicalMap->begin(); item != _pRadicalMap->end(); ++item)
		{
			delete [] item->second;
		}
		_pRadicalMap->clear();
	}
	CDictionarySearch dshSearch(_locale, _pDictionaryFile, NULL, _keywordDelimiter);
	if (dshSearch.ParseConfig(imeMode, _pRadicalMap, _pRadicalIndexMap, _pSelkey, _pEndkey) 
		&& _dictionaryType == DICTIONARY_TYPE::CIN_DICTIONARY && _pRadicalIndexMap && _pRadicalIndexMap->size())
	{
		_sortedCIN = TRUE;
	}
	

}

//+---------------------------------------------------------------------------
// SortListItemByFindKeyCode
//----------------------------------------------------------------------------

VOID CTableDictionaryEngine::SortListItemByFindKeyCode(_Inout_ CDIMEArray<CCandidateListItem> *pItemList)
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

VOID CTableDictionaryEngine::MergeSortByFindKeyCode(_Inout_ CDIMEArray<CCandidateListItem> *pItemList, int leftRange, int rightRange)
{
	assert(leftRange >= 0);
    assert(rightRange >= 0);

    int candidateCount = (rightRange - leftRange + 1);

    if (candidateCount > 2)
    {
        int mid = leftRange + (candidateCount / 2);

        MergeSortByFindKeyCode(pItemList, leftRange, mid);
        MergeSortByFindKeyCode(pItemList, mid, rightRange);

        CDIMEArray<CCandidateListItem> ListItemTemp;

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

//+---------------------------------------------------------------------------
// SortListItemByWordFrequency
//----------------------------------------------------------------------------

VOID CTableDictionaryEngine::SortListItemByWordFrequency(_Inout_ CDIMEArray<CCandidateListItem> *pItemList)
{
	if (pItemList->Count())
		MergeSortByWordFrequency(pItemList, 0, pItemList->Count() - 1);
}

//+---------------------------------------------------------------------------
// MergeSortByWordFrequency
//
//    Mergesort the array of element in CCandidateListItem::_WordFrequency
//
//----------------------------------------------------------------------------

VOID CTableDictionaryEngine::MergeSortByWordFrequency(_Inout_ CDIMEArray<CCandidateListItem> *pItemList, int leftRange, int rightRange)
{
	assert(leftRange >= 0);
	assert(rightRange >= 0);

	int candidateCount = (rightRange - leftRange + 1);

	if (candidateCount > 2)
	{
		int mid = leftRange + (candidateCount / 2);

		MergeSortByWordFrequency(pItemList, leftRange, mid);
		MergeSortByWordFrequency(pItemList, mid, rightRange);

		CDIMEArray<CCandidateListItem> ListItemTemp;

		int leftRangeTemp = 0;
		int midTemp = 0;
		for (leftRangeTemp = leftRange, midTemp = mid; leftRangeTemp != mid || midTemp != rightRange;)
		{
			int psrgLeftTemp = 0;
			int psrgMidTemp = 0;

			psrgLeftTemp = pItemList->GetAt(leftRangeTemp)->_WordFrequency;
			psrgMidTemp = pItemList->GetAt(midTemp)->_WordFrequency;

			psrgLeftTemp = (psrgLeftTemp < 0) ? 0 : psrgLeftTemp;
			psrgMidTemp = (psrgMidTemp < 0) ? 0 : psrgMidTemp;

			CCandidateListItem* pLI = nullptr;
			pLI = ListItemTemp.Append();
			if (pLI)
			{
				if (leftRangeTemp == mid)
				{
					*pLI = *pItemList->GetAt(midTemp++);
				}
				else if (midTemp == rightRange || psrgLeftTemp > psrgMidTemp)
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
		int psrgLeft = 0;
		int psrgLeftNext = 0;

		psrgLeft = pItemList->GetAt(leftRange)->_WordFrequency;
		psrgLeftNext = pItemList->GetAt(leftRange + 1)->_WordFrequency;

		psrgLeft = (psrgLeft < 0) ? 0 : psrgLeft;
		psrgLeftNext = (psrgLeftNext < 0) ? 0 : psrgLeftNext;

		
		if (psrgLeft < psrgLeftNext) 
		{
			CCandidateListItem ListItem;
			ListItem = *pItemList->GetAt(leftRange);
			*pItemList->GetAt(leftRange) = *pItemList->GetAt(leftRange + 1);
			*pItemList->GetAt(leftRange + 1) = ListItem;
		}
	}
}

