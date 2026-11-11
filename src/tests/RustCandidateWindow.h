// RustCandidateWindow.h - test-only stand-in for the removed product type
// (phase 4b-3). Window parity drives dime_ui_candidate_* with a C++ list.
// The product presenter owns its own Rust window and list.

#pragma once

#include "CandidateWindow.h"

#ifdef DIME_RUST_AVAILABLE
#include "dime_ffi.h"

class CRustCandidateWindow
{
public:
    CRustCandidateWindow(_In_ CANDWNDCALLBACK pfnCallback, _In_ void* pv, _In_ CCandidateRange* pIndexRange, _In_ BOOL isStoreAppMode);
    ~CRustCandidateWindow();
    CRustCandidateWindow(const CRustCandidateWindow&) = delete;
    CRustCandidateWindow& operator=(const CRustCandidateWindow&) = delete;

    // CCandidateWindow::_Create (fontSize is unused there too: the font is Global::defaultlFontHandle);
    // TRUE also when the window could not be created, as there.
    BOOL _Create(_In_ UINT wndWidth, _In_ UINT fontSize, _In_opt_ HWND parentWndHandle);
    void _Destroy();   // CBaseWindow::_Destroy: the window goes, the list stays until the object does

    HWND _GetWnd() const;
    BOOL _IsWindowVisible() const;
    BOOL _GetWindowRect(_Inout_ LPRECT lpRect) const;
    BOOL _GetClientRect(_Inout_ LPRECT lpRect) const;
    HRESULT _GetWindowExtent(_In_ const RECT* prcTextExtent, _In_opt_ RECT* prcCandidateExtent, _Inout_ POINT* pptCandidate) const;
    UINT _GetWidth() const;
    void _Move(int x, int y);
    void _Show(BOOL isShowWnd);
    void _InvalidateRect();
    void _ResizeWindow();

    void _SetCandIndexRange(CCandidateRange* pIndexRange) { _pIndexRange = pIndexRange; _model->SetIndexRange(pIndexRange); }
    void _SetCandStringLength(_In_ UINT wndWidth);
    VOID _SetNumberColor(_In_ COLORREF crColor, _In_ COLORREF crBkColor);
    VOID _SetTextColor(_In_ COLORREF crColor, _In_ COLORREF crBkColor);
    VOID _SetSelectedTextColor(_In_ COLORREF crColor, _In_ COLORREF crBkColor);
    VOID _SetFillColor(_In_ COLORREF fiColor);
    void _SetScrollInfo(_In_ int nMax, _In_ int nPage);

    // The candidate list (as CCandidateWindow: the model, the scroll bar kept in sync).
    void _AddString(_Inout_ CCandidateListItem* pCandidateItem, _In_ BOOL isAddFindKeyCode) { _model->AddItem(pCandidateItem, isAddFindKeyCode); }
    void _ClearList() { _model->Clear(); }
    UINT _GetCount() { return _model->Count(); }
    INT _GetSelection() { return _model->Selection(); }
    CCandidateListModel* _GetModel() { return _model.get(); }
    DWORD _GetCandidateString(_In_ int iIndex, _Inout_opt_ const WCHAR** ppwchCandidateString) { return _model->GetString(iIndex, ppwchCandidateString); }
    DWORD _GetSelectedCandidateString(_Inout_opt_ const WCHAR** ppwchCandidateString) { return _model->GetSelectedString(ppwchCandidateString); }
    DWORD _GetSelectedCandidateKeyCode(_Inout_opt_ const WCHAR** ppwchCandidateString) { return _model->GetSelectedKeyCode(ppwchCandidateString); }
    BOOL _MoveSelection(_In_ int offSet, _In_ BOOL isNotify);
    BOOL _SetSelection(_In_ int selectedIndex, _In_ BOOL isNotify);
    void _SetSelection(_In_ int nIndex) { _model->SetSelectionRaw(nIndex); }
    BOOL _MovePage(_In_ int offSet, _In_ BOOL isNotify);
    BOOL _SetSelectionInPage(int nPos) { return _model->SetSelectionInPage(nPos); }
    HRESULT _GetPageIndex(UINT* pIndex, _In_ UINT uSize, _Inout_ UINT* puPageCnt) { return _model->GetPageIndex(pIndex, uSize, puPageCnt); }
    HRESULT _SetPageIndex(UINT* pIndex, _In_ UINT uPageCnt) { return _model->SetPageIndex(pIndex, uPageCnt); }
    HRESULT _GetCurrentPage(_Inout_ UINT* pCurrentPage) { return _model->GetCurrentPage(pCurrentPage); }
    HRESULT _GetCurrentPage(_Inout_ int* pCurrentPage) { return _model->GetCurrentPage(pCurrentPage); }

    DimeCandidate* _GetRustWindow() const { return _c; }
#ifdef DIME_UNIT_TESTING
    // The classes the window is created with (before _Create; default: Global::Atom*).
    void _SetClassesForTest(const DimeCandClasses& classes) { _classesForTest = classes; _hasClassesForTest = TRUE; }
#endif

private:
    friend struct CRustCandidateWindowModel;

    std::unique_ptr<CCandidateListModel> _model;   // items, selection, pages
    CCandidateRange* _pIndexRange;
    CANDWNDCALLBACK _pfnCallback;
    void* _pObj;
    DimeCandidate* _c;
#ifdef DIME_UNIT_TESTING
    DimeCandClasses _classesForTest = {};
    BOOL _hasClassesForTest = FALSE;
#endif
};
#endif
