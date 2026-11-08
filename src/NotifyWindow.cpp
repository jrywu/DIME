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
#include "UIConstants.h"
#include "BaseWindow.h"
#include "NotifyWindow.h"
#include <memory> // Include for smart pointers
#ifdef DIME_RUST_AVAILABLE
#include "dime_ffi.h"
#endif
//#define NO_WINDOW_SHADOW
#ifdef DIME_RUST_AVAILABLE
//+---------------------------------------------------------------------------
//
// The notify window's drawing and behavior in Rust (dime-ui::notify); the
// state stays in this object and is reached through this host.
// Parity: NotifyParityTest.
//
//----------------------------------------------------------------------------

struct CNotifyWindowRustHost
{
    static CNotifyWindow* N(void* ctx) { return static_cast<CNotifyWindow*>(ctx); }

    static void* Hwnd(void* ctx) { return N(ctx)->_GetWnd(); }
    static const uint16_t* Text(void* ctx, uint32_t* len)
    {
        *len = (uint32_t)N(ctx)->_notifyText.GetLength();
        return (const uint16_t*)N(ctx)->_notifyText.Get();
    }
    static void* Font(void*) { return Global::defaultlFontHandle; }
    static int32_t NotifyType(void* ctx) { return (int32_t)N(ctx)->_notifyType; }
    static void TextMetric(void* ctx, int32_t* maxW, int32_t* aveW, int32_t* height)
    {
        *maxW = N(ctx)->_TextMetric.tmMaxCharWidth;
        *aveW = N(ctx)->_TextMetric.tmAveCharWidth;
        *height = N(ctx)->_TextMetric.tmHeight;
    }
    static void SetTitleSize(void* ctx, int32_t cx, int32_t cy) { N(ctx)->_cxTitle = cx; N(ctx)->_cyTitle = cy; }
    static void SetPosition(void* ctx, int32_t x, int32_t y) { N(ctx)->_x = x; N(ctx)->_y = y; }
    static void SetShowTimes(void* ctx, uint32_t delayShow, uint32_t timeToHide) { N(ctx)->_delayShow = delayShow; N(ctx)->_timeToHide = timeToHide; }
    static uint32_t TimeToHide(void* ctx) { return N(ctx)->_timeToHide; }
    static int32_t CornerRadiusBase(void* ctx) { return N(ctx)->_cornerRadiusBase; }
    static uint32_t Dpi(void* ctx) { return CConfig::GetDpiForHwnd(N(ctx)->_GetWnd()); }
    static void Invoke(void* ctx, int32_t action, uintptr_t wParam, intptr_t lParam)
    {
        // Copy before calling: the callback may destroy the window.
        NOTIFYWNDCALLBACK pfnCallback = N(ctx)->_pfnCallback;
        void* pObj = N(ctx)->_pObj;
        if (pfnCallback)
            pfnCallback(pObj, action == 0 ? NOTIFY_WND::SWITCH_CHN_ENG : NOTIFY_WND::SHOW_NOTIFY, (WPARAM)wParam, (LPARAM)lParam);
    }
    static void Paint(void* ctx, void* dc, void* ps) { N(ctx)->_OnPaint((HDC)dc, (PAINTSTRUCT*)ps); }
    static void DpiChanged(void*, void* hwnd) { CConfig::SetDefaultTextFont((HWND)hwnd); }
    static void BaseShow(void* ctx, int32_t on) { N(ctx)->CBaseWindow::_Show(on ? TRUE : FALSE); }
    static int32_t IsCapture(void* ctx) { return N(ctx)->_IsCapture() ? 1 : 0; }
    static void EndCapture(void* ctx) { N(ctx)->_EndCapture(); }
    static int32_t IsTimer(void* ctx) { return N(ctx)->_IsTimer() ? 1 : 0; }
    static void StartTimer(void* ctx, uint32_t elapse, uintptr_t id) { N(ctx)->_StartTimer(elapse, id); }
    static void EndTimer(void* ctx, uintptr_t id) { N(ctx)->_EndTimer(id); }
    static int32_t HasShadow(void* ctx) { return N(ctx)->_pShadowWnd != nullptr; }
    static void* ShadowHwnd(void* ctx) { return N(ctx)->_pShadowWnd ? N(ctx)->_pShadowWnd->_GetWnd() : nullptr; }
    static void ShadowShow(void* ctx, int32_t show) { if (N(ctx)->_pShadowWnd) N(ctx)->_pShadowWnd->_Show((BOOL)show); }
    static void ShadowOwnerMoved(void* ctx, int32_t resized) { if (N(ctx)->_pShadowWnd) N(ctx)->_pShadowWnd->_OnOwnerWndMoved((BOOL)resized); }
    static void DeleteShadow(void* ctx) { N(ctx)->_DeleteShadowWnd(); }

