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

//#define DEBUG_PRINT
#include "Private.h"
#include "Globals.h"
#include "BaseWindow.h"
#include "ScrollBarWindow.h"
#include "UIConstants.h"
#include "UILayout.h"
#ifdef DIME_RUST_AVAILABLE
#include "dime_ffi.h"
#endif

// CScrollInfo as UILayout sees it.
static UILayout::ScrollInfo _LayoutScrollInfo(const CScrollInfo& si)
{
    return { si.nMax, si.nPage, si.nPos };
}

#ifdef DIME_RUST_AVAILABLE
//+---------------------------------------------------------------------------
//
// The scroll bar's events in Rust (dime-ui::sbproc). The state stays in this
// object; Rust reads and writes it through this host (the notifications to the
// candidate window re-enter the scroll bar). Parity: ScrollBarProcParityTest.
//
//----------------------------------------------------------------------------

struct CScrollBarWindowRustHost
{
    static CScrollBarWindow* S(void* ctx) { return static_cast<CScrollBarWindow*>(ctx); }

    static void* Hwnd(void* ctx) { return S(ctx)->_GetWnd(); }
    static void* ParentHwnd(void* ctx) { return S(ctx)->_GetParent() ? S(ctx)->_GetParent()->_GetWnd() : nullptr; }
    static void ClientRect(void* ctx, DimeRect* out)
    {
        RECT rc = { 0, 0, 0, 0 };
        S(ctx)->_GetClientRect(&rc);
        *out = { rc.left, rc.top, rc.right, rc.bottom };
    }
    static uint32_t Dpi(void* ctx) { return CConfig::GetDpiForHwnd(S(ctx)->_GetWnd()); }
    static int32_t HoverWidth(void* ctx) { return S(ctx)->_GetHoverWidth(); }
    static void GetInfo(void* ctx, DimeScrollInfo* out) { *out = { S(ctx)->_scrollInfo.nMax, S(ctx)->_scrollInfo.nPage, S(ctx)->_scrollInfo.nPos }; }
    static void SetInfo(void* ctx, const DimeScrollInfo* si)
    {
        S(ctx)->_scrollInfo.nMax = si->max;
        S(ctx)->_scrollInfo.nPage = si->page;
        S(ctx)->_scrollInfo.nPos = si->pos;
    }
    static int32_t Dragging(void* ctx, int32_t* startY, int32_t* startPos)
    {
        *startY = S(ctx)->_dragStartY;
        *startPos = S(ctx)->_dragStartPos;
        return S(ctx)->_isDragging ? 1 : 0;
    }
    static void SetDragging(void* ctx, int32_t on, int32_t startY, int32_t startPos)
    {
        S(ctx)->_isDragging = on ? TRUE : FALSE;
        if (on)
        {
            S(ctx)->_dragStartY = startY;
            S(ctx)->_dragStartPos = startPos;
        }
    }
    static uint8_t Alpha(void* ctx) { return S(ctx)->_currentAlpha; }
    static void SetAlpha(void* ctx, uint8_t alpha) { S(ctx)->_currentAlpha = alpha; }
    static int32_t Fading(void* ctx) { return S(ctx)->_isFading ? 1 : 0; }
    static void SetFading(void* ctx, int32_t on) { S(ctx)->_isFading = on ? TRUE : FALSE; }
    static int32_t Hovered(void* ctx) { return S(ctx)->_isHovered ? 1 : 0; }
    static void SetHovered(void* ctx, int32_t on) { S(ctx)->_isHovered = on ? TRUE : FALSE; }
    static int32_t TrackingMouse(void* ctx) { return S(ctx)->_isTrackingMouse ? 1 : 0; }
    static void SetTrackingMouse(void* ctx, int32_t on) { S(ctx)->_isTrackingMouse = on ? TRUE : FALSE; }
    static int32_t HeldButton(void* ctx) { return (int32_t)S(ctx)->_heldButton; }
    static void SetHeldButton(void* ctx, int32_t button) { S(ctx)->_heldButton = (SCROLLBAR_HITTEST)button; }
    static int32_t RepeatStarted(void* ctx) { return S(ctx)->_repeatStarted ? 1 : 0; }
    static void SetRepeatStarted(void* ctx, int32_t on) { S(ctx)->_repeatStarted = on ? TRUE : FALSE; }
    static int32_t IsEnabled(void* ctx) { return S(ctx)->_IsEnabled() ? 1 : 0; }
    static void BaseEnable(void* ctx, int32_t on) { S(ctx)->CBaseWindow::_Enable(on ? TRUE : FALSE); }
    static int32_t IsVisible(void* ctx) { return S(ctx)->_IsWindowVisible() ? 1 : 0; }
    static void BaseShow(void* ctx, int32_t on) { S(ctx)->CBaseWindow::_Show(on ? TRUE : FALSE); }
    static void Invalidate(void* ctx) { S(ctx)->_InvalidateRect(); }
    static int32_t IsCapture(void* ctx) { return S(ctx)->_IsCapture() ? 1 : 0; }
    static void StartCapture(void* ctx) { S(ctx)->_StartCapture(); }
    static void EndCapture(void* ctx) { S(ctx)->_EndCapture(); }
    static void StartTimer(void* ctx, uint32_t elapse, uintptr_t id) { S(ctx)->_StartTimer(elapse, id); }
    static void EndTimer(void* ctx, uintptr_t id) { S(ctx)->_EndTimer(id); }
    static void NotifyCommand(void* ctx, uint32_t msg, int32_t code, int32_t pos) { S(ctx)->_NotifyCommand(msg, (DWORD)code, pos); }
    static void ShiftPage(void* ctx, int32_t pages, int32_t notify) { S(ctx)->_ShiftPage(pages, notify ? TRUE : FALSE); }
    static void Paint(void* ctx, void* dc, void* ps) { S(ctx)->_OnPaint((HDC)dc, (PAINTSTRUCT*)ps); }

