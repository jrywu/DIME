// RustScrollBarWindow.cpp - see RustScrollBarWindow.h

#include "Private.h"
#include "Globals.h"
#include "Config.h"
#include "BaseWindow.h"
#include "RustScrollBarWindow.h"

#ifdef DIME_RUST_AVAILABLE

namespace
{
    uint32_t DpiForHwnd(void* hwnd) { return CConfig::GetDpiForHwnd((HWND)hwnd); }
    int32_t Dark() { return CConfig::GetEffectiveDarkMode() ? 1 : 0; }
    int32_t AlwaysShow() { return CConfig::GetAlwaysShowScrollbars() ? 1 : 0; }
    const DimeSbEnv s_env = { DpiForHwnd, Dark, AlwaysShow };
}

CRustScrollBarWindow::~CRustScrollBarWindow()
{
    dime_ui_scrollbar_free(_sb);
}

BOOL CRustScrollBarWindow::SbCreate(ATOM atom, DWORD dwExStyle, DWORD dwStyle, _In_ CBaseWindow* pParent)
{
    _parent.hwnd = pParent ? pParent->_GetWnd() : nullptr;
    _sb = dime_ui_scrollbar_create(Global::dllInstanceHandle, atom, dwExStyle, dwStyle, &s_env, &_parent);
    return _sb != nullptr;
}

HWND CRustScrollBarWindow::SbWnd() { return (HWND)dime_ui_scrollbar_hwnd(_sb); }
void CRustScrollBarWindow::SbShow(BOOL isShowWnd) { dime_ui_scrollbar_op(_sb, 0, isShowWnd ? 1 : 0, 0); }
BOOL CRustScrollBarWindow::SbIsEnabled() { return dime_ui_scrollbar_is_enabled(_sb) ? TRUE : FALSE; }

// CBaseWindow::_GetClientRect / _GetWindowRect for a real window.
BOOL CRustScrollBarWindow::SbGetClientRect(_Inout_ LPRECT lpRect) { return GetClientRect(SbWnd(), lpRect); }
BOOL CRustScrollBarWindow::SbGetWindowRect(_Inout_ LPRECT lpRect) { return GetWindowRect(SbWnd(), lpRect); }

void CRustScrollBarWindow::SbResize(int x, int y, int cx, int cy) { dime_ui_scrollbar_resize(_sb, x, y, cx, cy); }
int CRustScrollBarWindow::SbHoverWidth() { return dime_ui_scrollbar_hover_width(_sb); }

void CRustScrollBarWindow::SbSetScrollInfo(_In_ CScrollInfo* lpsi)
{
    if (!lpsi) return;
    DimeScrollInfo si = { lpsi->nMax, lpsi->nPage, lpsi->nPos };
    dime_ui_scrollbar_set_scroll_info(_sb, &si);
}

void CRustScrollBarWindow::SbShiftLine(int nLine, BOOL isNotify) { dime_ui_scrollbar_op(_sb, 1, nLine, isNotify ? 1 : 0); }
void CRustScrollBarWindow::SbShiftPage(int nPage, BOOL isNotify) { dime_ui_scrollbar_op(_sb, 2, nPage, isNotify ? 1 : 0); }
void CRustScrollBarWindow::SbShiftPosition(int iPos, BOOL isNotify) { dime_ui_scrollbar_op(_sb, 3, iPos, isNotify ? 1 : 0); }
void CRustScrollBarWindow::SbSetCandidateMouseIn(BOOL isIn) { dime_ui_scrollbar_op(_sb, 4, isIn ? 1 : 0, 0); }
void CRustScrollBarWindow::SbOnScrollActivity() { dime_ui_scrollbar_op(_sb, 5, 0, 0); }
void CRustScrollBarWindow::SbOnOwnerWndMoved(BOOL) { dime_ui_scrollbar_op(_sb, 6, 0, 0); }
void CRustScrollBarWindow::SbLButtonDown(POINT pt) { dime_ui_scrollbar_op(_sb, 7, pt.x, pt.y); }
void CRustScrollBarWindow::SbLButtonUp(POINT) { dime_ui_scrollbar_op(_sb, 8, 0, 0); }
void CRustScrollBarWindow::SbMouseMove(POINT) { dime_ui_scrollbar_op(_sb, 9, 0, 0); }
void CRustScrollBarWindow::SbOnTimer(UINT_PTR timerID) { dime_ui_scrollbar_op(_sb, 10, (int32_t)timerID, 0); }

void CRustScrollBarWindow::SbSetSelectionHighlight(RECT rc, int cornerRadius, COLORREF color)
{
    dime_ui_scrollbar_set_highlight(_sb, { rc.left, rc.top, rc.right, rc.bottom }, cornerRadius, color);
}

#ifdef DIME_UNIT_TESTING
SbStateForTest CRustScrollBarWindow::SbGetStateForTest()
{
    DimeSbSnapshot s = {};
    dime_ui_scrollbar_snapshot(_sb, &s);
    SbStateForTest st = {};
    st.si.nMax = s.si.max;
    st.si.nPage = s.si.page;
    st.si.nPos = s.si.pos;
    st.dragging = s.dragging;
    st.dragStartY = s.drag_start_y;
    st.dragStartPos = s.drag_start_pos;
    st.alpha = (BYTE)s.alpha;
    st.fading = s.fading;
    st.hovered = s.hovered;
    st.tracking = s.tracking;
    st.held = s.held;
    st.repeatStarted = s.repeat_started;
    return st;
}

void CRustScrollBarWindow::SbGetSelectionHighlightForTest(RECT* prc, int* pRadius, COLORREF* pColor)
{
    DimeRect r = { 0, 0, 0, 0 };
    int32_t radius = 0;
    uint32_t color = 0;
    dime_ui_scrollbar_highlight(_sb, &r, &radius, &color);
    *prc = { r.left, r.top, r.right, r.bottom };
    *pRadius = radius;
    *pColor = color;
}

void CRustScrollBarWindow::SbSetHoveredForTest(BOOL hovered) { dime_ui_scrollbar_op(_sb, 11, hovered ? 1 : 0, 0); }
void CRustScrollBarWindow::SbSetCurPosForTest(int nPos, int dwSB) { dime_ui_scrollbar_op(_sb, 12, nPos, dwSB); }
void CRustScrollBarWindow::SbEnableForTest(BOOL isEnable) { dime_ui_scrollbar_op(_sb, 13, isEnable ? 1 : 0, 0); }

void CRustScrollBarWindow::SbPaintForTest(HDC dc, PAINTSTRUCT* pps)
{
    if (pps)
    {
        DimeRect r = { pps->rcPaint.left, pps->rcPaint.top, pps->rcPaint.right, pps->rcPaint.bottom };
        dime_ui_scrollbar_paint(_sb, dc, &r);
    }
    else
    {
        dime_ui_scrollbar_paint(_sb, dc, nullptr);
    }
}
#endif

#endif