    static const DimeNotifyHostVtbl s_vtbl;
};

const DimeNotifyHostVtbl CNotifyWindowRustHost::s_vtbl = {
    Hwnd, Text, Font, NotifyType, TextMetric, SetTitleSize, SetPosition, SetShowTimes, TimeToHide, CornerRadiusBase,
    Dpi, Invoke, Paint, DpiChanged, BaseShow, IsCapture, EndCapture, IsTimer, StartTimer, EndTimer, HasShadow,
    ShadowHwnd, ShadowShow, ShadowOwnerMoved, DeleteShadow,
};

// _OnPaintRust: dime-ui::notify draws; C++ applies the new size.
void CNotifyWindow::_OnPaintRust(_In_ HDC dcHandle, _In_ PAINTSTRUCT *pPaintStruct)
{
    DimeNotifyPaint in = {};
    in.font = Global::defaultlFontHandle;
    in.bk_brush = _brshBkColor;
    in.has_paint = pPaintStruct ? 1 : 0;
    if (pPaintStruct)
        in.paint = { pPaintStruct->rcPaint.left, pPaintStruct->rcPaint.top, pPaintStruct->rcPaint.right, pPaintStruct->rcPaint.bottom };
    in.text = (const uint16_t*)_notifyText.Get();
    in.text_len = (uint32_t)_notifyText.GetLength();
    in.text_color = _crTextColor;
    in.bk_color = _crBkColor;
    in.cx_title = _cxTitle;
    int32_t cx = 0, cy = 0, resize = 0;
    if (dime_ui_notify_paint(dcHandle, &in, &cx, &cy, &resize))
    {
        _cxTitle = cx;
        _cyTitle = cy;
        if (resize) _ResizeWindow();
    }
}
#endif

//+---------------------------------------------------------------------------
//
// ctor
//
//----------------------------------------------------------------------------

CNotifyWindow::CNotifyWindow(_In_ NOTIFYWNDCALLBACK pfnCallback, _In_ void *pv, _In_ enum NOTIFY_TYPE notifyType)
{
   debugPrint(L"CNotifyWindow::CNotifyWindow() gdiObjects = %d", GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS));
	
   _brshBkColor = nullptr;

    _pfnCallback = pfnCallback;
    _pObj = pv;

	_notifyType = notifyType;

    _animationStage = 0;
    _crBkColor = 0;
    _crTextColor = 0;
    _cxTitle = 0;
    _cyTitle = 0;
    _TextMetric = {0,0,0,0,0, 0,0,0,0,0,  0,0,0,0,0, 0,0,0,0,0};

    _pShadowWnd = nullptr;

	_x = UI::DEFAULT_WINDOW_X;
	_y = UI::DEFAULT_WINDOW_Y;


	_fontSize = UI::DEFAULT_FONT_SIZE;
	
	_delayShow = 0;
	_timeToHide = 0;

	_notifyText.Set(nullptr,0);
#ifdef DIME_USE_RUST_ENGINE
	_useRust = TRUE;
