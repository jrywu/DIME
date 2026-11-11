// RustCandidateWindow.cpp - see RustCandidateWindow.h

#include "Private.h"
#include "Globals.h"
#include "Config.h"
#include "Define.h"
#include "RustCandidateWindow.h"

#ifdef DIME_RUST_AVAILABLE

static_assert(sizeof(DimeRect) == sizeof(RECT), "DimeRect layout");
static_assert(sizeof(DimePoint) == sizeof(POINT), "DimePoint layout");

//+---------------------------------------------------------------------------
//
// The candidate list the Rust window reads (DimeCandModel): the model and the
// index range of this object. "Without the scroll bar": the Rust window moves
// its scroll bar itself where CCandidateWindow's message handling did.
//
//----------------------------------------------------------------------------

struct CRustCandidateWindowModel
{
    static CRustCandidateWindow* W(void* ctx) { return static_cast<CRustCandidateWindow*>(ctx); }
    static CCandidateListModel* M(void* ctx) { return W(ctx)->_model.get(); }

    static int32_t Selection(void* ctx) { return M(ctx)->Selection(); }
    static void SetSelectionRaw(void* ctx, uint32_t index) { M(ctx)->SetSelectionRaw(index); }
    static void SetSelectionOffset(void* ctx, int32_t offset) { M(ctx)->SetSelectionOffset(offset); }
    static void SetSelection(void* ctx, int32_t index) { BOOL applied = FALSE; M(ctx)->SetSelection(index, &applied); }
    static void MovePage(void* ctx, int32_t offset)
    {
        if (offset == 0) return;   // CCandidateWindow::_MovePage
        BOOL wrapped = FALSE;
        M(ctx)->MovePage(offset, &wrapped);
    }
    static int32_t CurrentPage(void* ctx, int32_t* page)
    {
        int p = 0;
        if (FAILED(M(ctx)->GetCurrentPage(&p))) return 0;
        *page = p;
        return 1;
    }
    static int32_t PaintPage(void* ctx, uint32_t* page, uint32_t* start)
    {
        UINT p = 0, st = 0;
        if (FAILED(M(ctx)->GetCurrentPage(&p))) return 0;
        M(ctx)->AdjustPageIndex(p, st);
        *page = p;
        *start = st;
        return 1;
    }
    static uint32_t PageCount(void* ctx) { return (uint32_t)M(ctx)->PageCount(); }
    static uint32_t PageStart(void* ctx, int32_t page) { return (uint32_t)M(ctx)->PageStart(page); }
    static uint32_t Count(void* ctx) { return M(ctx)->Count(); }
    static int32_t RowsInPage(void* ctx) { return W(ctx)->_pIndexRange ? W(ctx)->_pIndexRange->Count() : -1; }
    static uint16_t IndexChar(void* ctx, int32_t row)
    {
        CCandidateRange* range = W(ctx)->_pIndexRange;
        // GetAt throws out of range, and a callback must not throw.
        if (!range || row < 0 || row >= range->Count()) return 0;
        return (uint16_t)range->GetAt(row)->CandIndex;
    }
    static const uint16_t* Item(void* ctx, uint32_t index, uint32_t* len)
    {
        CCandidateListItem* it = M(ctx)->ItemAt(index);
        if (!it) return nullptr;
        *len = (uint32_t)it->_ItemString.GetLength();
        // Not null for an item with an empty string: CCandidateWindow still draws its row.
        static const uint16_t empty[1] = { 0 };
        return it->_ItemString.Get() ? (const uint16_t*)it->_ItemString.Get() : empty;
    }
    static int32_t HasCallback(void* ctx) { return W(ctx)->_pfnCallback != nullptr; }
    static void Invoke(void* ctx, int32_t action)
    {
        // Copy before calling: the callback may destroy the window and this object.
        CANDWNDCALLBACK pfnCallback = W(ctx)->_pfnCallback;
        void* pObj = W(ctx)->_pObj;
        if (pfnCallback)
            pfnCallback(pObj, action == 0 ? CANDWND_ACTION::CAND_ITEM_SELECT : CANDWND_ACTION::CAND_CANCEL);
    }