    static const DimeSbHostVtbl s_vtbl;
};

const DimeSbHostVtbl CScrollBarWindowRustHost::s_vtbl = {
    Hwnd, ParentHwnd, ClientRect, Dpi, HoverWidth, GetInfo, SetInfo, Dragging, SetDragging, Alpha, SetAlpha,
    Fading, SetFading, Hovered, SetHovered, TrackingMouse, SetTrackingMouse, HeldButton, SetHeldButton,
    RepeatStarted, SetRepeatStarted, IsEnabled, BaseEnable, IsVisible, BaseShow, Invalidate, IsCapture,
    StartCapture, EndCapture, StartTimer, EndTimer, NotifyCommand, ShiftPage, Paint,
};

void CScrollBarWindow::_RustEvent(UINT op, int a, int b)
{
    DimeSbHost host = { &CScrollBarWindowRustHost::s_vtbl, this };
    dime_ui_sb_event(&host, op, a, b);
}
#endif

//////////////////////////////////////////////////////////////////////
//
// CScrollBarWindowFactory class.
//
//////////////////////////////////////////////////////////////////////
CScrollBarWindowFactory* CScrollBarWindowFactory::_instance;

CScrollBarWindowFactory::CScrollBarWindowFactory()
{
    _instance = nullptr;
}

CScrollBarWindowFactory* CScrollBarWindowFactory::Instance()
{
    if (nullptr == _instance)
    {
        _instance = new (std::nothrow) CScrollBarWindowFactory();
    }

    return _instance;
}

CScrollBarWindow* CScrollBarWindowFactory::MakeScrollBarWindow(SHELL_MODE shellMode)
{
    CScrollBarWindow* pScrollBarWindow = nullptr;

    switch (shellMode)
    {
    case STOREAPP:
        pScrollBarWindow = new (std::nothrow) CScrollBarWindow();
        break;

    case DESKTOP:
		pScrollBarWindow = new (std::nothrow) CScrollBarWindow();
        //pScrollBarWindow = new (std::nothrow) CScrollBarNullWindow();
        break;

    default:
        pScrollBarWindow = new (std::nothrow) CScrollBarWindow();
		//pScrollBarWindow = new (std::nothrow) CScrollBarNullWindow();
        break;
    }
    return pScrollBarWindow;
}

void CScrollBarWindowFactory::Release()
{
    if (_instance)
    {
        delete _instance;
        _instance = nullptr;
    }
}

//////////////////////////////////////////////////////////////////////
//
// CScrollBarWindow class.
//
//////////////////////////////////////////////////////////////////////+---------------------------------------------------------------------------
//
// ctor
//
//----------------------------------------------------------------------------

CScrollBarWindow::CScrollBarWindow()
{
    _scrollDir = SCROLL_NONE_DIR;

    _isDragging = FALSE;
    _dragStartY = 0;
    _dragStartPos = 0;

    _currentAlpha = CConfig::GetAlwaysShowScrollbars() ? FADE_ALPHA_VISIBLE : 0;
    _isFading = FALSE;
    _lastActivityTick = 0;

    _isHovered = FALSE;
    _isTrackingMouse = FALSE;

    _heldButton = SB_HIT_NONE;
    _repeatStarted = FALSE;
#ifdef DIME_USE_RUST_ENGINE
    _useRust = TRUE;
#else
    _useRust = FALSE;
#endif

    SetRectEmpty(&_selHighlightRect);
    _selHighlightRadius = -1;
    _selHighlightColor = 0;
}

//+---------------------------------------------------------------------------
//
// dtor
//
//----------------------------------------------------------------------------

CScrollBarWindow::~CScrollBarWindow()
{
}

//+---------------------------------------------------------------------------
//
// _Create
//
//----------------------------------------------------------------------------

BOOL CScrollBarWindow::_Create(ATOM atom, DWORD dwExStyle, DWORD dwStyle, CBaseWindow *pParent, int wndWidth, int wndHeight)
{
	debugPrint(L"CScrollBarWindow::_Create() entered, atom=%d, pParent=%p", atom, pParent);
    // Add WS_EX_LAYERED for alpha fade support.
    // Some legacy apps reject WS_EX_LAYERED on child windows — fall back without it.
    BOOL created = CBaseWindow::_Create(atom, dwExStyle | WS_EX_LAYERED, dwStyle, pParent, wndWidth, wndHeight);
    if (!created)
    {
        debugPrint(L"CScrollBarWindow::_Create() layered child failed (err=%d), retrying without WS_EX_LAYERED", GetLastError());
        created = CBaseWindow::_Create(atom, dwExStyle, dwStyle, pParent, wndWidth, wndHeight);
    }
    if (!created)
    {
        debugPrint(L"CScrollBarWindow::_Create() CBaseWindow::_Create failed, hwnd=%p, GetLastError=%d", _GetWnd(), GetLastError());
        return FALSE;
    }

    // Set alpha only if the window has WS_EX_LAYERED
    if (GetWindowLong(_GetWnd(), GWL_EXSTYLE) & WS_EX_LAYERED)
        SetLayeredWindowAttributes(_GetWnd(), SCROLLBAR_COLORKEY, _currentAlpha, LWA_COLORKEY | LWA_ALPHA);
    debugPrint(L"CScrollBarWindow::_Create() success, hwnd=%p", _GetWnd());

    return TRUE;
}