#else
	_useRust = FALSE;
#endif
}

//+---------------------------------------------------------------------------
//
// dtor
//
//----------------------------------------------------------------------------

CNotifyWindow::~CNotifyWindow()
{
	if(_brshBkColor) DeleteObject(_brshBkColor);
	if(_IsTimer()) _EndTimer();
    _DeleteShadowWnd();
    debugPrint(L"CNotifyWindow::~CNotifyWindow() gdiObjects = %d", GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS));
}
//+---------------------------------------------------------------------------
//
// _Create
//
// CandidateWindow is the top window
//----------------------------------------------------------------------------

BOOL CNotifyWindow::_Create(_In_ UINT fontSize, _In_opt_ HWND parentWndHandle, _In_opt_ CStringRange* notifyText)
{
	debugPrint(L"CNotifyWindow::_Create()");
    BOOL ret = FALSE;
	_fontSize = fontSize;
	if(notifyText)
		_SetString(notifyText);

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
    _ResizeWindow();

Exit:
    return TRUE;
}

BOOL CNotifyWindow::_CreateMainWindow(_In_opt_ HWND parentWndHandle)
{
    _SetUIWnd(this);

	if (!CBaseWindow::_Create(Global::AtomNotifyWindow,
		WS_EX_TOPMOST |
		WS_EX_NOACTIVATE,
        WS_POPUP,
        NULL, 0, 0, parentWndHandle))
    {
        return FALSE;
    }
    return TRUE;
}

BOOL CNotifyWindow::_CreateBackGroundShadowWindow()
{
    _pShadowWnd = std::make_unique<DimeShadowWindow>(this); // Use std::make_unique
    if (!_pShadowWnd->_Create(Global::AtomNotifyShadowWindow,
        WS_EX_TOPMOST | 
        WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        WS_POPUP, this))
    {
        _pShadowWnd.reset(); // Automatically deletes the object
        return FALSE;
    }

    return TRUE;
}


void CNotifyWindow::_ResizeWindow()
{
	debugPrint(L"CNotifyWindow::_ResizeWindow() , _x = %d, _y= %d, _cx= %d, _cy= %d", _x, _y, _cxTitle, _cyTitle);
    CBaseWindow::_Resize(_x, _y, _cxTitle, _cyTitle); 
	_InvalidateRect();
}

//+---------------------------------------------------------------------------
//
// _Move
//
//----------------------------------------------------------------------------

void CNotifyWindow::_Move(int x, int y)
{
	debugPrint(L"CNotifyWindow::_Move(), x = %d, y= %d", x, y);
	
	if (_x == x && _y == y) return;

	_x = x;
	_y = y;
    CBaseWindow::_Move(_x, _y);
}
void CNotifyWindow::_OnTimerID(UINT_PTR timerID)
{   //animate the window faded out with layered transparency
#ifdef DIME_RUST_AVAILABLE
    if (_useRust)
    {
        DimeNotifyHost host = { &CNotifyWindowRustHost::s_vtbl, this };
        dime_ui_notify_timer(&host, (uintptr_t)timerID);
        return;
    }
#endif
	debugPrint(L"CNotifyWindow::_OnTimer(): timerID = %d,  _animationStage = %d", timerID, _animationStage);
	switch (timerID)
	{
	case UI::DELAY_SHOW_TIMER_ID:
 		_EndTimer(UI::DELAY_SHOW_TIMER_ID);
		_pfnCallback(_pObj, NOTIFY_WND::SHOW_NOTIFY, _timeToHide , (LPARAM) _notifyType);
		break;
	case UI::TIME_TO_HIDE_TIMER_ID:
		_Show(FALSE, 0, 0);
		break;
	}
}

//+---------------------------------------------------------------------------
//
// _Show
//
//----------------------------------------------------------------------------

