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
//#define ALWAYS_SHOW_SCROLL
#include "Globals.h"
#include "UIConstants.h"
#include "BaseWindow.h"
#include "CandidateWindow.h"
#include "UILayout.h"
#include <memory> // Include for smart pointers
#include <vector>
#ifdef DIME_RUST_AVAILABLE
#include "dime_ffi.h"
#endif

#define NO_ANIMATION
//#define NO_WINDOW_SHADOW
//+---------------------------------------------------------------------------
//
// ctor
//
//----------------------------------------------------------------------------

CCandidateWindow::CCandidateWindow(_In_ CANDWNDCALLBACK pfnCallback, _In_ void *pv, _In_ CCandidateRange *pIndexRange, _In_ BOOL isStoreAppMode)
{
	
	debugPrint(L"CCandidateWindow::CCandidateWindow() gdiObjects = %d", GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS));

	_brshBkColor = nullptr;


    _SetTextColor(CANDWND_ITEM_COLOR, GetSysColor(COLOR_WINDOW));    // text color is black
    _SetFillColor(GetSysColor(COLOR_3DHIGHLIGHT));

    _pIndexRange = pIndexRange;
    _model.reset(CCandidateListModel::Create());
    _model->SetIndexRange(pIndexRange);

    _pfnCallback = pfnCallback;
    _pObj = pv;

    _pShadowWnd = nullptr;

    _cyRow = UI::CANDIDATE_ROW_WIDTH;
    _padding = 0;
    _cxTitle = 0;

    _pVScrollBarWnd = nullptr;

    _wndWidth = 0;


    _isStoreAppMode = isStoreAppMode;
    _isTrackingCandMouse = FALSE;

	_fontSize = _TextMetric.tmHeight;

	_x = UI::DEFAULT_WINDOW_X;
	_y = UI::DEFAULT_WINDOW_Y;

	_cornerRadiusBase = 10;

}

//+---------------------------------------------------------------------------
//
// dtor
//
//----------------------------------------------------------------------------

CCandidateWindow::~CCandidateWindow()
{
	if(_brshBkColor) DeleteObject(_brshBkColor);
    _ClearList();
    _DeleteShadowWnd();
    _DeleteVScrollBarWnd();
	debugPrint(L"CCandidateWindow::~CCandidateWindow() gdiObjects = %d", GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS));
}

//+---------------------------------------------------------------------------
//
// _Create
//
// CandidateWindow is the top window
//----------------------------------------------------------------------------

BOOL CCandidateWindow::_Create(_In_ UINT wndWidth, _In_ UINT fontSize, _In_opt_ HWND parentWndHandle)
{
    BOOL ret = FALSE;
    _wndWidth = wndWidth;
	_fontSize = fontSize;

    ret = _CreateMainWindow(parentWndHandle);
    if (FALSE == ret)
    {
        goto Exit;
    }
#ifndef NO_WINDOW_SHADOW
    ret = _CreateBackGroundShadowWindow();
    if (FALSE == ret)
    {
        goto Exit;
    }
#endif

    ret = _CreateVScrollWindow();
    if (FALSE == ret)
    {
        goto Exit;
    }

    _ResizeWindow();

Exit:
    return TRUE;
}


BOOL CCandidateWindow::_CreateMainWindow(_In_opt_ HWND parentWndHandle)
{
    _SetUIWnd(this);

    if (!CBaseWindow::_Create(Global::AtomCandidateWindow,
        WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        WS_POPUP | WS_CLIPCHILDREN,
        NULL, 0, 0, parentWndHandle))
    {
        return FALSE;
    }

    return TRUE;
}

BOOL CCandidateWindow::_CreateBackGroundShadowWindow()
{
    _pShadowWnd = std::make_unique<CShadowWindow>(this);
    if (!_pShadowWnd)
    {
        return FALSE;
    }

    if (!_pShadowWnd->_Create(Global::AtomCandidateShadowWindow,
        WS_EX_TOPMOST | 
		WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        WS_DISABLED | WS_POPUP, this))
    {
        _DeleteShadowWnd();
        return FALSE;
    }


    return TRUE;
}

BOOL CCandidateWindow::_CreateVScrollWindow()
{
    BOOL ret = FALSE;

    SHELL_MODE shellMode = _isStoreAppMode ? STOREAPP : DESKTOP;
    CScrollBarWindowFactory* pFactory = CScrollBarWindowFactory::Instance();
	if(pFactory == nullptr) return ret;
    _pVScrollBarWnd = std::unique_ptr<CScrollBarWindow>(pFactory->MakeScrollBarWindow(shellMode));

    if (!_pVScrollBarWnd)
    {
        goto Exit;
    }

    _pVScrollBarWnd->_SetUIWnd(this);

    debugPrint(L"CCandidateWindow::_CreateVScrollWindow() atom=%d parentHwnd=%p", Global::AtomCandidateScrollBarWindow, _GetWnd());
    SetLastError(0);
    if (!_pVScrollBarWnd->_Create(Global::AtomCandidateScrollBarWindow,
		WS_EX_TOPMOST | WS_EX_TOOLWINDOW , WS_CHILD , this))
    {
        debugPrint(L"CCandidateWindow::_CreateVScrollWindow() FAILED, GetLastError=%d", GetLastError());
        _DeleteVScrollBarWnd();
        goto Exit;
    }
    
    ret = TRUE;

Exit:
    pFactory->Release();
    return ret;
}

// The scroll bar column width (hover width, also without a scroll bar window).
int CCandidateWindow::_GetScrollBarWidth()
{
	return _pVScrollBarWnd ? _pVScrollBarWnd->_GetHoverWidth() : UILayout::ScaleForDpi(SCROLLBAR_HOVER_WIDTH, CConfig::GetDpiForHwnd(_GetWnd()));
}

UINT CCandidateWindow::_GetWidth()
{
	return UILayout::CandWindowSize(_cxTitle, _cyRow, _padding, 0, _GetScrollBarWidth()).cx;
}

void CCandidateWindow::_ResizeWindow()
{

	if(_pIndexRange == nullptr) return;
    int candidateListPageCnt = _pIndexRange->Count();
	int VScrollWidth = _GetScrollBarWidth();
	SIZE total = UILayout::CandWindowSize(_cxTitle, _cyRow, _padding, candidateListPageCnt, VScrollWidth);
	debugPrint(L"CCandidateWindow::_ResizeWindow() _cxTitle=%d pageCnt=%d w=%d h=%d pos=(%d,%d)", _cxTitle, candidateListPageCnt, total.cx, total.cy, _x, _y);
	CBaseWindow::_Resize(_x, _y, total.cx, total.cy);

    RECT rcCandRect = {0, 0, 0, 0};
    _GetClientRect(&rcCandRect);

	if(_pVScrollBarWnd)
	{
		// Shorten scrollbar vertically by the corner arc radius at top and bottom so it
		// never overlaps the rounded corner areas. No SetWindowRgn needed on the scrollbar.
		RECT rcSB = UILayout::CandScrollBarRect(rcCandRect, VScrollWidth, CConfig::GetDpiForHwnd(_GetWnd()), _cornerRadiusBase);
		_pVScrollBarWnd->_Resize(rcSB.left, rcSB.top, rcSB.right - rcSB.left, rcSB.bottom - rcSB.top);
	}
}

