// RustCandidateListModel.h - CCandidateListModel with selection and paging in Rust
// (rust/crates/dime-core/src/candidates.rs, Rust migration phase 2).
//
// The candidate strings stay in the C++ base class (the window draws them); Rust
// only keeps the count, the selection and the page layout. Created by
// CCandidateListModel::Create() when DIME_USE_RUST_ENGINE is defined; the tests
// build it directly (DIME_RUST_AVAILABLE) to compare it with the C++ model.

#pragma once

#ifdef DIME_RUST_AVAILABLE

#include "CandidateListModel.h"

struct DimeCandidates;

class CRustCandidateListModel : public CCandidateListModel
{
public:
    CRustCandidateListModel();
    ~CRustCandidateListModel() override;
    BOOL IsValid() const { return _pRust != nullptr; }

    void AddItem(_In_ CCandidateListItem* pCandidateItem, BOOL includeKeyCode) override;
    void Clear() override;

    INT Selection() override;
    void SetSelectionRaw(INT index) override;
    BOOL SetSelectionInPage(int pos) override;
    BOOL MoveSelection(int offset) override;
    BOOL SetSelection(int index, _Out_opt_ BOOL* pApplied) override;
    BOOL MovePage(int offset, _Out_opt_ BOOL* pWrapped) override;
    BOOL SetSelectionOffset(int offset) override;

    UINT PageCount() override;
    UINT PageStart(UINT page) override;
    HRESULT GetPageIndex(UINT* pIndex, UINT uSize, _Inout_ UINT* puPageCnt) override;
    HRESULT SetPageIndex(UINT* pIndex, UINT uPageCnt) override;
    using CCandidateListModel::GetCurrentPage;
    HRESULT GetCurrentPage(_Inout_ UINT* pCurrentPage) override;
    BOOL AdjustPageIndexForSelection() override;
    HRESULT CurrentPageHasEmptyItems(_Inout_ BOOL* pHasEmptyItems) override;
    HRESULT AdjustPageIndex(_Inout_ UINT& currentPage, _Inout_ UINT& currentPageIndex) override;

private:
    DimeCandidates* _pRust;
};

#endif // DIME_RUST_AVAILABLE
