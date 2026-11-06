// CandidateListModel.cpp - see CandidateListModel.h.
// The method bodies are the former CCandidateWindow members, moved without changes.

#include "Private.h"
#include "CandidateListModel.h"
#include <intsafe.h>
#include <memory>

CCandidateListModel::CCandidateListModel()
    : _currentSelection(0), _pIndexRange(nullptr), _dontAdjustOnEmptyItemPage(FALSE)
{
}

CCandidateListModel::~CCandidateListModel()
{
    Clear();
}

#ifndef DIME_USE_RUST_ENGINE
CCandidateListModel* CCandidateListModel::Create()
{
    return new (std::nothrow) CCandidateListModel();
}
#endif // with DIME_USE_RUST_ENGINE: RustCandidateListModel.cpp

//+---------------------------------------------------------------------------
// AddItem (former CCandidateWindow::_AddString)
//----------------------------------------------------------------------------

void CCandidateListModel::AddItem(_In_ CCandidateListItem* pCandidateItem, BOOL isAddFindKeyCode)
{
    if (pCandidateItem == nullptr) return;

    // Use smart pointers for automatic cleanup
    std::unique_ptr<WCHAR[]> pwchString;
    DWORD_PTR dwItemString = pCandidateItem->_ItemString.GetLength();
    if (dwItemString)
    {
        pwchString = std::make_unique<WCHAR[]>(dwItemString);
        if (!pwchString)
        {
            return;
        }
        memcpy(pwchString.get(), pCandidateItem->_ItemString.Get(), dwItemString * sizeof(WCHAR));
    }

    std::unique_ptr<WCHAR[]> pwchWildcard;
    DWORD_PTR itemWildcard = pCandidateItem->_FindKeyCode.GetLength();
    if (itemWildcard && isAddFindKeyCode)
    {
        pwchWildcard = std::make_unique<WCHAR[]>(itemWildcard);
        if (!pwchWildcard)
        {
            return;
        }
        memcpy(pwchWildcard.get(), pCandidateItem->_FindKeyCode.Get(), itemWildcard * sizeof(WCHAR));
    }

    CCandidateListItem* pLI = _candidateList.Append();
    if (!pLI)
    {
        return;
    }

    if (pwchString && pLI)
    {
        pLI->_ItemString.Set(pwchString.release(), dwItemString);
    }
    if (pwchWildcard && pLI)
    {
        pLI->_FindKeyCode.Set(pwchWildcard.release(), itemWildcard);
    }
}

//+---------------------------------------------------------------------------
// Clear (former _ClearList)
//----------------------------------------------------------------------------

void CCandidateListModel::Clear()
{
    for (UINT index = 0; index < _candidateList.Count(); index++)
    {
        CCandidateListItem* pItemList = _candidateList.GetAt(index);
        if (pItemList)
        {
            const WCHAR* pItemString = pItemList->_ItemString.Get();
            const WCHAR* pFindKeyCode = pItemList->_FindKeyCode.Get();

            if (pItemString)
            {
                delete[] pItemString;
                pItemList->_ItemString.Set(nullptr, 0);
            }
            if (pFindKeyCode)
            {
                delete[] pFindKeyCode;
                pItemList->_FindKeyCode.Set(nullptr, 0);
            }
        }
    }
    _currentSelection = 0;
    _candidateList.Clear();
    _PageIndex.Clear();
}

//+---------------------------------------------------------------------------
// GetString / GetSelectedString / GetSelectedKeyCode
//----------------------------------------------------------------------------

DWORD CCandidateListModel::GetString(int iIndex, _Inout_opt_ const WCHAR** ppwchCandidateString)
{
    CCandidateListItem* pItemList = nullptr;

    if (iIndex < 0)
    {
        if (ppwchCandidateString)
            *ppwchCandidateString = nullptr;
        return 0;
    }

    UINT index = static_cast<UINT>(iIndex);

    if (index >= _candidateList.Count())
    {
        if (ppwchCandidateString)
            *ppwchCandidateString = nullptr;
        return 0;
    }

    pItemList = _candidateList.GetAt(iIndex);
    if (pItemList == nullptr) return 0;
    if (ppwchCandidateString)
    {
        *ppwchCandidateString = pItemList->_ItemString.Get();
    }
    return (DWORD)pItemList->_ItemString.GetLength();
}

DWORD CCandidateListModel::GetSelectedString(_Inout_opt_ const WCHAR** ppwchCandidateString)
{
    INT selection = Selection();
    CCandidateListItem* pItemList = nullptr;

    if (selection < 0 || (UINT)selection >= _candidateList.Count())
    {
        if (ppwchCandidateString)
            *ppwchCandidateString = nullptr;
        return 0;
    }

    pItemList = _candidateList.GetAt(selection);
    if (pItemList == nullptr) return 0;
    if (ppwchCandidateString)
    {
        *ppwchCandidateString = pItemList->_ItemString.Get();
    }
    return (DWORD)pItemList->_ItemString.GetLength();
}