//+---------------------------------------------------------------------------
//
// _WindowProcCallback
//
// Scrollbar window proc.
//----------------------------------------------------------------------------

LRESULT CALLBACK CScrollBarWindow::_WindowProcCallback(_In_ HWND wndHandle, _In_ UINT uMsg, _In_ WPARAM wParam, _In_ LPARAM lParam)
{
#ifdef DIME_RUST_AVAILABLE
    if (_useRust)
    {
        DimeSbHost host = { &CScrollBarWindowRustHost::s_vtbl, this };
        return (LRESULT)dime_ui_sb_window_proc(&host, wndHandle, uMsg, (uintptr_t)wParam, (intptr_t)lParam);
    }
#endif
	
    switch (uMsg)
    {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        {
			debugPrint(L"CScrollBarWindow::_WindowProcCallback() : WM_PAINT\n");
            PAINTSTRUCT ps;
            HDC dcHandle = BeginPaint(wndHandle, &ps);

            // Double-buffer
            RECT rcClient = {0,0,0,0};
            GetClientRect(wndHandle, &rcClient);
            int w = rcClient.right - rcClient.left;
            int h = rcClient.bottom - rcClient.top;
            HDC hdcMem = CreateCompatibleDC(dcHandle);
            HBITMAP hBmp = CreateCompatibleBitmap(dcHandle, w, h);
            HBITMAP hOldBmp = (HBITMAP)SelectObject(hdcMem, hBmp);

            PAINTSTRUCT psMem = ps;
            psMem.rcPaint = rcClient;
            _OnPaint(hdcMem, &psMem);

            BitBlt(dcHandle, 0, 0, w, h, hdcMem, 0, 0, SRCCOPY);
            SelectObject(hdcMem, hOldBmp);
            DeleteObject(hBmp);
            DeleteDC(hdcMem);

            EndPaint(wndHandle, &ps);
        }
        return 0;

    case WM_MOUSELEAVE:
        _isTrackingMouse = FALSE;
        return 0;

    case WM_TIMER:
        if (wParam == FADE_TIMER_ID)
        {
            if (_currentAlpha > FADE_ALPHA_STEP)
            {
                _currentAlpha -= FADE_ALPHA_STEP;
                SetLayeredWindowAttributes(wndHandle, SCROLLBAR_COLORKEY, _currentAlpha, LWA_COLORKEY | LWA_ALPHA);
            }
            else
            {
                _currentAlpha = 0;
                _isFading = FALSE;
                KillTimer(wndHandle, FADE_TIMER_ID);
                // Hide window — works on all Windows versions including Win7
                // where WS_EX_LAYERED on child windows is not supported
                _Show(FALSE);
            }
        }
        return 0;
    }

    return DefWindowProc(wndHandle, uMsg, wParam, lParam);
}

//+---------------------------------------------------------------------------
//
// _OnPaint
//
//----------------------------------------------------------------------------

void CScrollBarWindow::_OnPaint(_In_ HDC dcHandle, _In_ PAINTSTRUCT *pps)
{
	debugPrint(L"CScrollBarWindow::_OnPaint()\n");
#ifdef DIME_USE_RUST_ENGINE
    _OnPaintRust(dcHandle, pps);
#else
    _OnPaintCpp(dcHandle, pps);
#endif
}

#ifdef DIME_RUST_AVAILABLE
// _OnPaintRust: the same drawing by dime-ui::sbpaint. Parity: CandidatePaintParityTest.
void CScrollBarWindow::_OnPaintRust(_In_ HDC dcHandle, _In_ PAINTSTRUCT *pps)
{
    RECT rcClient = {0,0,0,0};
    _GetClientRect(&rcClient);
    DimeSbPaint in = {};
    in.hwnd = _GetWnd();
    in.has_paint = pps ? 1 : 0;
    if (pps) in.paint = { pps->rcPaint.left, pps->rcPaint.top, pps->rcPaint.right, pps->rcPaint.bottom };
    in.client = { rcClient.left, rcClient.top, rcClient.right, rcClient.bottom };
    in.dpi = CConfig::GetDpiForHwnd(_GetWnd());
    in.si = { _scrollInfo.nMax, _scrollInfo.nPage, _scrollInfo.nPos };
    in.dark = CConfig::GetEffectiveDarkMode() ? 1 : 0;
    in.full = (_isHovered || CConfig::GetAlwaysShowScrollbars()) ? 1 : 0;
    in.highlight = { _selHighlightRect.left, _selHighlightRect.top, _selHighlightRect.right, _selHighlightRect.bottom };
    in.highlight_radius = _selHighlightRadius;
    in.highlight_color = _selHighlightColor;
    dime_ui_sb_paint(dcHandle, &in);
}
#endif