    static uint32_t Dpi(void* hwnd) { return CConfig::GetDpiForHwnd((HWND)hwnd); }
    static int32_t Dark() { return CConfig::GetEffectiveDarkMode() ? 1 : 0; }
    static int32_t AlwaysShow() { return CConfig::GetAlwaysShowScrollbars() ? 1 : 0; }
    static void* Font() { return Global::defaultlFontHandle; }
    static void SetFont(void* hwnd) { CConfig::SetDefaultTextFont((HWND)hwnd); }
};

CRustCandidateWindow::CRustCandidateWindow(_In_ CANDWNDCALLBACK pfnCallback, _In_ void* pv, _In_ CCandidateRange* pIndexRange, _In_ BOOL isStoreAppMode)
    : _model(CCandidateListModel::Create()), _pIndexRange(pIndexRange), _pfnCallback(pfnCallback), _pObj(pv), _c(nullptr)
{
    UNREFERENCED_PARAMETER(isStoreAppMode);   // the Rust scroll bar has one implementation for both shells
    _model->SetIndexRange(pIndexRange);
}

CRustCandidateWindow::~CRustCandidateWindow()
{
    _Destroy();
    _model->Clear();
}

BOOL CRustCandidateWindow::_Create(_In_ UINT wndWidth, _In_ UINT fontSize, _In_opt_ HWND parentWndHandle)
{
    UNREFERENCED_PARAMETER(fontSize);
    if (_c) return FALSE;
    static const DimeCandEnv env = { CRustCandidateWindowModel::Dpi, CRustCandidateWindowModel::Dark,
                                     CRustCandidateWindowModel::AlwaysShow, CRustCandidateWindowModel::Font,
                                     CRustCandidateWindowModel::SetFont };
    const DimeCandModel model = { this,
        CRustCandidateWindowModel::Selection, CRustCandidateWindowModel::SetSelectionRaw,
        CRustCandidateWindowModel::SetSelectionOffset, CRustCandidateWindowModel::SetSelection,
        CRustCandidateWindowModel::MovePage, CRustCandidateWindowModel::CurrentPage,
        CRustCandidateWindowModel::PaintPage, CRustCandidateWindowModel::PageCount,
        CRustCandidateWindowModel::PageStart, CRustCandidateWindowModel::Count,
        CRustCandidateWindowModel::RowsInPage, CRustCandidateWindowModel::IndexChar,
        CRustCandidateWindowModel::Item, CRustCandidateWindowModel::HasCallback, CRustCandidateWindowModel::Invoke };
    DimeCandClasses classes = { Global::AtomCandidateWindow, Global::AtomCandidateShadowWindow,
                                Global::AtomCandidateScrollBarWindow, Global::AtomNotifyWindow, Global::AtomNotifyShadowWindow };
#ifdef DIME_UNIT_TESTING
    if (_hasClassesForTest) classes = _classesForTest;
#endif
    // The CCandidateWindow constructor's colors: black text on the window color (the number and
    // selected colors were left uninitialized there; UIPresenter sets all of them before painting).
    COLORREF text = CANDWND_ITEM_COLOR, bk = GetSysColor(COLOR_WINDOW);
    const DimeCandColors colors = { text, bk, text, bk, text, bk };
    _c = dime_ui_candidate_create(Global::dllInstanceHandle, &classes, parentWndHandle, wndWidth, &env, &model,
                                  &colors, GetSysColor(COLOR_3DHIGHLIGHT));
    if (_c) _SetTextColor(text, bk);   // the constructor's _SetTextColor (IsTooSimilar adjustment)
    // As CCandidateWindow::_Create: TRUE even when the window could not be created. The list
    // still works and the window operations do nothing (the C++ window was "virtual", without an HWND).
    return TRUE;
}

void CRustCandidateWindow::_Destroy()
{
    DimeCandidate* c = _c;
    _c = nullptr;   // before freeing: destroying the window may send messages
    dime_ui_candidate_free(c);
}

HWND CRustCandidateWindow::_GetWnd() const { return (HWND)dime_ui_candidate_hwnd(_c); }
BOOL CRustCandidateWindow::_IsWindowVisible() const { return dime_ui_candidate_is_visible(_c) ? TRUE : FALSE; }