//+---------------------------------------------------------------------------
//
// _Move
//
//----------------------------------------------------------------------------

void CCandidateWindow::_Move(int x, int y)
{
	debugPrint(L"CCandidateWindow::_Move() x =%d, y=%d", x,y);
    
	if (_x == x && _y == y) return;

	_x = x;
	_y = y;
	CBaseWindow::_Move(x, y);
    // Animation removed: SetLayeredWindowAttributes auto-adds WS_EX_LAYERED on Win8+,
    // which causes DWM to ignore SetWindowRgn (candidate corners become rectangular).
}

void CCandidateWindow::_OnTimerID(UINT_PTR timerID)
{   //animate the window faded out with layered transparency
	debugPrint(L"CCandidateWindow::_OnTimer(): timerID = %d,  _animationStage = %d", timerID, _animationStage);
	switch (timerID)
	{
	case UI::ANIMATION_TIMER_ID:
		_EndTimer(UI::ANIMATION_TIMER_ID);
		break;
	}
}

//+---------------------------------------------------------------------------
//
// _Show
//
//----------------------------------------------------------------------------

void CCandidateWindow::_Show(BOOL isShowWnd)
{
    debugPrint(L"CCandidateWindow::_Show(%d) hwnd=%p visible=%d pos=(%d,%d)", isShowWnd, _GetWnd(), _GetWnd() ? IsWindowVisible(_GetWnd()) : -1, _x, _y);
    if (isShowWnd)
    {
        // Ensure window has a non-zero size before showing.
        // Without WS_BORDER, a 0x0 popup gets no WM_PAINT, so _ResizeWindow
        // inside _DrawList would never fire.
        RECT rc0 = {0, 0, 0, 0};
        GetClientRect(_GetWnd(), &rc0);
        if (rc0.right - rc0.left == 0 || rc0.bottom - rc0.top == 0)
        {
            debugPrint(L"  _Show: window is 0x0, calling _ResizeWindow()");
            _ResizeWindow();
        }
    }
    if (_pShadowWnd)
		_pShadowWnd->_Show(isShowWnd);
	if (_pVScrollBarWnd)
		_pVScrollBarWnd->_Show(isShowWnd && _pVScrollBarWnd->_IsEnabled());
    CBaseWindow::_Show(isShowWnd);
    if (isShowWnd)
    {
        // Re-apply rounded region AFTER window is visible.
        // Win11 DWM silently discards SetWindowRgn on hidden windows (same as Root Cause #8).
        RECT rc = {0, 0, 0, 0};
        GetClientRect(_GetWnd(), &rc);
        int cx = rc.right - rc.left;
        int cy = rc.bottom - rc.top;
        if (cx > 0 && cy > 0)
        {
            UINT dpi = CConfig::GetDpiForHwnd(_GetWnd());
            int radius = UILayout::ScaleForDpi(_cornerRadiusBase, dpi);
            HRGN hRgn = CreateRoundRectRgn(0, 0, cx + 1, cy + 1, radius, radius);
            SetWindowRgn(_GetWnd(), hRgn, TRUE);
        }
        debugPrint(L"  _Show: after SetWindowRgn, visible=%d, rect=(%d,%d,%d,%d) cx=%d cy=%d",
            IsWindowVisible(_GetWnd()), rc.left, rc.top, rc.right, rc.bottom, cx, cy);
    }
	if (isShowWnd && !_IsCapture())
	{
		SetCapture(_GetWnd());
	}
	if (!isShowWnd && _IsCapture())
	{
		ReleaseCapture();
	}

	// #135: keep candidate above any host WS_EX_TOPMOST popup that opens async
	// after our show (Firefox autocomplete races our show by ~50-500ms). Re-stamp
	// HWND_TOPMOST every STAY_ON_TOP_TICK_MS while window is visible. Using a raw
	// TIMERPROC instead of _StartTimer / _OnTimerID because the latter's WM_TIMER
	// dispatch path silently drops timer events on the candidate HWND for reasons
	// not yet root-caused — but raw TIMERPROC on the same HWND fires reliably
	// (proven by the diagnostic DELAYED Z-snap that lives on this HWND too).
	// SetTimer with an existing ID just resets the period — no stacking.
	if (isShowWnd)
	{
		HWND h = _GetWnd();
		if (h)
		{
			SetTimer(h, UI::STAY_ON_TOP_TIMER_ID, UI::STAY_ON_TOP_TICK_MS,
				[](HWND hwnd, UINT, UINT_PTR, DWORD) -> void
				{
					if (!IsWindow(hwnd) || !IsWindowVisible(hwnd)) return;

					// Only re-stamp when a visible non-trivial NON-DIME window is above
					// us in the topmost band. Walks GW_HWNDPREV chain and ignores:
					// (a) system helpers (tiny rects like ThumbnailDeviceHelperWnd),
					// (b) DIME's own window classes — notify, notify-shadow,
					// candidate-shadow, scrollbar — identified by class atom.
					// Note: PID-based filtering would NOT work because DIME is a TSF
					// IME DLL loaded in-process into the host (firefox.exe), so DIME
					// windows and Firefox's autocomplete popup share the same PID.
					// #135 follow-up: with reverse-lookup notify on, our notify
					// (WS_EX_TOPMOST) sits above the candidate. The pre-fix check
					// treated it as a competitor and re-stamped every 30 ms, cascading
					// through WM_WINDOWPOSCHANGED → shadow _OnOwnerWndMoved (visible
					// flicker) and starving the budget meant to beat Firefox.
					HWND cur = GetWindow(hwnd, GW_HWNDPREV);
					int  depth = 0;
					while (cur && depth < 20)
					{
						if (IsWindowVisible(cur))
						{
							ATOM cls = (ATOM)GetClassWord(cur, GCW_ATOM);
							BOOL isOurs = (cls == Global::AtomCandidateWindow
							            || cls == Global::AtomCandidateShadowWindow
							            || cls == Global::AtomCandidateScrollBarWindow
							            || cls == Global::AtomNotifyWindow
							            || cls == Global::AtomNotifyShadowWindow);
							if (!isOurs)
							{
								RECT rc;
								if (GetWindowRect(cur, &rc))
								{
									int w = rc.right - rc.left;
									int hgt = rc.bottom - rc.top;
									if (w > 8 && hgt > 8)
									{
										// Real non-DIME visible competitor — re-stamp ourselves.
										SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
											SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
										return;
									}
								}
							}
						}
						cur = GetWindow(cur, GW_HWNDPREV);
						depth++;
					}
				});
		}
	}
	else
	{
		HWND h = _GetWnd();
		if (h) KillTimer(h, UI::STAY_ON_TOP_TIMER_ID);
	}
}

