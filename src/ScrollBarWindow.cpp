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

    // Fill background with the transparent color key — these pixels become fully transparent.
    if (pps)
    {
        HBRUSH hBg = CreateSolidBrush(SCROLLBAR_COLORKEY);
        FillRect(dcHandle, &pps->rcPaint, hBg);
        DeleteObject(hBg);
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

        RECT rcUp = {0,0,0,0};
        _GetBtnUpRect(&rcUp);
        int btnW = rcUp.right - rcUp.left;
        // Win11 Explorer style: triangle spans ~60% of button width, flat wedge aspect
        // Triangle fits within button width with 1px margin each side; equilateral proportions
        int aw = max(2, btnW / 3);
        int ah = max(MulDiv(2, dpi, USER_DEFAULT_SCREEN_DPI), aw * 87 / 100); // equilateral: ah = aw * sqrt(3)/2
        int acx = (rcUp.left + rcUp.right) / 2;
        int acy = rcUp.top + ah + 1;             // apex 1px inside top edge
        POINT upTri[3] = { {acx, acy - ah}, {acx - aw, acy + ah}, {acx + aw, acy + ah} };
        Polygon(dcHandle, upTri, 3);

        RECT rcDn = {0,0,0,0};
        _GetBtnDnRect(&rcDn);
        acx = (rcDn.left + rcDn.right) / 2;
        acy = rcDn.bottom - ah - 1;              // apex 1px inside bottom edge
        POINT dnTri[3] = { {acx, acy + ah}, {acx - aw, acy - ah}, {acx + aw, acy - ah} };
        Polygon(dcHandle, dnTri, 3);

        SelectObject(dcHandle, hOldBrA);
        SelectObject(dcHandle, hOldPenA);
        DeleteObject(hArrowBrush);

        // Pill thumb — single-point top aligned with triangle apex at acx.
        // RoundRect corner ellipse: left arc center = left + radius/2, right arc center = right - radius/2.
        // For both to equal acx (single pixel at top matching triangle apex):
        //   left + radius/2 = acx  AND  right - radius/2 = acx
        //   → left = acx - aw, right = acx + aw, radius = 2*aw (full pill width as ellipse diameter).
        RECT rcThumb = {0,0,0,0};
        _GetThumbRect(&rcThumb);
        // Pill narrower than triangle: use (aw-1) as half-width so pill insets 1px inside each triangle side.
        // GDI half-integer rule: center=(left+right-1)/2 must be acx+0.5 → left+right=2*acx+2.
        // With half-width pw=aw-1: left=acx-pw+1, right=acx+pw+1. Topmost pixel at x=acx. ✓
        int pw = max(2, aw - 1);
        rcThumb.left  = acx - pw + 1;
        rcThumb.right = acx + pw + 1;

        COLORREF thumbColor = isDark ? RGB(180, 180, 180) : RGB(128, 128, 128);
        int radius = 2 * pw;  // full-width corner ellipse → capsule shape
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
        int trackH = rcClient.bottom - rcClient.top;
        if (trackH <= 0) return;

        int thumbMinH = MulDiv(SCROLLBAR_THUMB_MIN_H, dpi, USER_DEFAULT_SCREEN_DPI);
        int thumbH = MulDiv(trackH, _scrollInfo.nPage, _scrollInfo.nMax);
        thumbH = max(thumbH, thumbMinH);
        thumbH = min(thumbH, trackH);

        int posRange = _scrollInfo.nMax - _scrollInfo.nPage;
        int thumbY = (posRange > 0) ? MulDiv(trackH - thumbH, _scrollInfo.nPos, posRange) : 0;
        thumbY = max(thumbY, 0);
        thumbY = min(thumbY, trackH - thumbH);

        // Center a narrow line within the idle scrollbar width
        int lineW = max(MulDiv(SCROLLBAR_IDLE_THUMB_W, dpi, USER_DEFAULT_SCREEN_DPI), 2);
        int cx = (rcClient.left + rcClient.right) / 2;
        RECT rcLine;
        rcLine.left   = cx - lineW / 2;
        rcLine.right  = rcLine.left + lineW;
        rcLine.top    = rcClient.top + thumbY;
        rcLine.bottom = rcLine.top + thumbH;

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
}

//+---------------------------------------------------------------------------
//
// _OnLButtonDown
//
//----------------------------------------------------------------------------