BOOL CRustCandidateWindow::_GetWindowRect(_Inout_ LPRECT lpRect) const
{
    if (!lpRect) return FALSE;
    dime_ui_candidate_rect(_c, 0, (DimeRect*)lpRect);
    return TRUE;
}

BOOL CRustCandidateWindow::_GetClientRect(_Inout_ LPRECT lpRect) const
{
    if (!lpRect) return FALSE;
    dime_ui_candidate_rect(_c, 1, (DimeRect*)lpRect);
    return TRUE;
}

HRESULT CRustCandidateWindow::_GetWindowExtent(_In_ const RECT* prcTextExtent, _In_opt_ RECT* prcCandidateExtent, _Inout_ POINT* pptCandidate) const
{
    if (prcTextExtent == nullptr || prcCandidateExtent == nullptr || pptCandidate == nullptr)
        return E_INVALIDARG;
    dime_ui_candidate_window_extent(_c, (const DimeRect*)prcTextExtent, (const DimeRect*)prcCandidateExtent, (DimePoint*)pptCandidate);
    return S_OK;
}

UINT CRustCandidateWindow::_GetWidth() const { return (UINT)dime_ui_candidate_width(_c); }
void CRustCandidateWindow::_Move(int x, int y) { dime_ui_candidate_op(_c, 1, x, y); }
void CRustCandidateWindow::_Show(BOOL isShowWnd) { dime_ui_candidate_op(_c, 0, isShowWnd ? 1 : 0, 0); }
void CRustCandidateWindow::_InvalidateRect() { dime_ui_candidate_op(_c, 3, 0, 0); }
void CRustCandidateWindow::_ResizeWindow() { dime_ui_candidate_op(_c, 2, 0, 0); }
void CRustCandidateWindow::_SetCandStringLength(_In_ UINT wndWidth) { dime_ui_candidate_op(_c, 4, (int32_t)wndWidth, 0); }
VOID CRustCandidateWindow::_SetNumberColor(_In_ COLORREF crColor, _In_ COLORREF crBkColor) { dime_ui_candidate_set_color(_c, 0, crColor, crBkColor); }
VOID CRustCandidateWindow::_SetTextColor(_In_ COLORREF crColor, _In_ COLORREF crBkColor) { dime_ui_candidate_set_color(_c, 1, crColor, crBkColor); }
VOID CRustCandidateWindow::_SetSelectedTextColor(_In_ COLORREF crColor, _In_ COLORREF crBkColor) { dime_ui_candidate_set_color(_c, 2, crColor, crBkColor); }
VOID CRustCandidateWindow::_SetFillColor(_In_ COLORREF fiColor) { dime_ui_candidate_op(_c, 5, (int32_t)fiColor, 0); }
void CRustCandidateWindow::_SetScrollInfo(_In_ int nMax, _In_ int nPage) { dime_ui_candidate_op(_c, 9, nMax, nPage); }

// CCandidateWindow::_MoveSelection / _SetSelection / _MovePage: the model, then the scroll bar.
BOOL CRustCandidateWindow::_MoveSelection(_In_ int offSet, _In_ BOOL isNotify)
{
    if (!_model->MoveSelection(offSet))
        return FALSE;
    if (isNotify)
        dime_ui_candidate_op(_c, 6, offSet, 1);
    return TRUE;
}

BOOL CRustCandidateWindow::_SetSelection(_In_ int selectedIndex, _In_ BOOL isNotify)
{
    BOOL applied = FALSE;
    BOOL ret = _model->SetSelection(selectedIndex, &applied);
    if (applied && isNotify)
        dime_ui_candidate_op(_c, 8, selectedIndex, 1);
    return ret;
}

BOOL CRustCandidateWindow::_MovePage(_In_ int offSet, _In_ BOOL isNotify)
{
    if (offSet == 0)
        return TRUE;
    BOOL wrapped = FALSE;
    if (!_model->MovePage(offSet, &wrapped))
        return FALSE;
    if (isNotify)
    {
        if (wrapped)
            dime_ui_candidate_op(_c, 8, _model->Selection(), 0);
        else
            dime_ui_candidate_op(_c, 7, offSet, 1);
    }
    return TRUE;
}

#endif