// _OnPaintCpp: the C++ original.
void CScrollBarWindow::_OnPaintCpp(_In_ HDC dcHandle, _In_ PAINTSTRUCT *pps)
{

    // RC26: Fill background — layered windows use magenta color key (becomes transparent);
    // non-layered windows (legacy apps where WS_EX_LAYERED failed) ask the parent for its bg brush.
    if (pps)
    {
        if (GetWindowLong(_GetWnd(), GWL_EXSTYLE) & WS_EX_LAYERED)
        {
            HBRUSH hBg = CreateSolidBrush(SCROLLBAR_COLORKEY);
            FillRect(dcHandle, &pps->rcPaint, hBg);
            DeleteObject(hBg);
        }
        else
        {
            // Non-layered: fill with parent's background brush
            HWND hParent = GetParent(_GetWnd());
            HBRUSH hBg = hParent ? (HBRUSH)SendMessage(hParent, WM_CTLCOLORSCROLLBAR, (WPARAM)dcHandle, (LPARAM)_GetWnd()) : NULL;
            if (!hBg) hBg = (HBRUSH)GetStockObject(BLACK_BRUSH);
            FillRect(dcHandle, &pps->rcPaint, hBg);
        }
    }

    if (_scrollInfo.nMax <= _scrollInfo.nPage) return;

    bool isDark = CConfig::GetEffectiveDarkMode();
    UINT dpi = CConfig::GetDpiForHwnd(_GetWnd());

    if (_isHovered || CConfig::GetAlwaysShowScrollbars())
    {
        // Hovered or always-show: full scrollbar — triangles + pill thumb

        // Arrow triangles
        COLORREF arrowColor = isDark ? RGB(160, 160, 160) : RGB(100, 100, 100);
        HBRUSH hArrowBrush = CreateSolidBrush(arrowColor);
        HPEN hArrowPen = (HPEN)GetStockObject(NULL_PEN);
        HBRUSH hOldBrA = (HBRUSH)SelectObject(dcHandle, hArrowBrush);
        HPEN hOldPenA = (HPEN)SelectObject(dcHandle, hArrowPen);

        // Win11 Explorer style triangles (equilateral, 1px inside the button edges): UILayout::ScrollBarArrows.
        RECT rcArrowClient = {0,0,0,0};
        _GetClientRect(&rcArrowClient);
        UILayout::ScrollArrows arrows = UILayout::ScrollBarArrows(rcArrowClient, dpi);
        Polygon(dcHandle, arrows.up, 3);
        Polygon(dcHandle, arrows.down, 3);

        SelectObject(dcHandle, hOldBrA);
        SelectObject(dcHandle, hOldPenA);
        DeleteObject(hArrowBrush);

        // Pill thumb — single-point top aligned with the triangle apex, 1px inside each
        // triangle side (see UILayout::Cpp::ScrollBarArrows).
        RECT rcThumb = {0,0,0,0};
        _GetThumbRect(&rcThumb);
        rcThumb.left  = arrows.pillLeft;
        rcThumb.right = arrows.pillRight;

        COLORREF thumbColor = isDark ? RGB(180, 180, 180) : RGB(128, 128, 128);
        int radius = arrows.pillRadius;  // full-width corner ellipse → capsule shape
        HBRUSH hThumb = CreateSolidBrush(thumbColor);
        HPEN hPen = (HPEN)GetStockObject(NULL_PEN);
        HBRUSH hOldBr = (HBRUSH)SelectObject(dcHandle, hThumb);
        HPEN hOldPen = (HPEN)SelectObject(dcHandle, hPen);
        RoundRect(dcHandle, rcThumb.left, rcThumb.top, rcThumb.right, rcThumb.bottom, radius, radius);
        SelectObject(dcHandle, hOldBr);
        SelectObject(dcHandle, hOldPen);
        DeleteObject(hThumb);
    }
    else
    {
        // Idle/hiding: thin centered line only, no arrows, full-height track.
        // Use full client rect as track so thumb spans the entire scrollbar height.
        RECT rcClient = {0,0,0,0};
        _GetClientRect(&rcClient);
        // A narrow line centered within the idle scrollbar width.
        RECT rcLine = {0,0,0,0};
        if (!UILayout::ScrollBarIdleThumb(rcClient, dpi, _LayoutScrollInfo(_scrollInfo), &rcLine)) return;
        int lineW = rcLine.right - rcLine.left;

        COLORREF thumbColor = isDark ? RGB(160, 160, 160) : RGB(160, 160, 160);
        int radius = lineW / 2;
        HBRUSH hThumb = CreateSolidBrush(thumbColor);
        HPEN hPen = (HPEN)GetStockObject(NULL_PEN);
        HBRUSH hOldBr = (HBRUSH)SelectObject(dcHandle, hThumb);
        HPEN hOldPen = (HPEN)SelectObject(dcHandle, hPen);
        RoundRect(dcHandle, rcLine.left, rcLine.top, rcLine.right, rcLine.bottom, radius, radius);
        SelectObject(dcHandle, hOldBr);
        SelectObject(dcHandle, hOldPen);
        DeleteObject(hThumb);
    }

    // RC26: Draw selection highlight on top of all scrollbar elements so the
    // candidate's rounded highlight visually extends over the scrollbar.
    // Only needed on non-layered windows (legacy apps); on layered windows
    // the color key transparency handles the scrollbar background.
    if (_selHighlightRadius >= 0 && !(GetWindowLong(_GetWnd(), GWL_EXSTYLE) & WS_EX_LAYERED))
    {
        HBRUSH hSel = CreateSolidBrush(_selHighlightColor);
        HPEN hSelPen = (HPEN)GetStockObject(NULL_PEN);
        HBRUSH hOldBr = (HBRUSH)SelectObject(dcHandle, hSel);
        HPEN hOldPen = (HPEN)SelectObject(dcHandle, hSelPen);
        RoundRect(dcHandle, _selHighlightRect.left, _selHighlightRect.top,
                  _selHighlightRect.right, _selHighlightRect.bottom,
                  _selHighlightRadius, _selHighlightRadius);
        SelectObject(dcHandle, hOldBr);
        SelectObject(dcHandle, hOldPen);
        DeleteObject(hSel);
    }
}