DWORD CCandidateListModel::GetSelectedKeyCode(_Inout_opt_ const WCHAR** ppwchCandidateString)
{
    INT selection = Selection();
    CCandidateListItem* pItemList = nullptr;

    if (selection < 0 || (UINT)selection >= _candidateList.Count())
    {
        if (ppwchCandidateString)
            *ppwchCandidateString = nullptr;
        return 0;
    }

    pItemList = _candidateList.GetAt(selection);
    if (pItemList == nullptr) return 0;
    if (ppwchCandidateString)
    {
        *ppwchCandidateString = pItemList->_FindKeyCode.Get();
    }
    return (DWORD)pItemList->_FindKeyCode.GetLength();
}

//+---------------------------------------------------------------------------
// SetSelectionInPage
//----------------------------------------------------------------------------

BOOL CCandidateListModel::SetSelectionInPage(int nPos)
{
    if (nPos < 0)
    {
        return FALSE;
    }

    UINT pos = static_cast<UINT>(nPos);

    if (pos >= _candidateList.Count())
    {
        return FALSE;
    }

    int currentPage = 0;
    if (FAILED(GetCurrentPage(&currentPage)))
    {
        return FALSE;
    }

    _currentSelection = *_PageIndex.GetAt(currentPage) + nPos;

    return TRUE;
}

//+---------------------------------------------------------------------------
// MoveSelection (former _MoveSelection without the scroll bar update)
//----------------------------------------------------------------------------

BOOL CCandidateListModel::MoveSelection(int offSet)
{
    if (_currentSelection + offSet >= (INT)_candidateList.Count() || _currentSelection + offSet < 0)
    {
        return FALSE;
    }

    _currentSelection += offSet;

    _dontAdjustOnEmptyItemPage = TRUE;

    return TRUE;
}

//+---------------------------------------------------------------------------
// SetSelection (former _SetSelection(int, BOOL) without the scroll bar update)
//----------------------------------------------------------------------------

BOOL CCandidateListModel::SetSelection(int selectedIndex, _Out_opt_ BOOL* pApplied)
{
    if (pApplied) *pApplied = FALSE;

    if (selectedIndex == -2)
    {
        selectedIndex = _candidateList.Count() - 1;
    }

    if (selectedIndex < -2) //-1 is used for phrase candidates
    {
        return FALSE;
    }

    int candCnt = static_cast<int>(_candidateList.Count());
    if (selectedIndex >= candCnt)
    {
        return FALSE;
    }

    _currentSelection = static_cast<UINT>(selectedIndex);
    if (pApplied) *pApplied = TRUE;

    return AdjustPageIndexForSelection();
}

//+---------------------------------------------------------------------------
// MovePage (former _MovePage without the scroll bar update)
//----------------------------------------------------------------------------

BOOL CCandidateListModel::MovePage(int offSet, _Out_opt_ BOOL* pWrapped)
{
    if (pWrapped) *pWrapped = FALSE;

    if (offSet == 0)
    {
        return TRUE;
    }

    int currentPage = 0;
    int selectionOffset = 0;
    int newPage = 0;

    if (FAILED(GetCurrentPage(&currentPage)))
    {
        return FALSE;
    }

    BOOL wrapped = FALSE;
    newPage = currentPage + offSet;
    if (newPage < 0)
    {
        newPage = static_cast<int>(_PageIndex.Count()) - 1;  // wrap to last page
        wrapped = TRUE;
    }
    else if (newPage >= static_cast<int>(_PageIndex.Count()))
    {
        newPage = 0;  // wrap to first page
        wrapped = TRUE;
    }
    if (_currentSelection < 0) _currentSelection = 0;//reset the selection position for phrase cand (_currentselection is -1);

    // If current selection is at the top of the page AND
    // we are on the "default" page border, then we don't
    // want adjustment to eliminate empty entries.
    //
    // We do this for keeping behavior inline with down level.
    if (_pIndexRange && _currentSelection % _pIndexRange->Count() == 0 &&
        _currentSelection == (INT)*_PageIndex.GetAt(currentPage))
    {
        _dontAdjustOnEmptyItemPage = TRUE;
    }

    selectionOffset = _currentSelection - *_PageIndex.GetAt(currentPage);
    _currentSelection = *_PageIndex.GetAt(newPage) + selectionOffset;
    _currentSelection = _candidateList.Count() > (UINT)_currentSelection ? _currentSelection : _candidateList.Count() - 1;

    if (pWrapped) *pWrapped = wrapped;
    return TRUE;
}

//+---------------------------------------------------------------------------
// SetSelectionOffset
//----------------------------------------------------------------------------

