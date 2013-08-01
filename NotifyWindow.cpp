//
//
//  Jeremy '13,7,17
//
//


#include "Private.h"
#include "Globals.h"
#include "BaseWindow.h"
#include "NotifyWindow.h"

//+---------------------------------------------------------------------------
//
// ctor
//
//----------------------------------------------------------------------------

CNotifyWindow::CNotifyWindow(_In_ NOTIFYWNDCALLBACK pfnCallback, _In_ void *pv)
{
   
    _pfnCallback = pfnCallback;
    _pObj = pv;

    _pShadowWnd = nullptr;
	_cxTitle = 50;
    _wndWidth = 50;

  
}

//+---------------------------------------------------------------------------
//
// dtor
//
//----------------------------------------------------------------------------

CNotifyWindow::~CNotifyWindow()
{
    _DeleteShadowWnd();
    
}
//+---------------------------------------------------------------------------
//
// _Create
//
// CandidateWinow is the top window
//----------------------------------------------------------------------------

BOOL CNotifyWindow::_Create(ATOM atom, _In_opt_ HWND parentWndHandle)
{
    BOOL ret = FALSE;

    ret = _CreateMainWindow(atom, parentWndHandle);
    if (FALSE == ret)
    {
        goto Exit;
    }

    ret = _CreateBackGroundShadowWindow();
    if (FALSE == ret)
    {
        goto Exit;
    }

    _ResizeWindow();

Exit:
    return TRUE;
}

BOOL CNotifyWindow::_CreateMainWindow(ATOM atom, _In_opt_ HWND parentWndHandle)
{
    _SetUIWnd(this);

    if (!CBaseWindow::_Create(atom,
        WS_EX_TOPMOST |
		WS_EX_TOOLWINDOW,
        WS_BORDER | WS_POPUP,
        NULL, 0, 0, parentWndHandle))
    {
        return FALSE;
    }

    return TRUE;
}

BOOL CNotifyWindow::_CreateBackGroundShadowWindow()
{
    _pShadowWnd = new (std::nothrow) CShadowWindow(this);
    if (_pShadowWnd == nullptr)
    {
        return FALSE;
    }

    if (!_pShadowWnd->_Create(Global::AtomShadowWindow,
        WS_EX_TOPMOST | 
		WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        WS_DISABLED | WS_POPUP, this))
    {
        _DeleteShadowWnd();
        return FALSE;
    }

    return TRUE;
}


void CNotifyWindow::_ResizeWindow()
{
    SIZE size = {0, 0};

    //_cxTitle = max(_cxTitle, size.cx + 2 * GetSystemMetrics(SM_CXFRAME));

    
	CBaseWindow::_Resize(0, 0, _cxTitle, _wndWidth);  //x, y, cx, cy

    
}

//+---------------------------------------------------------------------------
//
// _Move
//
//----------------------------------------------------------------------------

void CNotifyWindow::_Move(int x, int y)
{
    CBaseWindow::_Move(x, y);
}

//+---------------------------------------------------------------------------
//
// _Show
//
//----------------------------------------------------------------------------