//+---------------------------------------------------------------------------
//
// _OnLButtonDown
//
//----------------------------------------------------------------------------

void CScrollBarWindow::_OnLButtonDown(POINT pt)
{
#ifdef DIME_RUST_AVAILABLE
    if (_useRust)
    {
        _RustEvent(6, pt.x, pt.y);
        return;
    }
#endif
    _StartCapture();

    SCROLLBAR_HITTEST hit = _HitTest(pt);

    switch (hit)
    {
    case SB_HIT_BTN_UP:
        _NotifyCommand(WM_VSCROLL, SB_PAGEUP, 0);
        _ShiftPage(-1, FALSE);
        _heldButton = SB_HIT_BTN_UP;
        _repeatStarted = FALSE;
        _StartTimer(BTNREPEAT_DELAY_MS, BTNREPEAT_TIMER_ID);
        break;

    case SB_HIT_BTN_DOWN:
        _NotifyCommand(WM_VSCROLL, SB_PAGEDOWN, 0);
        _ShiftPage(+1, FALSE);
        _heldButton = SB_HIT_BTN_DOWN;
        _repeatStarted = FALSE;
        _StartTimer(BTNREPEAT_DELAY_MS, BTNREPEAT_TIMER_ID);
        break;

    case SB_HIT_THUMB:
        _isDragging = TRUE;
        _dragStartY = pt.y;
        _dragStartPos = _scrollInfo.nPos;
        break;

    case SB_HIT_TRACK_ABOVE:
        _ShiftPage(-1, TRUE);
        break;

    case SB_HIT_TRACK_BELOW:
        _ShiftPage(+1, TRUE);
        break;

    default:
        break;
    }
}

//+---------------------------------------------------------------------------
//
// _OnLButtonUp
//
//----------------------------------------------------------------------------

void CScrollBarWindow::_OnLButtonUp(POINT pt)
{
#ifdef DIME_RUST_AVAILABLE
    if (_useRust)
    {
        _RustEvent(7, 0, 0);
        return;
    }
#endif
    pt;
    if (_isDragging)
    {
        _isDragging = FALSE;
        _SetCurPos(_scrollInfo.nPos, SB_THUMBPOSITION);
    }

    if (_heldButton != SB_HIT_NONE)
    {
        _EndTimer(BTNREPEAT_TIMER_ID);
        _heldButton = SB_HIT_NONE;
        _repeatStarted = FALSE;
    }

    if (_IsCapture())
    {
        _EndCapture();
    }

    _scrollDir = SCROLL_NONE_DIR;
    _InvalidateRect();
}

//+---------------------------------------------------------------------------
//
// _OnMouseMove
//
//----------------------------------------------------------------------------

void CScrollBarWindow::_OnMouseMove(POINT pt)
{
#ifdef DIME_RUST_AVAILABLE
    if (_useRust)
    {
        _RustEvent(8, 0, 0);
        return;
    }
#endif
    pt;
    // Register for WM_MOUSELEAVE if not already tracking
    if (!_isTrackingMouse)
    {
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, _GetWnd(), 0 };
        TrackMouseEvent(&tme);
        _isTrackingMouse = TRUE;
    }

    if (!_isHovered)
    {
        _isHovered = TRUE;
        _OnScrollActivity();
        _InvalidateRect();
    }

    if (_isDragging)
    {
        // Get cursor in scrollbar client coords
        POINT ptClient;
        GetCursorPos(&ptClient);
        MapWindowPoints(HWND_DESKTOP, _GetWnd(), &ptClient, 1);

        RECT rc = {0,0,0,0};
        _GetClientRect(&rc);
        int newPos = 0;
        if (UILayout::ScrollBarDragPos(rc, CConfig::GetDpiForHwnd(_GetWnd()), _LayoutScrollInfo(_scrollInfo),
                _dragStartPos, ptClient.y - _dragStartY, &newPos))
        {
            _SetCurPos(newPos, SB_THUMBTRACK);
        }
    }
}

//+---------------------------------------------------------------------------
//
// _OnOwnerWndMoved
//
//----------------------------------------------------------------------------

void CScrollBarWindow::_OnOwnerWndMoved(BOOL isResized)
{
#ifdef DIME_RUST_AVAILABLE
    if (_useRust)
    {
        _RustEvent(10, 0, 0);
        return;
    }
#endif
    isResized;

    if (IsWindow(_GetWnd()) && IsWindowVisible(_GetWnd()))
    {
        _AdjustWindowPos();
    }
}