//+---------------------------------------------------------------------------
//
// _SetTextColor
// _SetFillColor
//
//----------------------------------------------------------------------------

VOID CCandidateWindow::_SetNumberColor(_In_ COLORREF crColor, _In_ COLORREF crBkColor)
{
    _crNumberColor = _AdjustTextColor(crColor, crBkColor);
    _crNumberBkColor = crBkColor;
}


VOID CCandidateWindow::_SetTextColor(_In_ COLORREF crColor, _In_ COLORREF crBkColor)
{
    _crTextColor = _AdjustTextColor(crColor, crBkColor);
    _crBkColor = crBkColor;
}

VOID CCandidateWindow::_SetSelectedTextColor(_In_ COLORREF crColor, _In_ COLORREF crBkColor)
{
    _crSelectedTextColor = _AdjustTextColor(crColor, crBkColor);
    _crSelectedBkColor = crBkColor;
}


VOID CCandidateWindow::_SetFillColor(_In_ COLORREF fiColor)
{
	if(_brshBkColor) DeleteObject(_brshBkColor);
	_brshBkColor = CreateSolidBrush(fiColor);

}

//+---------------------------------------------------------------------------
//
// _WindowProcCallback
//
// Cand window proc.
//----------------------------------------------------------------------------

LRESULT CALLBACK CCandidateWindow::_WindowProcCallback(_In_ HWND wndHandle, UINT uMsg, _In_ WPARAM wParam, _In_ LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_CREATE:
        {
			debugPrint(L"CCandidateWindow::_WindowProcCallback():WM_CREATE gdiObjects = %d", GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS));
            HDC dcHandle = nullptr;

            dcHandle = GetDC(wndHandle);
            if (dcHandle)
            {
                HFONT hFontOld = (HFONT)SelectObject(dcHandle, Global::defaultlFontHandle);

				PWCHAR pwszTestString = new (std::nothrow) WCHAR[_wndWidth + 1];

				if (pwszTestString == NULL) return 0;

				pwszTestString[0] = L'\0';
				for (UINT i = 0; i < _wndWidth; i++) StringCchCatN(pwszTestString, static_cast<size_t>(_wndWidth) + 1, L"國", 1);

				SIZE candSize;
				GetTextExtentPoint32(dcHandle, pwszTestString, _wndWidth, &candSize); //don't trust the TextMetrics. Measurement the font height and width directly.
				delete[]pwszTestString;

				// _padding matches _DrawList so _ResizeWindow produces consistent height
				UILayout::CandInitialMetrics(candSize, _wndWidth, &_cxTitle, &_cyRow, &_padding);

                SelectObject(dcHandle, hFontOld);
                ReleaseDC(wndHandle, dcHandle);
            }
			debugPrint(L"CCandidateWindow::_WindowProcCallback():WM_CREATE ended. gdiObjects = %d", GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS));
        }
        return 0;

    case WM_DESTROY:
        _DeleteShadowWnd();
        return 0;

    case WM_WINDOWPOSCHANGED:
        {
            WINDOWPOS* pWndPos = (WINDOWPOS*)lParam;

            // move shadow
            if (_pShadowWnd && pWndPos)
            {
                _pShadowWnd->_OnOwnerWndMoved((pWndPos->flags & SWP_NOSIZE) == 0);
            }

            // move v-scroll
            if (_pVScrollBarWnd && pWndPos)
            {
                _pVScrollBarWnd->_OnOwnerWndMoved((pWndPos->flags & SWP_NOSIZE) == 0);
            }
			if(pWndPos)
				_FireMessageToLightDismiss(wndHandle, pWndPos);
        }
        break;

    case WM_WINDOWPOSCHANGING:
        {
            WINDOWPOS* pWndPos = (WINDOWPOS*)lParam;

            // show/hide shadow
            if (_pShadowWnd && pWndPos)
            {
                if ((pWndPos->flags & SWP_HIDEWINDOW) != 0)
                {
                    _pShadowWnd->_Show(FALSE);
                }

                // don't go behind of shadow
                if (((pWndPos->flags & SWP_NOZORDER) == 0) && (pWndPos->hwndInsertAfter == _pShadowWnd->_GetWnd()))
                {
                    pWndPos->flags |= SWP_NOZORDER;
                }

                _pShadowWnd->_OnOwnerWndMoved((pWndPos->flags & SWP_NOSIZE) == 0);
            }

            // show/hide v-scroll
            if (_pVScrollBarWnd && pWndPos)
            {
                if ((pWndPos->flags & SWP_HIDEWINDOW) != 0)
                {
                    _pVScrollBarWnd->_Show(FALSE);
                }

                _pVScrollBarWnd->_OnOwnerWndMoved((pWndPos->flags & SWP_NOSIZE) == 0);
            }
        }
        break;

    case WM_SHOWWINDOW:
        // show/hide shadow
        if (_pShadowWnd)
        {
            _pShadowWnd->_Show((BOOL)wParam);
        }
		if (_pVScrollBarWnd)
		{
			// Only show scrollbar if multi-page; always hide when candidate hides
			_pVScrollBarWnd->_Show((BOOL)wParam && _pVScrollBarWnd->_IsEnabled());
		}
        break;

    case WM_ERASEBKGND:
        return 1;  // suppress background erase to prevent flicker during scrolling

    case WM_CTLCOLORSCROLLBAR:
        // Scrollbar child asks for background brush — return our background brush
        // so its track area matches the candidate window background in all color modes.
        if (_brshBkColor) return (LRESULT)_brshBkColor;
        break;

    case WM_PAINT:
        {
			debugPrint(L"CCandidateWindow::_WindowProcCallback():WM_PAINT gdiObjects = %d", GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS));
            PAINTSTRUCT ps;
            HDC dcHandle = BeginPaint(wndHandle, &ps);

            // Double-buffer: compose entire frame off-screen then blit once
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

            // No custom border drawn on any version — CShadowWindow provides visual
            // separation on all supported Windows versions (Win7+).
            EndPaint(wndHandle, &ps);
			debugPrint(L"CCandidateWindow::_WindowProcCallback():WM_PAINT ended. gdiObjects = %d", GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS));
        }

        return 0;

    case WM_SETCURSOR:
        {
            POINT cursorPoint;

            GetCursorPos(&cursorPoint);
            MapWindowPoints(NULL, wndHandle, &cursorPoint, 1);

            // handle mouse message
            _HandleMouseMsg(HIWORD(lParam), cursorPoint);
        }
        return 1;

    case WM_MOUSELEAVE:
        _isTrackingCandMouse = FALSE;
        if (_pVScrollBarWnd)
            _pVScrollBarWnd->_SetCandidateMouseIn(FALSE);
        return 0;

    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_MBUTTONUP:
    case WM_RBUTTONUP:
        {
            POINT point = { 0, 0 };;

            POINTSTOPOINT(point, MAKEPOINTS(lParam));

            // handle mouse message
            _HandleMouseMsg(uMsg, point);
        }
		// we processes this message, it should return zero. 
        return 0;

    case WM_MOUSEACTIVATE:
        {
            WORD mouseEvent = HIWORD(lParam);
            if (mouseEvent == WM_LBUTTONDOWN || 
                mouseEvent == WM_RBUTTONDOWN || 
                mouseEvent == WM_MBUTTONDOWN) 
            {
                return MA_NOACTIVATE;
            }
        }
        break;

    case WM_POINTERACTIVATE:
        return PA_NOACTIVATE;

    case WM_MOUSEWHEEL:
        {
            short zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
            INT prevSelection = _model->Selection();
            if (zDelta > 0)
                _SetSelectionOffset(-1);
            else if (zDelta < 0)
                _SetSelectionOffset(+1);
            if (_model->Selection() != prevSelection)
            {
                if (_pVScrollBarWnd)
                {
                    _pVScrollBarWnd->_OnScrollActivity();
                    _pVScrollBarWnd->_ShiftPosition(_model->Selection(), FALSE);
                }
                _InvalidateRect();
            }
        }
        return 0;

    case WM_VSCROLL:
        _OnVScroll(LOWORD(wParam), HIWORD(wParam));
        return 0;

    case WM_DPICHANGED:
    {
        // Recreate font at the new monitor DPI
        CConfig::SetDefaultTextFont(wndHandle);
        // Use the suggested new window rect from Windows
        RECT* prcNew = reinterpret_cast<RECT*>(lParam);
        if (prcNew)
        {
            SetWindowPos(wndHandle, NULL,
                prcNew->left, prcNew->top,
                prcNew->right - prcNew->left,
                prcNew->bottom - prcNew->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    }
    }

    return DefWindowProc(wndHandle, uMsg, wParam, lParam);
}