void CNotifyWindow::_Show(BOOL isShowWnd, UINT delayShow, UINT timeToHide)
{
#ifdef DIME_RUST_AVAILABLE
    if (_useRust)
    {
        DimeNotifyHost host = { &CNotifyWindowRustHost::s_vtbl, this };
        dime_ui_notify_show(&host, isShowWnd ? 1 : 0, delayShow, timeToHide);
        return;
    }
#endif
	debugPrint(L"CNotifyWindow::_Show(), isShowWnd = %d, delayShow = %d, timeToHide = %d ", isShowWnd, delayShow, timeToHide);
	_delayShow = delayShow;
	_timeToHide = timeToHide;

	if(_IsTimer()) 
	{
		debugPrint(L"CNotifyWindow::_Show(), end old timers first");
		_EndTimer(UI::DELAY_SHOW_TIMER_ID);
		_EndTimer(UI::TIME_TO_HIDE_TIMER_ID);
	}

	if(!isShowWnd || delayShow == 0) //ignore delayShow if isShowWnd is FALSE
	{
		
		if (_pShadowWnd)
			_pShadowWnd->_Show(isShowWnd);

		CBaseWindow::_Show(isShowWnd);

		// Re-apply rounded region AFTER window is visible.
		// Win11 DWM silently discards SetWindowRgn on hidden windows.
		if (isShowWnd)
		{
			RECT rc = { 0, 0, 0, 0 };
			GetClientRect(_GetWnd(), &rc);
			int cx = rc.right - rc.left;
			int cy = rc.bottom - rc.top;
			if (cx > 0 && cy > 0)
			{
				UINT dpi = CConfig::GetDpiForHwnd(_GetWnd());
				int radius = MulDiv(_cornerRadiusBase, dpi, USER_DEFAULT_SCREEN_DPI);
				HRGN hRgn = CreateRoundRectRgn(0, 0, cx + 1, cy + 1, radius, radius);
				SetWindowRgn(_GetWnd(), hRgn, TRUE);
			}
		}
	}
	if(isShowWnd)
	{

		if(delayShow > 0 )
		{
			debugPrint(L"CNotifyWindow::_Show(), set delay show timer, id = %d", UI::DELAY_SHOW_TIMER_ID);
			_StartTimer(delayShow, UI::DELAY_SHOW_TIMER_ID);//Show the notify after delay Show
		}
		else if(timeToHide > 0)
		{
			debugPrint(L"CNotifyWindow::_Show(), set time to hide timer");
			_StartTimer(timeToHide, UI::TIME_TO_HIDE_TIMER_ID);//Show the notify after delay Show
		}
	}
	if( delayShow == 0 )
	{
		debugPrint(L"CNotifyWindow::_Show() showing and start capture");
		if(isShowWnd && _notifyType == NOTIFY_TYPE::NOTIFY_CHN_ENG)
		{
			debugPrint(L"CNotifyWindow::_Show() about to show and start capture");
		}
		else
		{
			debugPrint(L"CNotifyWindow::_Show() about to hid  and stop capture");
			if(_IsCapture()) _EndCapture();
		}
	}
}

//+---------------------------------------------------------------------------
//
// _SetTextColor
// _SetFillColor
//
//----------------------------------------------------------------------------

VOID CNotifyWindow::_SetTextColor(_In_ COLORREF crColor, _In_ COLORREF crBkColor)
{
    _crTextColor = _AdjustTextColor(crColor, crBkColor);
    _crBkColor = crBkColor;
}

VOID CNotifyWindow::_SetFillColor(_In_ COLORREF crFiColor)
{
	if(_brshBkColor) DeleteObject(_brshBkColor);
	//_crFiColor = crFiColor;
    _brshBkColor = CreateSolidBrush(crFiColor);
}

//+---------------------------------------------------------------------------
//
// _WindowProcCallback
//
// Cand window proc.
//----------------------------------------------------------------------------


