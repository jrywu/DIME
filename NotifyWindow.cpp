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
#include "NotifyWindow.h"
#include <memory> // Include for smart pointers
#define NO_ANIMATION
#define NO_WINDOW_SHADOW
#define ANIMATION_STEP_TIME 15
#define ANIMATION_TIMER_ID 39773
#define DELAY_SHOW_TIMER_ID 39775
#define TIME_TO_HIDE_TIMER_ID 39776
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

	_x =0;
	_y =0;


	_fontSize = 12;
	
	_delayShow = 0;
	_timeToHide = 0;

	_notifyText.Set(nullptr,0);
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
#ifndef NO_ANIMATION
		WS_EX_LAYERED |
#endif
		WS_EX_TOOLWINDOW,
        WS_BORDER | WS_POPUP,
        NULL, 0, 0, parentWndHandle))
    {
        return FALSE;
    }
#ifndef NO_ANIMATION	
	SetLayeredWindowAttributes(_GetWnd(), 0,  (255 * 5) / 100, LWA_ALPHA);
#endif
    return TRUE;
}

BOOL CNotifyWindow::_CreateBackGroundShadowWindow()
{
    _pShadowWnd = std::make_unique<CShadowWindow>(this); // Use std::make_unique
    if (!_pShadowWnd->_Create(Global::AtomNotifyShadowWindow,
        WS_EX_TOPMOST | 
        WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        WS_BORDER | WS_POPUP, this))
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
#ifndef NO_ANIMATION	
	SetLayeredWindowAttributes(_GetWnd(), 0,  255 * (5 / 100), LWA_ALPHA); // 5% transparent faded out to 95 %
	_animationStage = 10;	
	_EndTimer(ANIMATION_TIMER_ID);
	_StartTimer(ANIMATION_STEP_TIME, ANIMATION_TIMER_ID);
#endif
}
void CNotifyWindow::_OnTimerID(UINT_PTR timerID)
{   //animate the window faded out with layered transparency
	debugPrint(L"CNotifyWindow::_OnTimer(): timerID = %d,  _animationStage = %d", timerID, _animationStage);
	switch (timerID)
	{
#ifndef NO_ANIMATION
	case ANIMATION_TIMER_ID:
		if(_animationStage)
		{
			BYTE transparentLevel = (255 * (5 + 9 * (11 - (BYTE)_animationStage))) / 100; 
			debugPrint(L"CNotifyWindow::_OnTimer() transparentLevel = %d", transparentLevel);

			SetLayeredWindowAttributes(_GetWnd(), 0, transparentLevel , LWA_ALPHA); 
			_StartTimer(ANIMATION_STEP_TIME, ANIMATION_TIMER_ID);
			_animationStage --;

		}
		else
		{
			_EndTimer(ANIMATION_TIMER_ID);
			SetLayeredWindowAttributes(_GetWnd(), 0,  (255 * 95) / 100, LWA_ALPHA); 
		}
		break;
#endif
	case DELAY_SHOW_TIMER_ID:
 		_EndTimer(DELAY_SHOW_TIMER_ID);
		_pfnCallback(_pObj, NOTIFY_WND::SHOW_NOTIFY, _timeToHide , (LPARAM) _notifyType);
		break;
	case TIME_TO_HIDE_TIMER_ID:
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
	debugPrint(L"CNotifyWindow::_Show(), isShowWnd = %d, delayShow = %d, timeToHide = %d ", isShowWnd, delayShow, timeToHide);
	_delayShow = delayShow;
	_timeToHide = timeToHide;

	if(_IsTimer()) 
	{
		debugPrint(L"CNotifyWindow::_Show(), end old timers first");
		_EndTimer(DELAY_SHOW_TIMER_ID);
		_EndTimer(TIME_TO_HIDE_TIMER_ID);
	}

	if(!isShowWnd || delayShow == 0) //ignore delayShow if isShowWnd is FALSE
	{
		
		if (_pShadowWnd)
			_pShadowWnd->_Show(isShowWnd);
 
		CBaseWindow::_Show(isShowWnd);
	}
	if(isShowWnd)
	{

		if(delayShow > 0 )
		{
			debugPrint(L"CNotifyWindow::_Show(), set delay show timer, id = %d", DELAY_SHOW_TIMER_ID);
			_StartTimer(delayShow, DELAY_SHOW_TIMER_ID);//Show the notify after delay Show
		}
		else if(timeToHide > 0)
		{
			debugPrint(L"CNotifyWindow::_Show(), set time to hide timer");
			_StartTimer(timeToHide, TIME_TO_HIDE_TIMER_ID);//Show the notify after delay Show
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
            _DrawBorder(wndHandle, NOTIFYWND_BORDER_WIDTH);
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
    HPEN hPen = CreatePen(PS_SOLID, cx, NOTIFYWND_BORDER_COLOR);
    HBRUSH hBorderBrush = (HBRUSH)GetStockObject(NULL_BRUSH);

    HPEN hPenOld = (HPEN)SelectObject(dcHandle, hPen);
    HBRUSH hBorderBrushOld = (HBRUSH)SelectObject(dcHandle, hBorderBrush);

    Rectangle(dcHandle, rcWnd.left, rcWnd.top, rcWnd.right, rcWnd.bottom);

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