//+---------------------------------------------------------------------------
//
// _HandleMouseMsg
//
//----------------------------------------------------------------------------

void CCandidateWindow::_HandleMouseMsg(_In_ UINT mouseMsg, _In_ POINT point)
{
    switch (mouseMsg)
    {
    case WM_MOUSEMOVE:
        _OnMouseMove(point);
        break;
    case WM_LBUTTONDOWN:
        _OnLButtonDown(point);
        break;
    case WM_LBUTTONUP:
        _OnLButtonUp(point);
        break;
    }
}

//+---------------------------------------------------------------------------
//
// _OnPaint
//
//----------------------------------------------------------------------------

void CCandidateWindow::_OnPaint(_In_ HDC dcHandle, _In_ PAINTSTRUCT *pPaintStruct)
{
	debugPrint(L"CCandidateWindow::_OnPaint()\n");
#ifdef DIME_USE_RUST_ENGINE
	_OnPaintRust(dcHandle, pPaintStruct);
#else
	_OnPaintCpp(dcHandle, pPaintStruct);
#endif
}

#ifdef DIME_RUST_AVAILABLE
//+---------------------------------------------------------------------------
//
// _OnPaintRust: the same drawing by dime-ui::candpaint (GDI from Rust). C++
// gathers the rows and applies the results (layout members, the scroll bar's
// selection highlight, _ResizeWindow between the list and the page indicator,
// as _DrawList does). Parity: CandidatePaintParityTest.
//
//----------------------------------------------------------------------------

static_assert(sizeof(DimeCandColors) == 6 * sizeof(COLORREF), "DimeCandColors layout");

void CCandidateWindow::_OnPaintRust(_In_ HDC dcHandle, _In_ PAINTSTRUCT *pPaintStruct)
{
	if(pPaintStruct == nullptr) return;

	UINT currentPageIndex = 0;
	UINT currentPage = 0;
	BOOL hasPage = SUCCEEDED(_GetCurrentPage(&currentPage));
	if (hasPage)
		_AdjustPageIndex(currentPage, currentPageIndex);
	BOOL drawList = hasPage && _pIndexRange != nullptr;

	int VScrollWidth = _GetScrollBarWidth();
	UINT dpi = CConfig::GetDpiForHwnd(_GetWnd());

	DimeCandPaintList in = {};
	in.font = Global::defaultlFontHandle;
	in.bk_brush = _brshBkColor;
	in.paint = { pPaintStruct->rcPaint.left, pPaintStruct->rcPaint.top, pPaintStruct->rcPaint.right, pPaintStruct->rcPaint.bottom };
	in.draw_list = drawList ? 1 : 0;
	in.wnd_width = _wndWidth;
	in.sb_width = VScrollWidth;
	in.dpi = dpi;
	in.selected_row = -1;
	in.colors = { _crNumberColor, _crNumberBkColor, _crTextColor, _crBkColor, _crSelectedTextColor, _crSelectedBkColor };

	std::vector<DimeCandPaintRow> rows;
	if (drawList)
	{
		int candidateListPageCnt = _pIndexRange->Count();
		in.rows_in_page = candidateListPageCnt;
		for (UINT index = currentPageIndex; index < _model->Count() && (int)rows.size() < candidateListPageCnt; index++)
		{
			DimeCandPaintRow row = {};
			row.index_char = (uint16_t)_pIndexRange->GetAt((int)rows.size())->CandIndex;
			CCandidateListItem* pItem = _model->ItemAt(index);
			if (pItem)
			{
				row.text = (const uint16_t*)pItem->_ItemString.Get();
				row.len = (uint32_t)pItem->_ItemString.GetLength();
				row.has_text = 1;
			}
			if (_model->Selection() == (INT)index)
				in.selected_row = (int32_t)rows.size();
			rows.push_back(row);
		}

		// RC26: the selection highlight is mirrored on a non-layered scroll bar.
		if (_pVScrollBarWnd && _pVScrollBarWnd->_GetWnd() &&
			!(GetWindowLong(_pVScrollBarWnd->_GetWnd(), GWL_EXSTYLE) & WS_EX_LAYERED))
		{
			RECT rcSB = {0};
			_pVScrollBarWnd->_GetWindowRect(&rcSB);
			POINT ptSB = { rcSB.left, rcSB.top };
			ScreenToClient(_GetWnd(), &ptSB);
			in.has_sb_origin = 1;
			in.sb_origin = { ptSB.x, ptSB.y };
		}
	}
	in.rows = rows.empty() ? nullptr : rows.data();
	in.row_count = (uint32_t)rows.size();

	DimeCandPaintListOut out = {};
	dime_ui_cand_paint_list(dcHandle, &in, &out);

	if (out.highlight_state != 0 && _pVScrollBarWnd)
	{
		RECT rcEmpty = {0};
		_pVScrollBarWnd->_SetSelectionHighlight(rcEmpty, -1, 0);
		if (out.highlight_state == 2)
		{
			RECT rcSelSB = { out.highlight.left, out.highlight.top, out.highlight.right, out.highlight.bottom };
			_pVScrollBarWnd->_SetSelectionHighlight(rcSelSB, out.highlight_radius, out.highlight_color);
		}
	}

	if (out.drew_list)
	{
		_cxTitle = out.cx_title;
		_cyRow = out.cy_row;
		_padding = out.padding;
		_wndWidth = out.wnd_width;
		if (out.needs_resize) _ResizeWindow();

		// _DrawPageIndicator: absolute client coordinates, after any resize.
		if (_model->PageCount() > 1)
		{
			RECT rcClient = { 0, 0, 0, 0 };
			_GetClientRect(&rcClient);
			DimeCandPaintIndicator ind = {};
			ind.font = Global::defaultlFontHandle;
			ind.client = { rcClient.left, rcClient.top, rcClient.right, rcClient.bottom };
			ind.cy_row = _cyRow;
			ind.rows_in_page = _pIndexRange->Count();
			ind.sb_width = _GetScrollBarWidth();
			ind.dpi = CConfig::GetDpiForHwnd(_GetWnd());
			ind.current_page = currentPage;
			ind.page_count = (uint32_t)_model->PageCount();
			ind.number_color = _crNumberColor;
			dime_ui_cand_paint_indicator(dcHandle, &ind);
		}
	}

	dime_ui_cand_paint_end(dcHandle, out.old_font);
}
#endif