//+---------------------------------------------------------------------------
//
// _Resize
//
//----------------------------------------------------------------------------

void CScrollBarWindow::_Resize(int x, int y, int cx, int cy)
{
    CBaseWindow::_Resize(x, y, cx, cy);
}

//+---------------------------------------------------------------------------
//
// _Show
//
//----------------------------------------------------------------------------

void CScrollBarWindow::_Show(BOOL isShowWnd)
{
#ifdef DIME_RUST_AVAILABLE
    if (_useRust)
    {
        _RustEvent(11, isShowWnd ? 1 : 0, 0);
        return;
    }
#endif
    if (_IsWindowVisible() != isShowWnd)
    {
        CBaseWindow::_Show(isShowWnd);
    }
}

//+---------------------------------------------------------------------------
//
// _Enable
//
//----------------------------------------------------------------------------

void CScrollBarWindow::_Enable(BOOL isEnable)
{
#ifdef DIME_RUST_AVAILABLE
    if (_useRust)
    {
        _RustEvent(12, isEnable ? 1 : 0, 0);
        return;
    }
#endif
	debugPrint(L"CScrollBarWindow::_Enable(), isEnable = %d, _scrollInfo.nMax = %d, _scrollInfo.nPage =%d, _scrollInfo.nPos = %d ",
		isEnable, _scrollInfo.nMax, _scrollInfo.nPage, _scrollInfo.nPos);
    if (_IsEnabled() != isEnable)
    {
        CBaseWindow::_Enable(isEnable);
    }
}

//+---------------------------------------------------------------------------
//
// _AdjustWindowPos
//
//----------------------------------------------------------------------------

void CScrollBarWindow::_AdjustWindowPos()
{
    if (!IsWindow(_GetWnd()))
    {
        return;
    }

    RECT rc = {0, 0, 0, 0};
    CBaseWindow* pParent = _GetParent();
    if (pParent == nullptr)
    {
        return;
    }

    GetWindowRect(pParent->_GetWnd(), &rc);
	int sbWidth = _GetHoverWidth();  // always hover width; idle vs hover is paint-only
	RECT rcSB = UILayout::ScrollBarAttachedRect(rc.right - rc.left, rc.bottom - rc.top, sbWidth);
	SetWindowPos(_GetWnd(), pParent->_GetWnd(),
		rcSB.left, rcSB.top, rcSB.right - rcSB.left, rcSB.bottom - rcSB.top,
        SWP_NOOWNERZORDER | SWP_NOACTIVATE);
}

//+---------------------------------------------------------------------------
//
// _SetScrollInfo
//
//----------------------------------------------------------------------------

void CScrollBarWindow::_SetScrollInfo(_In_ CScrollInfo *lpsi)
{
#ifdef DIME_RUST_AVAILABLE
    if (_useRust)
    {
        DimeSbHost host = { &CScrollBarWindowRustHost::s_vtbl, this };
        DimeScrollInfo si = { lpsi->nMax, lpsi->nPage, lpsi->nPos };
        dime_ui_sb_set_scroll_info(&host, &si);
        return;
    }
#endif
    _scrollInfo = *lpsi;

    BOOL isEnable = (_scrollInfo.nMax > _scrollInfo.nPage);

    _scrollDir = SCROLL_NONE_DIR;

    _SetCurPos(_scrollInfo.nPos, -1);

	_Enable(isEnable);

    if (isEnable)
        _OnScrollActivity();   // show thin line immediately
    else
        _Show(FALSE);          // single-page: hide completely
}

//+---------------------------------------------------------------------------
//
// _GetScrollInfo
//
//----------------------------------------------------------------------------

void CScrollBarWindow::_GetScrollInfo(_Out_ CScrollInfo *lpsi)
{
    *lpsi = _scrollInfo;
}

//+---------------------------------------------------------------------------
//
// _GetScrollArea — now same as full client rect (no buttons)
//
//----------------------------------------------------------------------------

// The parts of the scroll bar in client coordinates: UILayout::ScrollBarParts.
UILayout::ScrollParts CScrollBarWindow::_GetParts()
{
    RECT rc = {0,0,0,0};
    _GetClientRect(&rc);
    return UILayout::ScrollBarParts(rc, CConfig::GetDpiForHwnd(_GetWnd()), _LayoutScrollInfo(_scrollInfo));
}

BOOL CScrollBarWindow::_GetBtnUpRect(_Out_ RECT *prc)
{
    if (prc == nullptr) return FALSE;
    *prc = _GetParts().up;
    return TRUE;
}

BOOL CScrollBarWindow::_GetBtnDnRect(_Out_ RECT *prc)
{
    if (prc == nullptr) return FALSE;
    *prc = _GetParts().down;
    return TRUE;
}

void CScrollBarWindow::_GetScrollArea(_Out_ RECT *prc)
{
    if (prc == nullptr) return;
    *prc = {0,0,0,0};
    _GetClientRect(prc);
}

//+---------------------------------------------------------------------------
//
// _GetTrackRect — full client rect (no buttons in thin scrollbar)
//
//----------------------------------------------------------------------------

