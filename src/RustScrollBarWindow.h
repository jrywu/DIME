// RustScrollBarWindow.h - the candidate window's scroll bar owned by Rust
// (dime-ui::sbwin, phase 3c-5c; docs/RUST_WINDOW_OWNERSHIP.md).
//
// Tests only (3c-5f): the Rust DIME.dll has no C++ candidate window; dime-ui's
// candidate window owns its scroll bar directly. Parity tests put this beside a
// C++ CCandidateWindow to compare it with CScrollBarWindow.
//
// CRustScrollBarWindow implements IDimeScrollBar (ScrollBarWindow.h) over a
// Rust handle. CBaseWindow keeps a scroll bar's capture and timers on the
// candidate window; the Rust scroll bar asks the candidate through
// DimeSbParent callbacks, and the candidate forwards the WM_TIMERs it gets to
// SbOnTimer.

#pragma once

#include "ScrollBarWindow.h"

#ifdef DIME_RUST_AVAILABLE
#include "dime_ffi.h"

class CRustScrollBarWindow : public IDimeScrollBar
{
public:
    // parent: the candidate window's capture / timer callbacks (ctx: the candidate).
    explicit CRustScrollBarWindow(const DimeSbParent& parent) : _parent(parent), _sb(nullptr) { }
    virtual ~CRustScrollBarWindow();
    CRustScrollBarWindow(const CRustScrollBarWindow&) = delete;
    CRustScrollBarWindow& operator=(const CRustScrollBarWindow&) = delete;

    BOOL SbCreate(ATOM atom, DWORD dwExStyle, DWORD dwStyle, _In_ CBaseWindow* pParent) override;
    HWND SbWnd() override;
    void SbShow(BOOL isShowWnd) override;
    BOOL SbIsEnabled() override;
    BOOL SbGetClientRect(_Inout_ LPRECT lpRect) override;
    BOOL SbGetWindowRect(_Inout_ LPRECT lpRect) override;
    void SbResize(int x, int y, int cx, int cy) override;
    int SbHoverWidth() override;
    void SbSetScrollInfo(_In_ CScrollInfo* lpsi) override;
    void SbShiftLine(int nLine, BOOL isNotify) override;
    void SbShiftPage(int nPage, BOOL isNotify) override;
    void SbShiftPosition(int iPos, BOOL isNotify) override;
    void SbSetCandidateMouseIn(BOOL isIn) override;
    void SbOnScrollActivity() override;
    void SbOnOwnerWndMoved(BOOL isResized) override;
    void SbLButtonDown(POINT pt) override;
    void SbLButtonUp(POINT pt) override;
    void SbMouseMove(POINT pt) override;
    void SbOnTimer(UINT_PTR timerID) override;
    void SbSetSelectionHighlight(RECT rc, int cornerRadius, COLORREF color) override;
#ifdef DIME_UNIT_TESTING
    SbStateForTest SbGetStateForTest() override;
    void SbGetSelectionHighlightForTest(RECT* prc, int* pRadius, COLORREF* pColor) override;
    void SbSetHoveredForTest(BOOL hovered) override;
    void SbPaintForTest(HDC dc, PAINTSTRUCT* pps) override;
    void SbSetCurPosForTest(int nPos, int dwSB) override;
    void SbEnableForTest(BOOL isEnable) override;
#endif

private:
    DimeSbParent _parent;
    DimeScrollBar* _sb;
};
#endif