//+---------------------------------------------------------------------------
//
// _OnPaintCpp: the C++ original.
//
//----------------------------------------------------------------------------

void CCandidateWindow::_OnPaintCpp(_In_ HDC dcHandle, _In_ PAINTSTRUCT *pPaintStruct)
{
	if(pPaintStruct == nullptr) return;

    SetBkMode(dcHandle, TRANSPARENT);

    HFONT hFontOld = (HFONT)SelectObject(dcHandle, Global::defaultlFontHandle);
	
	if(_brshBkColor) 
	{
		FillRect(dcHandle, &pPaintStruct->rcPaint,_brshBkColor);
	}

    UINT currentPageIndex = 0;
    UINT currentPage = 0;

    if (FAILED(_GetCurrentPage(&currentPage)))
    {
        goto cleanup;
    }

    _AdjustPageIndex(currentPage, currentPageIndex);

    _DrawList(dcHandle, currentPageIndex, &pPaintStruct->rcPaint);
    _DrawPageIndicator(dcHandle, currentPage, &pPaintStruct->rcPaint);


cleanup:
    SelectObject(dcHandle, hFontOld);
}

//+---------------------------------------------------------------------------
//
// _OnLButtonDown
//
//----------------------------------------------------------------------------

void CCandidateWindow::_OnLButtonDown(POINT pt)
{
	// Safety check: ensure window is still valid
	if (!IsWindow(_GetWnd()))
	{
		return;
	}

	if(_pIndexRange == nullptr) return;

    RECT rcWindow = {0, 0, 0, 0};;
    _GetClientRect(&rcWindow);

    UINT candidateListPageCnt = _pIndexRange->Count();
    int currentPage = 0;

    if (FAILED(_GetCurrentPage(&currentPage)))
    {
        return;
    }

    // Hit test on list items
    UINT pageStart = _model->PageStart(currentPage);
    int row = UILayout::CandHitRow(rcWindow, _padding, _GetScrollBarWidth(), _cyRow, candidateListPageCnt, pageStart, _model->Count(), pt);
    if (row >= 0 && _pfnCallback)
    {
        SetCursor(LoadCursor(NULL, IDC_HAND));
        _model->SetSelectionRaw(pageStart + row);
        // Store callback pointer before calling to prevent use-after-free
        CANDWNDCALLBACK pfnCallback = _pfnCallback;
        void* pObj = _pObj;
        pfnCallback(pObj, CANDWND_ACTION::CAND_ITEM_SELECT);
        return;
    }

    SetCursor(LoadCursor(NULL, IDC_ARROW));

    if (_pVScrollBarWnd && _pVScrollBarWnd->_IsEnabled())
    {
        RECT rc = {0, 0, 0, 0};

        _pVScrollBarWnd->_GetClientRect(&rc);
        MapWindowPoints(_GetWnd(), _pVScrollBarWnd->_GetWnd(), &pt, 1);

        if (PtInRect(&rc, pt))
        {
            _pVScrollBarWnd->_OnLButtonDown(pt);
			return;
        }
    }
	_pfnCallback(_pObj, CANDWND_ACTION::CAND_CANCEL);
}

//+---------------------------------------------------------------------------
//
// _OnLButtonUp
//
//----------------------------------------------------------------------------

void CCandidateWindow::_OnLButtonUp(POINT pt)
{
    if (nullptr == _pVScrollBarWnd)
    {
        return;
    }

    RECT rc = {0, 0, 0, 0};
    _pVScrollBarWnd->_GetClientRect(&rc);
    MapWindowPoints(_GetWnd(), _pVScrollBarWnd->_GetWnd(), &pt, 1);

    if (_IsCapture())
    {
        _pVScrollBarWnd->_OnLButtonUp(pt);
    }
    else if (PtInRect(&rc, pt))
    {
        _pVScrollBarWnd->_OnLButtonUp(pt);
    }
}

//+---------------------------------------------------------------------------
//
// _OnMouseMove
//
//----------------------------------------------------------------------------

void CCandidateWindow::_OnMouseMove(POINT pt)
{
	if (!_IsCapture())
	{
		SetCapture(_GetWnd());
	}
	RECT rcWindow = { 0, 0, 0, 0 }, rc = { 0, 0, 0, 0 };
	_GetClientRect(&rcWindow);

	int scrollWidth = _GetScrollBarWidth();
	// Mouse in scrollbar column → full scrollbar; outside → thin line after 2s delay (handled in scrollbar).
	if (_pVScrollBarWnd)
	{
		RECT rcScroll = UILayout::CandScrollColumnRect(rcWindow, scrollWidth);
		_pVScrollBarWnd->_SetCandidateMouseIn(PtInRect(&rcScroll, pt));
	}

	if (!_isTrackingCandMouse)
	{
		TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, _GetWnd(), 0 };
		TrackMouseEvent(&tme);
		_isTrackingCandMouse = TRUE;
	}

	rc = UILayout::CandListAreaRect(rcWindow, scrollWidth);

	if (PtInRect(&rc, pt))
	{
		SetCursor(LoadCursor(NULL, IDC_HAND));
	}
	else
	{
		SetCursor(LoadCursor(NULL, IDC_ARROW));

		if (_pVScrollBarWnd)
		{
			_pVScrollBarWnd->_GetClientRect(&rc);
			MapWindowPoints(_GetWnd(), _pVScrollBarWnd->_GetWnd(), &pt, 1);

			if (PtInRect(&rc, pt))
			{
				_pVScrollBarWnd->_OnMouseMove(pt);
			}
		}
	}
}

//+---------------------------------------------------------------------------
//
// _OnVScroll
//
//----------------------------------------------------------------------------

