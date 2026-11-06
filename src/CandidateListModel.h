// CandidateListModel.h - the candidate list, its selection and paging, without any UI.
//
// Rust migration phase 2a-2 (docs/RUST_MIGRATION_PLAN.md): the state the key handling
// decides on (items, selected item, page layout) used to live inside CCandidateWindow.
// It is moved here unchanged so that the decision logic can work without a window;
// CCandidateWindow renders the model and keeps its scroll bar in sync.
//
// Behaviour is identical to the former CCandidateWindow members, quirks included
// (selection -1 for associated phrases, the "don't adjust on empty item page" flag,
// wrapping page moves, the page-index adjustment on the first/last page).

#pragma once

#include "BaseStructure.h"

class CCandidateListModel
{
public:
    CCandidateListModel();
    virtual ~CCandidateListModel();

    // The C++ model, or with DIME_USE_RUST_ENGINE the Rust-backed one
    // (CRustCandidateListModel: selection and paging in rust/crates/dime-core).
    static CCandidateListModel* Create();
    CCandidateListModel(const CCandidateListModel&) = delete;
    CCandidateListModel& operator=(const CCandidateListModel&) = delete;

    // Page size = number of select keys (nullptr: no paging information yet).
    void SetIndexRange(_In_opt_ CCandidateRange* pIndexRange) { _pIndexRange = pIndexRange; }
    CCandidateRange* IndexRange() const { return _pIndexRange; }

    // Items (copied; the key code only when includeKeyCode).
    virtual void AddItem(_In_ CCandidateListItem* pCandidateItem, BOOL includeKeyCode);
    virtual void Clear();
    UINT Count() { return _candidateList.Count(); }
    CCandidateListItem* ItemAt(UINT index) { return _candidateList.GetAt(index); }
    DWORD GetString(int index, _Inout_opt_ const WCHAR** ppString);
    DWORD GetSelectedString(_Inout_opt_ const WCHAR** ppString);
    DWORD GetSelectedKeyCode(_Inout_opt_ const WCHAR** ppKeyCode);

    // Selection (-1: nothing selected, used for associated phrases).
    virtual INT Selection() { return _currentSelection; }
    virtual void SetSelectionRaw(INT index) { _currentSelection = index; }
    virtual BOOL SetSelectionInPage(int pos);
    // Returns FALSE (nothing changed) when the move would leave the list.
    virtual BOOL MoveSelection(int offset);
    // index -2 = last item. *pApplied tells whether the index was accepted; the
    // return value is the page-index adjustment result (former _SetSelection).
    virtual BOOL SetSelection(int index, _Out_opt_ BOOL* pApplied);
    // Moves by whole pages, wrapping around; *pWrapped tells whether it wrapped.
    virtual BOOL MovePage(int offset, _Out_opt_ BOOL* pWrapped);
    // Scroll-bar style move: stops at the ends, keeps a short last page in place.
    virtual BOOL SetSelectionOffset(int offset);

    // Pages.
    virtual UINT PageCount() { return _PageIndex.Count(); }
    virtual UINT PageStart(UINT page) { return *_PageIndex.GetAt(page); }
    virtual HRESULT GetPageIndex(UINT* pIndex, UINT uSize, _Inout_ UINT* puPageCnt);
    virtual HRESULT SetPageIndex(UINT* pIndex, UINT uPageCnt);
    virtual HRESULT GetCurrentPage(_Inout_ UINT* pCurrentPage);
    HRESULT GetCurrentPage(_Inout_ int* pCurrentPage);
    virtual BOOL AdjustPageIndexForSelection();
    virtual HRESULT CurrentPageHasEmptyItems(_Inout_ BOOL* pHasEmptyItems);
    // Used while painting: moves a short first/last page so it is full.
    virtual HRESULT AdjustPageIndex(_Inout_ UINT& currentPage, _Inout_ UINT& currentPageIndex);

protected:
    UINT PageSize() const { return _pIndexRange ? (UINT)_pIndexRange->Count() : 0; }

private:
    INT _currentSelection;
    CDIMEArray<CCandidateListItem> _candidateList;
    CDIMEArray<UINT> _PageIndex;
    CCandidateRange* _pIndexRange;
    BOOL _dontAdjustOnEmptyItemPage;
};