BOOL CCandidateListModel::SetSelectionOffset(int offSet)
{
    if (_currentSelection + offSet >= (INT)_candidateList.Count() || _currentSelection + offSet < 0)
    {
        return FALSE;
    }

    BOOL fCurrentPageHasEmptyItems = FALSE;
    BOOL fAdjustPageIndex = TRUE;

    CurrentPageHasEmptyItems(&fCurrentPageHasEmptyItems);

    int newOffset = _currentSelection + offSet;

    // For SB_LINEUP and SB_LINEDOWN, we need to special case if CurrentPageHasEmptyItems.
    // CurrentPageHasEmptyItems if we are on the last page.
    if ((offSet == 1 || offSet == -1) &&
        fCurrentPageHasEmptyItems && _PageIndex.Count() > 1)
    {
        int iPageIndex = *_PageIndex.GetAt(static_cast<size_t>(_PageIndex.Count()) - 1);
        // Moving on the last page and last page has empty items.
        if (newOffset >= iPageIndex)
        {
            fAdjustPageIndex = FALSE;
        }
        // Moving across page border.
        else if (newOffset < iPageIndex)
        {
            fAdjustPageIndex = TRUE;
        }

        _dontAdjustOnEmptyItemPage = TRUE;
    }

    _currentSelection = newOffset;

    if (fAdjustPageIndex)
    {
        return AdjustPageIndexForSelection();
    }

    return TRUE;
}

//+---------------------------------------------------------------------------
// GetPageIndex / SetPageIndex / GetCurrentPage
//----------------------------------------------------------------------------

HRESULT CCandidateListModel::GetPageIndex(UINT* pIndex, UINT uSize, _Inout_ UINT* puPageCnt)
{
    HRESULT hr = S_OK;

    if (uSize > _PageIndex.Count())
    {
        uSize = _PageIndex.Count();
    }
    else
    {
        hr = S_FALSE;
    }

    if (pIndex)
    {
        for (UINT i = 0; i < uSize; i++)
        {
            *pIndex = *_PageIndex.GetAt(i);
            pIndex++;
        }
    }

    *puPageCnt = _PageIndex.Count();

    return hr;
}

HRESULT CCandidateListModel::SetPageIndex(UINT* pIndex, UINT uPageCnt)
{
    _PageIndex.Clear();

    for (UINT i = 0; i < uPageCnt; i++)
    {
        UINT* pLastNewPageIndex = _PageIndex.Append();
        if (pLastNewPageIndex != nullptr)
        {
            *pLastNewPageIndex = *pIndex;
            pIndex++;
        }
    }

    return S_OK;
}

HRESULT CCandidateListModel::GetCurrentPage(_Inout_ UINT* pCurrentPage)
{
    HRESULT hr = S_OK;

    if (pCurrentPage == nullptr)
    {
        hr = E_INVALIDARG;
        goto Exit;
    }

    *pCurrentPage = 0;

    if (_PageIndex.Count() == 0)
    {
        hr = E_UNEXPECTED;
        goto Exit;
    }

    if (_PageIndex.Count() == 1 || _currentSelection < 0)
    {
        *pCurrentPage = 0;
        goto Exit;
    }

    {
        UINT i = 0;
        for (i = 1; i < _PageIndex.Count(); i++)
        {
            UINT uPageIndex = *_PageIndex.GetAt(i);

            if (uPageIndex > (UINT)_currentSelection)
            {
                break;
            }
        }

        *pCurrentPage = i - 1;
    }

Exit:
    return hr;
}

HRESULT CCandidateListModel::GetCurrentPage(_Inout_ int* pCurrentPage)
{
    HRESULT hr = E_FAIL;
    UINT needCastCurrentPage = 0;

    if (nullptr == pCurrentPage)
    {
        goto Exit;
    }

    *pCurrentPage = 0;

    hr = GetCurrentPage(&needCastCurrentPage);
    if (FAILED(hr))
    {
        goto Exit;
    }

    hr = UIntToInt(needCastCurrentPage, pCurrentPage);
    if (FAILED(hr))
    {
        goto Exit;
    }

Exit:
    return hr;
}

//+---------------------------------------------------------------------------
// AdjustPageIndexForSelection
//----------------------------------------------------------------------------