void CCandidateWindow::_OnVScroll(DWORD dwSB, _In_ DWORD nPos)
{
    // _OnVScroll handles scrollbar-originated events only (not keyboard).
    // Scrollbar navigation stops at boundaries (non-circular).
    // Keyboard navigation (via _MovePage directly) wraps circularly.
    INT prevSelection = _model->Selection();
    switch (dwSB)
    {
    case SB_LINEDOWN:
        _SetSelectionOffset(+1);  // stops at last item
        break;
    case SB_LINEUP:
        _SetSelectionOffset(-1);  // stops at first item
        break;
    case SB_PAGEDOWN:
        {
            int currentPage = 0;
            if (SUCCEEDED(_GetCurrentPage(&currentPage)) &&
                currentPage < static_cast<int>(_model->PageCount()) - 1)
                _MovePage(+1, FALSE);  // only if not on last page
        }
        break;
    case SB_PAGEUP:
        {
            int currentPage = 0;
            if (SUCCEEDED(_GetCurrentPage(&currentPage)) && currentPage > 0)
                _MovePage(-1, FALSE);  // only if not on first page
        }
        break;
    case SB_THUMBPOSITION:
        _SetSelection(nPos, FALSE);
        break;
    case SB_THUMBTRACK:
        _SetSelection(nPos, FALSE);
        break;
    }
    // Update scrollbar thumb and repaint only if selection actually changed
    if (_model->Selection() != prevSelection)
    {
        if (_pVScrollBarWnd) _pVScrollBarWnd->_ShiftPosition(_model->Selection(), FALSE);
        _InvalidateRect();
    }
}

//+---------------------------------------------------------------------------
//
// _DrawList
//
//----------------------------------------------------------------------------

void CCandidateWindow::_DrawList(_In_ HDC dcHandle, _In_ UINT currentPageIndex, _In_ RECT *prc)
{
	debugPrint(L"CCandidateWindow::_DrawList(), _wndWidth = %d", _wndWidth);

	if(_pIndexRange == nullptr || prc == nullptr) return;

    int indexInPage = 0;
    int candidateListPageCnt = _pIndexRange->Count();
	int VScrollWidth = _GetScrollBarWidth();
	UINT dpi = CConfig::GetDpiForHwnd(_GetWnd());

    const size_t numStringLen = 2;

	GetTextMetrics(dcHandle, &_TextMetric);
	SIZE candSize = { 0,0 };
	PWCHAR pwszTestString = new (std::nothrow) WCHAR[_wndWidth + 1];
	if (pwszTestString)
	{
		pwszTestString[0] = L'\0';
		for (UINT i = 0; i < _wndWidth; i++) StringCchCatN(pwszTestString, static_cast<size_t>(_wndWidth) + 1, L"國", 1);
		GetTextExtentPoint32(dcHandle, pwszTestString, _wndWidth, &candSize); //don't trust the TextMetrics. Measurement the font height and width directly.
		delete[]pwszTestString;

	}

	// Row metrics, rectangles, truncation and widening: UILayout (CandMeasureMetrics, CandRowLayout, ...).
	const UILayout::CandMetrics m = UILayout::CandMeasureMetrics(candSize, (int)_TextMetric.tmAveCharWidth, _wndWidth);
	_cxTitle = m.cxTitle;
	_cyRow = m.cyLine;
	_padding = m.padding;

    // RC26: Reset scrollbar selection highlight before drawing rows (non-layered only)
    if (_pVScrollBarWnd && _pVScrollBarWnd->_GetWnd() &&
        !(GetWindowLong(_pVScrollBarWnd->_GetWnd(), GWL_EXSTYLE) & WS_EX_LAYERED))
    {
        RECT rcEmpty = {0};
        _pVScrollBarWnd->_SetSelectionHighlight(rcEmpty, -1, 0);
    }

    for (;
		(currentPageIndex < _model->Count()) && (indexInPage < candidateListPageCnt);
		currentPageIndex++, indexInPage++)
    {
        WCHAR wszNumString[numStringLen] = {'\0'};
        CCandidateListItem* pItemList = nullptr;

        const UILayout::CandRow row = UILayout::CandRowLayout(*prc, indexInPage, m, VScrollWidth, dpi);
        RECT rc = row.number;

        BOOL isSelected = (_model->Selection() == (INT)currentPageIndex);

        if (isSelected)
        {
            // Selected row: fill entire row with normal bg, then draw rounded highlight
            RECT rcFullRow = row.row;
            if (_brshBkColor) FillRect(dcHandle, &rcFullRow, _brshBkColor);

            int cornerR = row.cornerRadius;

            // Rounded selection highlight with symmetric left/right margin
            RECT rcHighlight = row.highlight;
            HBRUSH hSelBrush = CreateSolidBrush(_crSelectedBkColor);
            HPEN hSelPen = (HPEN)GetStockObject(NULL_PEN);
            HBRUSH hOldBr = (HBRUSH)SelectObject(dcHandle, hSelBrush);
            HPEN hOldPen = (HPEN)SelectObject(dcHandle, hSelPen);
            RoundRect(dcHandle, rcHighlight.left, rcHighlight.top,
                      rcHighlight.right, rcHighlight.bottom, cornerR, cornerR);
            SelectObject(dcHandle, hOldBr);
            SelectObject(dcHandle, hOldPen);
            DeleteObject(hSelBrush);

            // RC26: Pass full selection RoundRect to scrollbar (in scrollbar client coords)
            // so the highlight visually extends over the scrollbar with its original shape.
            // Only needed on non-layered windows (legacy apps).
            if (_pVScrollBarWnd && _pVScrollBarWnd->_GetWnd() &&
                !(GetWindowLong(_pVScrollBarWnd->_GetWnd(), GWL_EXSTYLE) & WS_EX_LAYERED))
            {
                RECT rcSB = {0};
                _pVScrollBarWnd->_GetWindowRect(&rcSB);
                POINT ptSB = { rcSB.left, rcSB.top };
                ScreenToClient(_GetWnd(), &ptSB);
                RECT rcSelSB = { rcHighlight.left - ptSB.x, rcHighlight.top - ptSB.y,
                                 rcHighlight.right - ptSB.x, rcHighlight.bottom - ptSB.y };
                _pVScrollBarWnd->_SetSelectionHighlight(rcSelSB, cornerR, _crSelectedBkColor);
            }

            // Draw number with transparent background
            SetBkMode(dcHandle, TRANSPARENT);
            SetTextColor(dcHandle, _crSelectedTextColor);
            StringCchPrintf(wszNumString, ARRAYSIZE(wszNumString), L"%c", _pIndexRange->GetAt(indexInPage)->CandIndex);
            ExtTextOut(dcHandle, row.numberX, row.textY, 0, &rc, wszNumString, numStringLen, NULL);

            // Set up for candidate text (transparent bg)
            rc = row.text;
            SetTextColor(dcHandle, _crSelectedTextColor);
        }
        else
        {
            // Non-selected row: standard opaque drawing
            SetTextColor(dcHandle, _crNumberColor);
            SetBkColor(dcHandle, _crNumberBkColor);
            SetBkMode(dcHandle, OPAQUE);
            StringCchPrintf(wszNumString, ARRAYSIZE(wszNumString), L"%c", _pIndexRange->GetAt(indexInPage)->CandIndex);
            ExtTextOut(dcHandle, row.numberX, row.textY, ETO_OPAQUE, &rc, wszNumString, numStringLen, NULL);

            rc = row.text;
            SetTextColor(dcHandle, _crTextColor);
            SetBkColor(dcHandle, _crBkColor);
        }

		pItemList = _model->ItemAt(currentPageIndex);
		if(pItemList == nullptr) continue;
		SIZE size;
		WCHAR itemText[MAX_CAND_ITEM_LENGTH + 3] = { '\0' };
		int tailOffset = -1;
		int itemLength = UILayout::CandTruncateItem((int)pItemList->_ItemString.GetLength(), &tailOffset);
		if (tailOffset >= 0) // "First 6 chars of itemtext"+"..."+"last 6 chars of itemtext"
		{
			StringCchCatN(itemText, MAX_CAND_ITEM_LENGTH + 3, pItemList->_ItemString.Get(), 6);
            StringCchCatN(itemText, MAX_CAND_ITEM_LENGTH + 3, L"…", 1);
			StringCchCatN(itemText, MAX_CAND_ITEM_LENGTH + 3, (pItemList->_ItemString.Get() + tailOffset), 6);
		}
		else
			StringCchCopyN(itemText, MAX_CAND_ITEM_LENGTH + 3, pItemList->_ItemString.Get(), itemLength);

		if (UILayout::CandItemNeedsMeasure(itemLength, _wndWidth))
		{
			GetTextExtentPoint32(dcHandle, pItemList->_ItemString.Get(), (int)pItemList->_ItemString.GetLength(), &size);
			UILayout::CandWidenForItem(itemLength, (int)size.cx, row.textX, &_cxTitle, &_wndWidth);
		}

		ExtTextOut(dcHandle, row.textX, row.textY, isSelected ? 0 : ETO_OPAQUE, &rc, itemText, (DWORD)itemLength, NULL);
		if (isSelected) SetBkMode(dcHandle, OPAQUE); // restore for next row

    }
	for (; (indexInPage < candidateListPageCnt); indexInPage++)
    {
        RECT rc = UILayout::CandRowLayout(*prc, indexInPage, m, VScrollWidth, dpi).number;
        if(_brshBkColor) FillRect(dcHandle, &rc, _brshBkColor);
    }

	if (UILayout::CandNeedsResize(_cxTitle, *prc, VScrollWidth)) _ResizeWindow();
}