void CNotifyWindow::_Show(BOOL isShowWnd)
{
    if (_pShadowWnd)
    {
        _pShadowWnd->_Show(isShowWnd);
    }
    CBaseWindow::_Show(isShowWnd);
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

VOID CNotifyWindow::_SetFillColor(_In_ HBRUSH hBrush)
{
    _brshBkColor = hBrush;
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
                GetTextMetrics(dcHandle, &_TextMetric);

                //_cxTitle = _TextMetric.tmMaxCharWidth * _wndWidth;
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
            if (_pShadowWnd)
            {
                _pShadowWnd->_OnOwnerWndMoved((pWndPos->flags & SWP_NOSIZE) == 0);
            }

            _FireMessageToLightDismiss(wndHandle, pWndPos);
        }
        break;

    case WM_WINDOWPOSCHANGING:
        {
            WINDOWPOS* pWndPos = (WINDOWPOS*)lParam;

            // show/hide shadow
            if (_pShadowWnd)
            {
                if ((pWndPos->flags & SWP_HIDEWINDOW) != 0)
                {
                    _pShadowWnd->_Show(FALSE);
                }

                // don't go behaind of shadow
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
            HDC dcHandle = nullptr;
            PAINTSTRUCT ps;

            dcHandle = BeginPaint(wndHandle, &ps);
            _OnPaint(dcHandle, &ps);
            _DrawBorder(wndHandle, NOTIFYWND_BORDER_WIDTH*2);
            EndPaint(wndHandle, &ps);
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
            POINT point;

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

    FillRect(dcHandle, &pPaintStruct->rcPaint, _brshBkColor);

    _DrawText(dcHandle, &pPaintStruct->rcPaint);

//cleanup:
    SelectObject(dcHandle, hFontOld);
}
/*
//+---------------------------------------------------------------------------
//
// _OnLButtonDown
//
//----------------------------------------------------------------------------

void CNotifyWindow::_OnLButtonDown(POINT pt)
{
   
   return;
}

//+---------------------------------------------------------------------------
//
// _OnLButtonUp
//
//----------------------------------------------------------------------------

void CNotifyWindow::_OnLButtonUp(POINT pt)
{
	return;
   
}

//+---------------------------------------------------------------------------
//
// _OnMouseMove
//
//----------------------------------------------------------------------------

void CNotifyWindow::_OnMouseMove(POINT pt)
{
   return;
}
*/

//+---------------------------------------------------------------------------
//
// _DrawText
//
//----------------------------------------------------------------------------

void CNotifyWindow::_DrawText(_In_ HDC dcHandle, _In_ RECT *prc)
{
    
    int cxLine = _TextMetric.tmAveCharWidth;
    int cyLine = _TextMetric.tmHeight;
//    int cyOffset = (cyLine-_TextMetric.tmHeight)/2;

    RECT rc;
	rc.top = prc->top +  cyLine;
    rc.bottom = rc.top + cyLine;

    rc.left = prc->left + cxLine;
	rc.right = prc->left +(LONG) notifyText.GetLength()* cxLine;
	
    SetTextColor(dcHandle, NOTIFYWND_TEXT_COLOR);
    SetBkColor(dcHandle, NOTIFYWND_TEXT_BK_COLOR);
        
    
    ExtTextOut(dcHandle, 0, 0, ETO_OPAQUE, &rc, 
			notifyText.Get(), (DWORD)notifyText.GetLength(), NULL);
    
    
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
    // zero based
    OffsetRect(&rcWnd, -rcWnd.left, -rcWnd.top); 

    HPEN hPen = CreatePen(PS_DOT, cx, NOTIFYWND_BORDER_COLOR);
    HPEN hPenOld = (HPEN)SelectObject(dcHandle, hPen);
    HBRUSH hBorderBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
    HBRUSH hBorderBrushOld = (HBRUSH)SelectObject(dcHandle, hBorderBrush);

    Rectangle(dcHandle, rcWnd.left, rcWnd.top, rcWnd.right, rcWnd.bottom);

    SelectObject(dcHandle, hPenOld);
    SelectObject(dcHandle, hBorderBrushOld);
    DeleteObject(hPen);
    DeleteObject(hBorderBrush);
    ReleaseDC(wndHandle, dcHandle);

}

//+---------------------------------------------------------------------------
//
// _AddString
//
//----------------------------------------------------------------------------

void CNotifyWindow::_AddString(_Inout_ const WCHAR *pNotifyText)
{
    size_t notifyTextLen = wcslen(pNotifyText);
    WCHAR* pwchString = nullptr;
    if (notifyTextLen)
    {
		pwchString = new (std::nothrow) WCHAR[ notifyText.GetLength() + notifyTextLen + 2];
        if (!pwchString)
        {
            return;
        }
		StringCchCopyN(pwchString, notifyText.GetLength() + notifyTextLen + 2, notifyText.Get(),notifyText.GetLength()); 
		StringCchCatN(pwchString,notifyText.GetLength() + notifyTextLen + 2, pNotifyText, notifyTextLen); 
		notifyText.Set(pwchString, notifyText.GetLength() + notifyTextLen);
    }

   
}

//+---------------------------------------------------------------------------
//
// _SetString
//
//----------------------------------------------------------------------------

void CNotifyWindow::_SetString(_Inout_ const WCHAR *pNotifyText)
{
    size_t notifyTextLen = wcslen(pNotifyText);
    WCHAR* pwchString = nullptr;
    if (notifyTextLen)
    {
        pwchString = new (std::nothrow) WCHAR[ notifyTextLen + 1];
        if (!pwchString)
        {
            return;
        }
		StringCchCopyN(pwchString, notifyTextLen + 1, pNotifyText, notifyTextLen);
		notifyText.Set(pwchString, notifyTextLen);
    }

   
}

//+---------------------------------------------------------------------------
//
// _Clear
//
//----------------------------------------------------------------------------

void CNotifyWindow::_Clear()
{
	notifyText.Set(L'\0',1);
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
    if (nullptr != _pShadowWnd)
    {
        delete _pShadowWnd;
        _pShadowWnd = nullptr;
    }
}