BOOL CScrollBarWindow::_GetTrackRect(_Out_ RECT *prc)
{
    if (prc == nullptr) return FALSE;
    *prc = _GetParts().track;
    return TRUE;
}

//+---------------------------------------------------------------------------
//
// _GetThumbRect — proportionally-sized thumb within the track
//
//----------------------------------------------------------------------------

BOOL CScrollBarWindow::_GetThumbRect(_Out_ RECT *prc)
{
    if (prc == nullptr) return FALSE;
    *prc = _GetParts().thumb;
    return TRUE;
}

//+---------------------------------------------------------------------------
//
// _HitTest — determine which part of the scrollbar a point is in
//
//----------------------------------------------------------------------------

SCROLLBAR_HITTEST CScrollBarWindow::_HitTest(POINT pt)
{
    RECT rc = {0,0,0,0};
    _GetClientRect(&rc);
    return (SCROLLBAR_HITTEST)UILayout::ScrollBarHitTest(rc, CConfig::GetDpiForHwnd(_GetWnd()), _LayoutScrollInfo(_scrollInfo), pt);
}

//+---------------------------------------------------------------------------
//
// _SetCurPos
//
// param: dwSB - SB_xxx for WM_VSCROLL or WM_HSCROLL.
//    if -1 is specified, function doesn't send WM_xSCROLL message
//----------------------------------------------------------------------------

void CScrollBarWindow::_SetCurPos(int nPos, int dwSB)
{
#ifdef DIME_RUST_AVAILABLE
    if (_useRust)
    {
        _RustEvent(0, nPos, dwSB);
        return;
    }
#endif
	debugPrint(L"CScrollButtonWindow::_SetCurPos() nPos = %d, dwSB = %d", nPos, dwSB);
    //int posMax = (_scrollInfo.nMax <= _scrollInfo.nPage) ? 0 : _scrollInfo.nMax - _scrollInfo.nPage;

	nPos = UILayout::ScrollBarClampPos(nPos, _scrollInfo.nMax);

    _scrollInfo.nPos = nPos;

	BOOL enabled = _scrollInfo.nMax > _scrollInfo.nPage;
	_Enable(enabled);

    if (enabled && _IsWindowVisible())
	{
        _InvalidateRect();
    }

    if ((_IsCapture() && dwSB != -1) || (dwSB == SB_THUMBPOSITION) || (dwSB == SB_THUMBTRACK))
    {
        _NotifyCommand(WM_VSCROLL, dwSB, nPos);
    }
}

//+---------------------------------------------------------------------------
//
// _SetCandidateMouseIn — called by candidate window on mouse enter/leave
//
//----------------------------------------------------------------------------

void CScrollBarWindow::_SetCandidateMouseIn(BOOL isIn)
{
#ifdef DIME_RUST_AVAILABLE
    if (_useRust)
    {
        _RustEvent(5, isIn ? 1 : 0, 0);
        return;
    }
#endif
    if (isIn)
    {
        // Mouse entered scrollbar column: show full scrollbar immediately.
        _EndTimer(UI::SCROLLBAR_COLLAPSE_TIMER_ID);   // cancel any pending collapse
        if (!_isHovered)
        {
            _isHovered = TRUE;
            _InvalidateRect();
        }
    }
    else
    {
        // Mouse left scrollbar column: schedule collapse after 2s delay.
        _StartTimer(UI::SCROLLBAR_COLLAPSE_DELAY_MS, UI::SCROLLBAR_COLLAPSE_TIMER_ID);
    }
}

//+---------------------------------------------------------------------------
//
// _SetSelectionHighlight — RC26: candidate passes selected row rect for non-layered bg
//
//----------------------------------------------------------------------------

void CScrollBarWindow::_SetSelectionHighlight(RECT rc, int cornerRadius, COLORREF color)
{
    _selHighlightRect = rc;
    _selHighlightRadius = cornerRadius;
    _selHighlightColor = color;
}

//+---------------------------------------------------------------------------
//
// _OnTimerID — dispatched by CBaseWindow for non-default timer IDs
//
//----------------------------------------------------------------------------

void CScrollBarWindow::_OnTimerID(UINT_PTR timerID)
{
#ifdef DIME_RUST_AVAILABLE
    if (_useRust)
    {
        _RustEvent(9, (int)timerID, 0);
        return;
    }
#endif
    if (timerID == BTNREPEAT_TIMER_ID)
    {
        if (!_repeatStarted)
        {
            _repeatStarted = TRUE;
            _EndTimer(BTNREPEAT_TIMER_ID);
            _StartTimer(BTNREPEAT_SPEED_MS, BTNREPEAT_TIMER_ID);
        }
        if (_heldButton == SB_HIT_BTN_UP)
        {
            _NotifyCommand(WM_VSCROLL, SB_PAGEUP, 0);
            _ShiftPage(-1, FALSE);
        }
        else if (_heldButton == SB_HIT_BTN_DOWN)
        {
            _NotifyCommand(WM_VSCROLL, SB_PAGEDOWN, 0);
            _ShiftPage(+1, FALSE);
        }
    }
    else if (timerID == UI::SCROLLBAR_COLLAPSE_TIMER_ID)
    {
        _EndTimer(UI::SCROLLBAR_COLLAPSE_TIMER_ID);
        if (_isHovered)
        {
            _isHovered = FALSE;
            _InvalidateRect();
        }
    }
}