//+---------------------------------------------------------------------------
//
// _DrawPageIndicator
//
//----------------------------------------------------------------------------
void CCandidateWindow::_DrawPageIndicator(_In_ HDC dcHandle, _In_ UINT currentPage, _In_ RECT *prc)
{
	if (_pIndexRange == nullptr || _model->PageCount() <= 1 || prc == nullptr) return;

	int candidateListPageCnt = _pIndexRange->Count();

	// Use absolute client coordinates — do not rely on prc (the paint dirty rect),
	// which may reflect a stale window size when _ResizeWindow() was called mid-paint.
	RECT rcClient = { 0, 0, 0, 0 };
	_GetClientRect(&rcClient);

	WCHAR wszPageInfo[16] = { L'\0' };
	StringCchPrintf(wszPageInfo, ARRAYSIZE(wszPageInfo), L"%u/%u",
		currentPage + 1, (UINT)_model->PageCount());

	// Create a 60% size font for the page indicator
	LOGFONT lf = { 0 };
	if (GetObject(Global::defaultlFontHandle, sizeof(LOGFONT), &lf))
	{
		lf.lfHeight = UILayout::CandPageIndicatorFontHeight(lf.lfHeight);
		HFONT hSmallFont = CreateFontIndirect(&lf);
		if (hSmallFont)
		{
			SelectObject(dcHandle, hSmallFont);

			// Right-aligned, compact, with padding from edges
			RECT rcIndicator = UILayout::CandPageIndicatorRect(rcClient, _cyRow, candidateListPageCnt,
				_GetScrollBarWidth(), CConfig::GetDpiForHwnd(_GetWnd()));
			SetTextColor(dcHandle, _crNumberColor);
			SetBkMode(dcHandle, TRANSPARENT);
			DrawText(dcHandle, wszPageInfo, -1, &rcIndicator, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
			SetBkMode(dcHandle, OPAQUE);

			SelectObject(dcHandle, Global::defaultlFontHandle);
			DeleteObject(hSmallFont);
		}
	}
}

//+---------------------------------------------------------------------------
//
// _DrawBorder
//
//----------------------------------------------------------------------------
void CCandidateWindow::_DrawBorder(_In_ HWND wndHandle, _In_ int cx)
{
    RECT rcWnd;

    HDC dcHandle = GetWindowDC(wndHandle);
    if (!dcHandle)
    {
        return;
    }

    GetWindowRect(wndHandle, &rcWnd);
    // zero based
    OffsetRect(&rcWnd, -rcWnd.left, -rcWnd.top); 

    // Derive border color from actual window background so it adapts to dark/light/custom themes.
    int lum = (299 * (int)GetRValue(_crBkColor) + 587 * (int)GetGValue(_crBkColor) + 114 * (int)GetBValue(_crBkColor)) / 1000;
    COLORREF borderColor;
    if (lum > 128)  // light background: darken 30%
        borderColor = RGB((BYTE)(GetRValue(_crBkColor) * 70 / 100),
                          (BYTE)(GetGValue(_crBkColor) * 70 / 100),
                          (BYTE)(GetBValue(_crBkColor) * 70 / 100));
    else            // dark background: lighten 30%
        borderColor = RGB((BYTE)min(255, (int)GetRValue(_crBkColor) + (255 - (int)GetRValue(_crBkColor)) * 30 / 100),
                          (BYTE)min(255, (int)GetGValue(_crBkColor) + (255 - (int)GetGValue(_crBkColor)) * 30 / 100),
                          (BYTE)min(255, (int)GetBValue(_crBkColor) + (255 - (int)GetBValue(_crBkColor)) * 30 / 100));
    HPEN hPen = CreatePen(PS_SOLID, cx, borderColor);
    if (hPen)
    {
        HPEN hPenOld = (HPEN)SelectObject(dcHandle, hPen);
        HBRUSH hBorderBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
        HBRUSH hBorderBrushOld = (HBRUSH)SelectObject(dcHandle, hBorderBrush);

        UINT dpi = CConfig::GetDpiForHwnd(wndHandle);
        int radius = MulDiv(_cornerRadiusBase, dpi, USER_DEFAULT_SCREEN_DPI);
        RoundRect(dcHandle, rcWnd.left, rcWnd.top, rcWnd.right, rcWnd.bottom, radius, radius);

        SelectObject(dcHandle, hPenOld);
        SelectObject(dcHandle, hBorderBrushOld);
        DeleteObject(hPen);
    }
    ReleaseDC(wndHandle, dcHandle);

}

//+---------------------------------------------------------------------------
//
// Candidate list, selection and paging: CCandidateListModel (CandidateListModel.cpp).
// These wrappers keep the vertical scroll bar in sync with the model.
//
//----------------------------------------------------------------------------

void CCandidateWindow::_AddString(_Inout_ CCandidateListItem *pCandidateItem, _In_ BOOL isAddFindKeyCode)
{
	_model->AddItem(pCandidateItem, isAddFindKeyCode);
}

void CCandidateWindow::_ClearList()
{
	_model->Clear();
}

DWORD CCandidateWindow::_GetCandidateString(_In_ int iIndex, _Inout_opt_ const WCHAR **ppwchCandidateString)
{
	return _model->GetString(iIndex, ppwchCandidateString);
}

DWORD CCandidateWindow::_GetSelectedCandidateString(_Inout_opt_ const WCHAR **ppwchCandidateString)
{
	return _model->GetSelectedString(ppwchCandidateString);
}

DWORD CCandidateWindow::_GetSelectedCandidateKeyCode(_Inout_opt_ const WCHAR **ppwchCandidateString)
{
	return _model->GetSelectedKeyCode(ppwchCandidateString);
}

BOOL CCandidateWindow::_SetSelectionInPage(int nPos)
{
	return _model->SetSelectionInPage(nPos);
}

BOOL CCandidateWindow::_MoveSelection(_In_ int offSet, _In_ BOOL isNotify)
{
	if (!_model->MoveSelection(offSet))
		return FALSE;
	if (_pVScrollBarWnd && isNotify)
		_pVScrollBarWnd->_ShiftLine(offSet, isNotify);
	return TRUE;
}

BOOL CCandidateWindow::_SetSelection(_In_ int selectedIndex, _In_ BOOL isNotify)
{
	BOOL applied = FALSE;
	BOOL ret = _model->SetSelection(selectedIndex, &applied);
	if (applied && _pVScrollBarWnd && isNotify)
		_pVScrollBarWnd->_ShiftPosition(selectedIndex, isNotify);
	return ret;
}

void CCandidateWindow::_SetSelection(_In_ int nIndex)
{
	_model->SetSelectionRaw(nIndex);
}

BOOL CCandidateWindow::_MovePage(_In_ int offSet, _In_ BOOL isNotify)
{
	if (offSet == 0)
		return TRUE;
	BOOL wrapped = FALSE;
	if (!_model->MovePage(offSet, &wrapped))
		return FALSE;
	// adjust scrollbar position
	if (_pVScrollBarWnd && isNotify)
	{
		if (wrapped)
			_pVScrollBarWnd->_ShiftPosition(_model->Selection(), FALSE);
		else
			_pVScrollBarWnd->_ShiftPage(offSet, isNotify);
	}
	return TRUE;
}

BOOL CCandidateWindow::_SetSelectionOffset(_In_ int offSet)
{
	return _model->SetSelectionOffset(offSet);
}

HRESULT CCandidateWindow::_GetPageIndex(UINT *pIndex, _In_ UINT uSize, _Inout_ UINT *puPageCnt)
{
	return _model->GetPageIndex(pIndex, uSize, puPageCnt);
}

HRESULT CCandidateWindow::_SetPageIndex(UINT *pIndex, _In_ UINT uPageCnt)
{
	return _model->SetPageIndex(pIndex, uPageCnt);
}

HRESULT CCandidateWindow::_GetCurrentPage(_Inout_ UINT *pCurrentPage)
{
	return _model->GetCurrentPage(pCurrentPage);
}

HRESULT CCandidateWindow::_GetCurrentPage(_Inout_ int *pCurrentPage)
{
	return _model->GetCurrentPage(pCurrentPage);
}

BOOL CCandidateWindow::_AdjustPageIndexForSelection()
{
	return _model->AdjustPageIndexForSelection();
}

HRESULT CCandidateWindow::_CurrentPageHasEmptyItems(_Inout_ BOOL *pfHasEmptyItems)
{
	return _model->CurrentPageHasEmptyItems(pfHasEmptyItems);
}

HRESULT CCandidateWindow::_AdjustPageIndex(_Inout_ UINT & currentPage, _Inout_ UINT & currentPageIndex)
{
	return _model->AdjustPageIndex(currentPage, currentPageIndex);
}

//+---------------------------------------------------------------------------
//
// _SetScrollInfo
//
//----------------------------------------------------------------------------

void CCandidateWindow::_SetScrollInfo(_In_ int nMax, _In_ int nPage)
{
    CScrollInfo si;
    si.nMax = nMax;
    si.nPage = nPage;
    si.nPos = 0;

    if (_pVScrollBarWnd)
    {
        _pVScrollBarWnd->_SetScrollInfo(&si);
    }
}

//+---------------------------------------------------------------------------
//
// _AdjustTextColor
//
//----------------------------------------------------------------------------

COLORREF _AdjustTextColor(_In_ COLORREF crColor, _In_ COLORREF crBkColor)
{
    if (!Global::IsTooSimilar(crColor, crBkColor))
    {
        return crColor;
    }
    else
    {
        return crColor ^ RGB(255, 255, 255);
    }
}


//+---------------------------------------------------------------------------
//
// _FireMessageToLightDismiss
//      fire EVENT_OBJECT_IME_xxx to let LightDismiss know about IME window.
//----------------------------------------------------------------------------

void CCandidateWindow::_FireMessageToLightDismiss(_In_ HWND wndHandle, _In_opt_ WINDOWPOS *pWndPos)
{
    if (nullptr == pWndPos)
    {
        return;
    }

    BOOL isShowWnd = ((pWndPos->flags & SWP_SHOWWINDOW) != 0);
    BOOL isHide = ((pWndPos->flags & SWP_HIDEWINDOW) != 0);
    BOOL needResize = ((pWndPos->flags & SWP_NOSIZE) == 0);
    BOOL needMove = ((pWndPos->flags & SWP_NOMOVE) == 0);
    BOOL needRedraw = ((pWndPos->flags & SWP_NOREDRAW) == 0);

    if (isShowWnd)
    {
        NotifyWinEvent(EVENT_OBJECT_IME_SHOW, wndHandle, OBJID_CLIENT, CHILDID_SELF);
    }
    else if (isHide)
    {
        NotifyWinEvent(EVENT_OBJECT_IME_HIDE, wndHandle, OBJID_CLIENT, CHILDID_SELF);
    }
    else if (needResize || needMove || needRedraw)
    {
        if (IsWindowVisible(wndHandle))
        {
            NotifyWinEvent(EVENT_OBJECT_IME_CHANGE, wndHandle, OBJID_CLIENT, CHILDID_SELF);
        }
    }

}
void CCandidateWindow::_DeleteShadowWnd()
{
	debugPrint(L"CCandidateWindow::_DeleteShadowWnd(), gdiObjects = %d", GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS));
    _pShadowWnd.reset();
}

void CCandidateWindow::_DeleteVScrollBarWnd()
{
	debugPrint(L"CCandidateWindow::_DeleteVScrollBarWnd(), gdiObjects = %d", GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS));
    _pVScrollBarWnd.reset();
}