LRESULT CALLBACK CNotifyWindow::_WindowProcCallback(_In_ HWND wndHandle, UINT uMsg, _In_ WPARAM wParam, _In_ LPARAM lParam)
{
#ifdef DIME_RUST_AVAILABLE
    if (_useRust)
    {
        DimeNotifyHost host = { &CNotifyWindowRustHost::s_vtbl, this };
        return (LRESULT)dime_ui_notify_window_proc(&host, wndHandle, uMsg, (uintptr_t)wParam, (intptr_t)lParam);
    }
#endif
	
    switch (uMsg)
    {
    case WM_CREATE:
        {
            HDC dcHandle = nullptr;
            dcHandle = GetDC(wndHandle);
            if (dcHandle)
            {
				HFONT hFontOld = (HFONT)SelectObject(dcHandle, Global::defaultlFontHandle);

				if(_notifyText.GetLength())
				{
					SIZE size;
					GetTextExtentPoint32(dcHandle,_notifyText.Get(), (UINT)_notifyText.GetLength(), &size); //don't trust the TextMetrics. Measurement the font height and width directly.

					_cxTitle = size.cx + size.cx / (int)_notifyText.GetLength();
					_cyTitle = size.cy *3/2;
				}
				else
				{
					_cxTitle = _TextMetric.tmMaxCharWidth *4 + _TextMetric.tmAveCharWidth * 5/2;
					_cyTitle = _TextMetric.tmHeight *2;
				}
				_x = -32768;
				_y = -32768;  //out of screen initially
				
				
				debugPrint(L"CNotifyWindow::_WindowProcCallback():WM_CREATE, _cxTitle = %d, _cyTitle=%d, _TextMetric.tmAveCharWidt = %d", 
					_cxTitle, _cyTitle, _TextMetric.tmAveCharWidth);
                SelectObject(dcHandle, hFontOld);
                ReleaseDC(wndHandle, dcHandle);
            }
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

           
        }
        break;

    case WM_SHOWWINDOW:
        // show/hide shadow
        if (_pShadowWnd)
        {
            _pShadowWnd->_Show((BOOL)wParam);
        }

        break;

    case WM_PAINT:
        {
			debugPrint(L"CNotifyWindow::_WindowProcCallback():WM_PAINT gdiObjects = %d", GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS));
            HDC dcHandle = nullptr;
            PAINTSTRUCT ps;

            dcHandle = BeginPaint(wndHandle, &ps);
	
            _OnPaint(dcHandle, &ps);
            // No custom border drawn on any version — CShadowWindow provides visual
            // separation on all supported Windows versions (Win7+).
            EndPaint(wndHandle, &ps);

            ReleaseDC(wndHandle, dcHandle);
			debugPrint(L"CNotifyWindow::_WindowProcCallback():WM_PAINT ended. gdiObjects = %d", GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS));
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

    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_MBUTTONUP:
    case WM_RBUTTONUP:
        {
        POINT point = { 0,0 };

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

    case WM_DPICHANGED:
    {
        CConfig::SetDefaultTextFont(wndHandle);
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

void CNotifyWindow::_HandleMouseMsg(_In_ UINT mouseMsg, _In_ POINT point)
{
    switch (mouseMsg)
    {
    case WM_MOUSEMOVE:
        _OnMouseMove(point);
        break;
    case WM_LBUTTONDOWN:
        _OnLButtonDown(point);
        break;
	case WM_RBUTTONDOWN:
        _OnRButtonDown(point);
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

void CNotifyWindow::_OnPaint(_In_ HDC dcHandle, _In_ PAINTSTRUCT *pPaintStruct)
{
#ifdef DIME_RUST_AVAILABLE
    if (_useRust)
    {
        _OnPaintRust(dcHandle, pPaintStruct);
        return;
    }
#endif
    _OnPaintCpp(dcHandle, pPaintStruct);
}

// _OnPaintCpp: the C++ original.
void CNotifyWindow::_OnPaintCpp(_In_ HDC dcHandle, _In_ PAINTSTRUCT *pPaintStruct)
{
    SetBkMode(dcHandle, TRANSPARENT);

    HFONT hFontOld = (HFONT)SelectObject(dcHandle, Global::defaultlFontHandle);

	if(pPaintStruct)
	{
		FillRect(dcHandle, &pPaintStruct->rcPaint, _brshBkColor);

		_DrawText(dcHandle, &pPaintStruct->rcPaint);
	}

//cleanup:
    SelectObject(dcHandle, hFontOld);
}




//+---------------------------------------------------------------------------
//
// _DrawText
//
//----------------------------------------------------------------------------

void CNotifyWindow::_DrawText(_In_ HDC dcHandle, _In_ RECT *prc)
{
 	HFONT hFontOld = (HFONT)SelectObject(dcHandle, Global::defaultlFontHandle);
	int _oldCxTitle = _cxTitle;
	SIZE size;
	GetTextExtentPoint32(dcHandle, _notifyText.Get(), (UINT)_notifyText.GetLength(), &size);
	_cxTitle = size.cx + size.cx / (int)_notifyText.GetLength();
	_cyTitle = size.cy * 3/2;
	debugPrint(L"CNotifyWindow::_DrawText(), _cxTitle = %d, _cyTitle=%d, text size x = %d, y = %d", _cxTitle, _cyTitle, size.cx, size.cy);
	
    RECT rc = { 0,0,0,0 };
	if(prc)
	{
		rc.top = prc->top;
		rc.bottom = rc.top + _cyTitle;

		rc.left = prc->left;
		rc.right = prc->left + _cxTitle;
	}

    SetTextColor(dcHandle, _crTextColor);// NOTIFYWND_TEXT_COLOR);
    SetBkColor(dcHandle, _crBkColor);//NOTIFYWND_TEXT_BK_COLOR);
	ExtTextOut(dcHandle, size.cx / (int)_notifyText.GetLength() / 2, _cyTitle / 5, ETO_OPAQUE, &rc, _notifyText.Get(), (DWORD)_notifyText.GetLength(), NULL);

	SelectObject(dcHandle, hFontOld);
	if(_oldCxTitle != _cxTitle)
	{
		_ResizeWindow();
	}
    
    
}

//+---------------------------------------------------------------------------
//
// _DrawBorder
//
//----------------------------------------------------------------------------
void CNotifyWindow::_DrawBorder(_In_ HWND wndHandle, _In_ int cx)
{
    RECT rcWnd;
    HDC dcHandle = GetWindowDC(wndHandle);

    GetWindowRect(wndHandle, &rcWnd);
    OffsetRect(&rcWnd, -rcWnd.left, -rcWnd.top); // Zero-based

    // Use RAII for GDI objects
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
    HBRUSH hBorderBrush = (HBRUSH)GetStockObject(NULL_BRUSH);

    HPEN hPenOld = (HPEN)SelectObject(dcHandle, hPen);
    HBRUSH hBorderBrushOld = (HBRUSH)SelectObject(dcHandle, hBorderBrush);

    // Use RoundRect to match the SetWindowRgn rounded region from _Resize,
    // so the border follows the corners instead of being clipped at them.
    UINT dpi = CConfig::GetDpiForHwnd(wndHandle);
    int radius = MulDiv(_cornerRadiusBase, dpi, USER_DEFAULT_SCREEN_DPI);
    RoundRect(dcHandle, rcWnd.left, rcWnd.top, rcWnd.right, rcWnd.bottom, radius, radius);

    SelectObject(dcHandle, hPenOld);
    SelectObject(dcHandle, hBorderBrushOld);

    DeleteObject(hPen);
    ReleaseDC(wndHandle, dcHandle);
}

//+---------------------------------------------------------------------------
//
// _AddString
//
//----------------------------------------------------------------------------

void CNotifyWindow::_AddString(_Inout_ const CStringRange *pNotifyText)
{
	debugPrint(L"CNotifyWindow::_AddString()");
	if(pNotifyText == nullptr ) return;
	size_t notifyTextLen = pNotifyText->GetLength();
    WCHAR* pwchString = nullptr;
    if (notifyTextLen)
    {
		pwchString = new (std::nothrow) WCHAR[ _notifyText.GetLength() + notifyTextLen + 2];
        if (!pwchString)
        {
            return;
        }
		StringCchCopyN(pwchString, _notifyText.GetLength() + notifyTextLen + 2, _notifyText.Get(),_notifyText.GetLength()); 
		StringCchCatN(pwchString,_notifyText.GetLength() + notifyTextLen + 2, pNotifyText->Get(), notifyTextLen); 
		_notifyText.Set(pwchString, _notifyText.GetLength() + notifyTextLen);
		
    }

   
}

//+---------------------------------------------------------------------------
//
// _SetString
//
//----------------------------------------------------------------------------

void CNotifyWindow::_SetString(_Inout_ const CStringRange *pNotifyText)
{
    debugPrint(L"CNotifyWindow::_SetString()");
    if (pNotifyText == nullptr) return;

    size_t notifyTextLen = pNotifyText->GetLength();
    if (notifyTextLen)
    {
        // Use std::unique_ptr for safe memory management
        std::unique_ptr<WCHAR[]> pwchString = std::make_unique<WCHAR[]>(notifyTextLen + 1);
        StringCchCopyN(pwchString.get(), notifyTextLen + 1, pNotifyText->Get(), notifyTextLen);

        _notifyText.Set(pwchString.release(), notifyTextLen); // Transfer ownership
        debugPrint(L"CNotifyWindow::_SetString(), notifyTextLen =%d", notifyTextLen);
    }
}

//+---------------------------------------------------------------------------
//
// _Clear
//
//----------------------------------------------------------------------------

void CNotifyWindow::_Clear()
{
	_notifyText.Set(nullptr,0);
}

UINT CNotifyWindow::_GetWidth()
{
	debugPrint(L"CNotifyWindow::_GetWidth() _cxTitle = %d", _cxTitle);
	return _cxTitle;
}
UINT CNotifyWindow::_GetHeight()
{
	return _cyTitle;
}


void CNotifyWindow::_FireMessageToLightDismiss(_In_ HWND wndHandle, _In_ WINDOWPOS *pWndPos)
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

void CNotifyWindow::_DeleteShadowWnd()
{
    debugPrint(L"CNotifyWindow::_DeleteShadowWnd(), gdiObjects = %d", GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS));
    _pShadowWnd.reset(); // Automatically deletes the object
}


void CNotifyWindow::_OnLButtonDown(POINT pt)
{
	RECT rcWindow = {0, 0, 0, 0};

	_GetClientRect(&rcWindow);

	if(PtInRect(&rcWindow, pt))
	{
		if(_notifyType == NOTIFY_TYPE::NOTIFY_CHN_ENG)
		{
			_pfnCallback(_pObj, NOTIFY_WND::SWITCH_CHN_ENG, NULL, NULL);
		}
	}
	else
	{   // hide the notify if click outside the notify window.
		_Show(FALSE,0,0);
		
	}

}

void CNotifyWindow::_OnRButtonDown(POINT pt)
{
	pt;
	//hide the notify if click outside the notify window.
	_Show(FALSE,0,0);


}


void CNotifyWindow::_OnMouseMove(POINT pt)
{

	 RECT rcWindow = {0, 0, 0, 0};

    _GetClientRect(&rcWindow);

	if(PtInRect(&rcWindow, pt))
	{
		if(_notifyType == NOTIFY_TYPE::NOTIFY_CHN_ENG)
			SetCursor(LoadCursor(NULL, IDC_HAND));
		else
			SetCursor(LoadCursor(NULL, IDC_ARROW));
	}
	else
			SetCursor(LoadCursor(NULL, IDC_ARROW));
}