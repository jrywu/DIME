// RustCandidateListModel.cpp - see RustCandidateListModel.h

#include "Private.h"
#include "RustCandidateListModel.h"

#ifdef DIME_RUST_AVAILABLE

#include "dime_ffi.h"

#ifdef DIME_USE_RUST_ENGINE
CCandidateListModel* CCandidateListModel::Create()
{
    CRustCandidateListModel* p = new (std::nothrow) CRustCandidateListModel();
    if (p && !p->IsValid())
    {
        delete p;
        return new (std::nothrow) CCandidateListModel();
    }
    return p;
}
#endif

CRustCandidateListModel::CRustCandidateListModel() : _pRust(dime_candidates_new())
{
}

CRustCandidateListModel::~CRustCandidateListModel()
{
    dime_candidates_free(_pRust);
}

void CRustCandidateListModel::AddItem(_In_ CCandidateListItem* pCandidateItem, BOOL includeKeyCode)
{
    UINT before = Count();
    CCandidateListModel::AddItem(pCandidateItem, includeKeyCode);
    if (Count() > before)
        dime_candidates_push_item(_pRust);
}

void CRustCandidateListModel::Clear()
{
    CCandidateListModel::Clear();
    dime_candidates_clear(_pRust);
}

INT CRustCandidateListModel::Selection()
{
    return dime_candidates_selection(_pRust);
}

void CRustCandidateListModel::SetSelectionRaw(INT index)
{
    dime_candidates_set_selection_raw(_pRust, index);
}

BOOL CRustCandidateListModel::SetSelectionInPage(int pos)
{
    return dime_candidates_set_selection_in_page(_pRust, pos);
}

BOOL CRustCandidateListModel::MoveSelection(int offset)
{
    return dime_candidates_move_selection(_pRust, offset);
}

BOOL CRustCandidateListModel::SetSelection(int index, _Out_opt_ BOOL* pApplied)
{
    int32_t applied = 0;
    BOOL ret = dime_candidates_set_selection(_pRust, PageSize(), index, &applied);
    if (pApplied) *pApplied = applied;
    return ret;
}

BOOL CRustCandidateListModel::MovePage(int offset, _Out_opt_ BOOL* pWrapped)
{
    int32_t wrapped = 0;
    BOOL ret = dime_candidates_move_page(_pRust, PageSize(), offset, &wrapped);
    if (pWrapped) *pWrapped = wrapped;
    return ret;
}

BOOL CRustCandidateListModel::SetSelectionOffset(int offset)
{
    return dime_candidates_set_selection_offset(_pRust, PageSize(), offset);
}

UINT CRustCandidateListModel::PageCount()
{
    return dime_candidates_page_count(_pRust);
}

UINT CRustCandidateListModel::PageStart(UINT page)
{
    return dime_candidates_page_start(_pRust, page);
}

HRESULT CRustCandidateListModel::GetPageIndex(UINT* pIndex, UINT uSize, _Inout_ UINT* puPageCnt)
{
    HRESULT hr = S_OK;
    UINT pageCount = PageCount();
    if (uSize > pageCount)
        uSize = pageCount;
    else
        hr = S_FALSE;
    if (pIndex)
        for (UINT i = 0; i < uSize; i++)
            pIndex[i] = PageStart(i);
    *puPageCnt = pageCount;
    return hr;
}

HRESULT CRustCandidateListModel::SetPageIndex(UINT* pIndex, UINT uPageCnt)
{
    dime_candidates_set_page_index(_pRust, pIndex, uPageCnt);
    return S_OK;
}

HRESULT CRustCandidateListModel::GetCurrentPage(_Inout_ UINT* pCurrentPage)
{
    if (pCurrentPage == nullptr)
        return E_INVALIDARG;
    uint32_t page = 0;
    HRESULT hr = dime_candidates_current_page(_pRust, &page);
    *pCurrentPage = page;
    return hr;
}

BOOL CRustCandidateListModel::AdjustPageIndexForSelection()
{
    return dime_candidates_adjust_page_index_for_selection(_pRust, PageSize());
}

HRESULT CRustCandidateListModel::CurrentPageHasEmptyItems(_Inout_ BOOL* pHasEmptyItems)
{
    int32_t hasEmpty = 0;
    HRESULT hr = dime_candidates_current_page_has_empty_items(_pRust, PageSize(), &hasEmpty);
    if (hr == S_OK)
        *pHasEmptyItems = hasEmpty;
    return hr;
}

HRESULT CRustCandidateListModel::AdjustPageIndex(_Inout_ UINT& currentPage, _Inout_ UINT& currentPageIndex)
{
    if (PageSize() == 0)
        return E_FAIL; // C++: no range, outputs untouched
    uint32_t page = currentPage, index = currentPageIndex;
    HRESULT hr = dime_candidates_adjust_page_index(_pRust, PageSize(), &page, &index);
    currentPage = page;
    currentPageIndex = index;
    return hr;
}

#endif // DIME_RUST_AVAILABLE