void CScrollBarWindow::_OnLButtonDown(POINT pt)
{
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
        RECT rcTrack = {0,0,0,0};
        _GetTrackRect(&rcTrack);
        int trackH = rcTrack.bottom - rcTrack.top;

        RECT rcThumb = {0,0,0,0};
        _GetThumbRect(&rcThumb);
        int thumbH = rcThumb.bottom - rcThumb.top;

        int slideRange = trackH - thumbH;
        if (slideRange > 0)
        {
            // Get cursor in scrollbar client coords
            POINT ptClient;
            GetCursorPos(&ptClient);
            MapWindowPoints(HWND_DESKTOP, _GetWnd(), &ptClient, 1);

            int deltaY = ptClient.y - _dragStartY;
            int posRange = _scrollInfo.nMax - _scrollInfo.nPage;
            int newPos = _dragStartPos + MulDiv(deltaY, posRange, slideRange);
            newPos = max(newPos, 0);
            newPos = min(newPos, posRange);
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
	int parentW = rc.right - rc.left;
	int parentH = rc.bottom - rc.top;
	SetWindowPos(_GetWnd(), pParent->_GetWnd(),
		parentW - sbWidth - CANDWND_BORDER_WIDTH,
        CANDWND_BORDER_WIDTH,
		sbWidth,
		parentH - CANDWND_BORDER_WIDTH * 2,
        SWP_NOOWNERZORDER | SWP_NOACTIVATE);
}

//+---------------------------------------------------------------------------
//
// _SetScrollInfo
//
//----------------------------------------------------------------------------

void CScrollBarWindow::_SetScrollInfo(_In_ CScrollInfo *lpsi)
{
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

BOOL CScrollBarWindow::_GetBtnUpRect(_Out_ RECT *prc)
{
    if (prc == nullptr) return FALSE;
    RECT rc = {0,0,0,0};
    _GetClientRect(&rc);
    UINT dpi = CConfig::GetDpiForHwnd(_GetWnd());
    int btnH = MulDiv(SCROLLBAR_BTN_HEIGHT, dpi, USER_DEFAULT_SCREEN_DPI);
    prc->left = rc.left;
    prc->top = rc.top;
    prc->right = rc.right;
    prc->bottom = rc.top + min(btnH, (rc.bottom - rc.top) / 3);
    return TRUE;
}

BOOL CScrollBarWindow::_GetBtnDnRect(_Out_ RECT *prc)
{
    if (prc == nullptr) return FALSE;
    RECT rc = {0,0,0,0};
    _GetClientRect(&rc);
    UINT dpi = CConfig::GetDpiForHwnd(_GetWnd());
    int btnH = MulDiv(SCROLLBAR_BTN_HEIGHT, dpi, USER_DEFAULT_SCREEN_DPI);
    prc->left = rc.left;
    prc->top = rc.bottom - min(btnH, (rc.bottom - rc.top) / 3);
    prc->right = rc.right;
    prc->bottom = rc.bottom;
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
    RECT rcUp = {0,0,0,0}, rcDn = {0,0,0,0};
    _GetBtnUpRect(&rcUp);
    _GetBtnDnRect(&rcDn);
    RECT rc = {0,0,0,0};
    _GetClientRect(&rc);
    prc->left   = rc.left;
    prc->top    = rcUp.bottom;
    prc->right  = rc.right;
    prc->bottom = rcDn.top;
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

    RECT rcTrack = {0,0,0,0};
    _GetTrackRect(&rcTrack);

    int trackH = rcTrack.bottom - rcTrack.top;
    if (trackH <= 0 || _scrollInfo.nMax <= 0 || _scrollInfo.nMax <= _scrollInfo.nPage)
    {
        // Thumb fills entire track when everything fits in one page
        *prc = rcTrack;
        return TRUE;
    }

    UINT dpi = CConfig::GetDpiForHwnd(_GetWnd());
    int thumbMinH = MulDiv(SCROLLBAR_THUMB_MIN_H, dpi, USER_DEFAULT_SCREEN_DPI);

    int thumbH = MulDiv(trackH, _scrollInfo.nPage, _scrollInfo.nMax);
    thumbH = max(thumbH, thumbMinH);
    thumbH = min(thumbH, trackH);

    int posRange = _scrollInfo.nMax - _scrollInfo.nPage;
    int thumbY = (posRange > 0) ? MulDiv(trackH - thumbH, _scrollInfo.nPos, posRange) : 0;
    thumbY = max(thumbY, 0);
    thumbY = min(thumbY, trackH - thumbH);

    prc->left   = rcTrack.left;
    prc->top    = rcTrack.top + thumbY;
    prc->right  = rcTrack.right;
    prc->bottom = rcTrack.top + thumbY + thumbH;
    return TRUE;
}

//+---------------------------------------------------------------------------
//
// _HitTest — determine which part of the scrollbar a point is in
//
//----------------------------------------------------------------------------

SCROLLBAR_HITTEST CScrollBarWindow::_HitTest(POINT pt)
{
    RECT rcUp = {0,0,0,0};
    _GetBtnUpRect(&rcUp);
    if (PtInRect(&rcUp, pt)) return SB_HIT_BTN_UP;

    RECT rcDn = {0,0,0,0};
    _GetBtnDnRect(&rcDn);
    if (PtInRect(&rcDn, pt)) return SB_HIT_BTN_DOWN;

    RECT rcThumb = {0,0,0,0};
    _GetThumbRect(&rcThumb);
    if (PtInRect(&rcThumb, pt)) return SB_HIT_THUMB;

    RECT rcTrack = {0,0,0,0};
    _GetTrackRect(&rcTrack);
    if (PtInRect(&rcTrack, pt))
    {
        return (pt.y < rcThumb.top) ? SB_HIT_TRACK_ABOVE : SB_HIT_TRACK_BELOW;
    }

    return SB_HIT_NONE;
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
	debugPrint(L"CScrollButtonWindow::_SetCurPos() nPos = %d, dwSB = %d", nPos, dwSB);
    //int posMax = (_scrollInfo.nMax <= _scrollInfo.nPage) ? 0 : _scrollInfo.nMax - _scrollInfo.nPage;

	nPos = min(nPos, _scrollInfo.nMax-1);// posMax);
    nPos = max(nPos, 0);

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
// _OnTimerID — dispatched by CBaseWindow for non-default timer IDs
//
//----------------------------------------------------------------------------

void CScrollBarWindow::_OnTimerID(UINT_PTR timerID)
{
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
        ? MulDiv(SCROLLBAR_HOVER_WIDTH, dpi, USER_DEFAULT_SCREEN_DPI)
        : MulDiv(SCROLLBAR_THIN_WIDTH, dpi, USER_DEFAULT_SCREEN_DPI);
}

int CScrollBarWindow::_GetHoverWidth()
{
    UINT dpi = CConfig::GetDpiForHwnd(_GetWnd());
    return MulDiv(SCROLLBAR_HOVER_WIDTH, dpi, USER_DEFAULT_SCREEN_DPI);
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