//+---------------------------------------------------------------------------
//
// _OnScrollActivity — called when scroll or hover occurs; shows scrollbar
//
//----------------------------------------------------------------------------

void CScrollBarWindow::_OnScrollActivity()
{
#ifdef DIME_RUST_AVAILABLE
    if (_useRust)
    {
        _RustEvent(4, 0, 0);
        return;
    }
#endif
    // Don't show scrollbar for single-page candidate lists
    if (!_IsEnabled()) return;

    // Kill any lingering fade timer
    if (_isFading)
    {
        KillTimer(_GetWnd(), FADE_TIMER_ID);
        _isFading = FALSE;
    }

    // Show the window and ensure it is at full alpha
    if (!_IsWindowVisible())
        _Show(TRUE);

    if (_currentAlpha < FADE_ALPHA_VISIBLE)
    {
        _currentAlpha = FADE_ALPHA_VISIBLE;
        if (_GetWnd())
            SetLayeredWindowAttributes(_GetWnd(), SCROLLBAR_COLORKEY, _currentAlpha, LWA_COLORKEY | LWA_ALPHA);
    }
    // No fade timer — scrollbar stays visible as thin line until disabled
}

//+---------------------------------------------------------------------------
//
// _GetIdleWidth / _GetHoverWidth — DPI-scaled scrollbar widths
//
//----------------------------------------------------------------------------

int CScrollBarWindow::_GetIdleWidth()
{
    UINT dpi = CConfig::GetDpiForHwnd(_GetWnd());
    return CConfig::GetAlwaysShowScrollbars()
        ? UILayout::ScaleForDpi(SCROLLBAR_HOVER_WIDTH, dpi)
        : UILayout::ScaleForDpi(SCROLLBAR_THIN_WIDTH, dpi);
}

int CScrollBarWindow::_GetHoverWidth()
{
    UINT dpi = CConfig::GetDpiForHwnd(_GetWnd());
    return UILayout::ScaleForDpi(SCROLLBAR_HOVER_WIDTH, dpi);
}


//////////////////////////////////////////////////////////////////////
//
// CScrollButtonWindow class.
//
//////////////////////////////////////////////////////////////////////+---------------------------------------------------------------------------
//
// _OnPaint
//
//----------------------------------------------------------------------------

void CScrollButtonWindow::_OnPaint(_In_ HDC dcHandle, _In_ PAINTSTRUCT *pps)
{
	debugPrint(L"CScrollButtonWindow::_OnPaint()\n");
    pps;

    RECT rc = {0, 0, 0, 0};

    _GetClientRect(&rc);

    DrawFrameControl(dcHandle, &rc, DFC_SCROLL, DFCS_FLAT | subTypeOfControl | typeOfControl | (!_IsEnabled() ? DFCS_INACTIVE : 0));
}

//+---------------------------------------------------------------------------
//
// _OnLButtonDown
//
//----------------------------------------------------------------------------

void CScrollButtonWindow::_OnLButtonDown(POINT pt)
{
    CButtonWindow::_OnLButtonDown(pt);

    CScrollBarWindow* pParent = (CScrollBarWindow*)_GetParent();    // more secure w/ dynamic_cast
    if (pParent == nullptr)
    {
        return;
    }

    switch (subTypeOfControl)
    {
    case DFCS_SCROLLDOWN:
        _NotifyCommand(WM_VSCROLL, SB_PAGEDOWN, 0);
        pParent->_ShiftPage(+1, FALSE);
        break;
    case DFCS_SCROLLUP:
        _NotifyCommand(WM_VSCROLL, SB_PAGEUP, 0);
        pParent->_ShiftPage(-1, FALSE);
        break;
    }

    _StartTimer(_GetScrollDelay());
}

//+---------------------------------------------------------------------------
//
// _OnLButtonUp
//
//----------------------------------------------------------------------------

void CScrollButtonWindow::_OnLButtonUp(POINT pt)
{
    CButtonWindow::_OnLButtonUp(pt);

    _InvalidateRect();

    if (_IsTimer())
    {
        _EndTimer();
    }
}

//+---------------------------------------------------------------------------
//
// _OnTimer : Speed up page Down/Up while holding Down/Up Button
//
//----------------------------------------------------------------------------

void CScrollButtonWindow::_OnTimer()
{
    POINT pt = {0, 0};
    CScrollBarWindow* pParent = (CScrollBarWindow*)_GetParent();    // more secure w/ dynamic_cast
    if (pParent == nullptr)
    {
        return;
    }

    // start another faster timer
    _StartTimer(_GetScrollSpeed());

    GetCursorPos(&pt);
    MapWindowPoints(HWND_DESKTOP, pParent->_GetWnd(), &pt, 1);

    RECT rc = {0, 0, 0, 0};

    _GetClientRect(&rc);

    if (PtInRect(&rc, pt))
    {
        switch (subTypeOfControl)
        {
        case DFCS_SCROLLDOWN:
            _NotifyCommand(WM_VSCROLL, SB_LINEDOWN, 0);
            pParent->_ShiftLine(+1, FALSE);
            break;
        case DFCS_SCROLLUP:
            _NotifyCommand(WM_VSCROLL, SB_LINEUP, 0);
            pParent->_ShiftLine(-1, FALSE);
            break;
        }
    }
}