BOOL CCandidateListModel::AdjustPageIndexForSelection()
{
    if (_pIndexRange == nullptr) return FALSE;
    UINT candidateListPageCnt = _pIndexRange->Count();
    UINT* pNewPageIndex = nullptr;
    UINT newPageCnt = 0;

    if (_candidateList.Count() < candidateListPageCnt)
    {
        // no needed to reconstruct page index
        return TRUE;
    }

    // B is number of pages before the current page
    // A is number of pages after the current page
    // uNewPageCount is A + B + 1;
    // A is (uItemsAfter - 1) / candidateListPageCnt + 1 ->
    //      (_CandidateListCount - _currentSelection - CandidateListPageCount - 1) / candidateListPageCnt + 1->
    //      (_CandidateListCount - _currentSelection - 1) / candidateListPageCnt
    // B is (uItemsBefore - 1) / candidateListPageCnt + 1 ->
    //      (_currentSelection - 1) / candidateListPageCnt + 1
    // A + B is (_CandidateListCount - 2) / candidateListPageCnt + 1

    BOOL isBefore = _currentSelection > 0; //has to consider _currentSelection = -1 for associated phrase
    BOOL isAfter = (_currentSelection < 0 && _candidateList.Count() > candidateListPageCnt) || _candidateList.Count() > _currentSelection + candidateListPageCnt;

    // only have current page
    if (!isBefore && !isAfter)
    {
        newPageCnt = 1;
    }
    // only have after pages; just count the total number of pages
    else if (!isBefore && isAfter)
    {
        newPageCnt = (_candidateList.Count() - 1) / candidateListPageCnt + 1;
    }
    // we are at the last page
    else if (isBefore && !isAfter)
    {
        newPageCnt = 2 + (_currentSelection - 1) / candidateListPageCnt;
    }
    else if (isBefore && isAfter)
    {
        newPageCnt = (_candidateList.Count() - 2) / candidateListPageCnt + 2;
    }

    pNewPageIndex = new (std::nothrow) UINT[newPageCnt];
    if (pNewPageIndex == nullptr)
    {
        return FALSE;
    }
    pNewPageIndex[0] = 0;
    UINT firstPage = (_currentSelection < 0) ? candidateListPageCnt : _currentSelection % candidateListPageCnt;
    if (firstPage && newPageCnt > 1)
    {
        pNewPageIndex[1] = firstPage;
    }

    for (UINT i = firstPage ? 2 : 1; i < newPageCnt; ++i)
    {
        pNewPageIndex[i] = pNewPageIndex[i - 1] + candidateListPageCnt;
    }

    SetPageIndex(pNewPageIndex, newPageCnt);

    delete[] pNewPageIndex;

    return TRUE;
}

//+---------------------------------------------------------------------------
// CurrentPageHasEmptyItems
//----------------------------------------------------------------------------

HRESULT CCandidateListModel::CurrentPageHasEmptyItems(_Inout_ BOOL* hasEmptyItems)
{
    if (_pIndexRange == nullptr) return E_FAIL;
    int candidateListPageCnt = _pIndexRange->Count();
    UINT currentPage = 0;

    if (FAILED(GetCurrentPage(&currentPage)))
    {
        return S_FALSE;
    }

    if ((currentPage == 0 || currentPage == _PageIndex.Count() - 1) &&
        (_PageIndex.Count() > 0) &&
        (*_PageIndex.GetAt(currentPage) > (UINT)(_candidateList.Count() - candidateListPageCnt)))
    {
        *hasEmptyItems = TRUE;
    }
    else
    {
        *hasEmptyItems = FALSE;
    }

    return S_OK;
}

//+---------------------------------------------------------------------------
// AdjustPageIndex
//----------------------------------------------------------------------------

HRESULT CCandidateListModel::AdjustPageIndex(_Inout_ UINT& currentPage, _Inout_ UINT& currentPageIndex)
{
    HRESULT hr = E_FAIL;
    if (_pIndexRange == nullptr) return hr;
    UINT candidateListPageCnt = _pIndexRange->Count();

    currentPageIndex = *_PageIndex.GetAt(currentPage);

    BOOL hasEmptyItems = FALSE;
    if (FAILED(CurrentPageHasEmptyItems(&hasEmptyItems)))
    {
        goto Exit;
    }

    if (FALSE == hasEmptyItems)
    {
        goto Exit;
    }

    if (TRUE == _dontAdjustOnEmptyItemPage)
    {
        goto Exit;
    }

    {
        UINT tempSelection = _currentSelection;

        // Last page
        UINT candNum = _candidateList.Count();
        UINT pageNum = _PageIndex.Count();

        if ((currentPageIndex > candNum - candidateListPageCnt) && (pageNum > 0) && (currentPage == (pageNum - 1)))
        {
            _currentSelection = candNum - candidateListPageCnt;

            AdjustPageIndexForSelection();

            _currentSelection = tempSelection;

            if (FAILED(GetCurrentPage(&currentPage)))
            {
                goto Exit;
            }

            currentPageIndex = *_PageIndex.GetAt(currentPage);
        }
        // First page
        else if ((currentPageIndex < candidateListPageCnt) && (currentPage == 0))
        {
            _currentSelection = 0;

            AdjustPageIndexForSelection();

            _currentSelection = tempSelection;
        }
    }

    _dontAdjustOnEmptyItemPage = FALSE;
    hr = S_OK;

Exit:
    return hr;
}
