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
#include "BaseWindow.h"
#include "CandidateWindow.h"
#include <memory> // Include for smart pointers

#define NO_ANIMATION
#define NO_WINDOW_SHADOW
#define ANIMATION_STEP_TIME 8
#define ANIMATION_TIMER_ID 39773
//+---------------------------------------------------------------------------
//
// ctor
//
//----------------------------------------------------------------------------

CCandidateWindow::CCandidateWindow(_In_ CANDWNDCALLBACK pfnCallback, _In_ void *pv, _In_ CCandidateRange *pIndexRange, _In_ BOOL isStoreAppMode)
{
	
	debugPrint(L"CCandidateWindow::CCandidateWindow() gdiObjects = %d", GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS));

	_brshBkColor = nullptr;

    _currentSelection = 0;

    _SetTextColor(CANDWND_ITEM_COLOR, GetSysColor(COLOR_WINDOW));    // text color is black
    _SetFillColor(GetSysColor(COLOR_3DHIGHLIGHT));

    _pIndexRange = pIndexRange;

    _pfnCallback = pfnCallback;
    _pObj = pv;

    _pShadowWnd = nullptr;

    _cyRow = CANDWND_ROW_WIDTH;
    _cxTitle = 0;

    _pVScrollBarWnd = nullptr;

    _wndWidth = 0;

    _dontAdjustOnEmptyItemPage = FALSE;

    _isStoreAppMode = isStoreAppMode;

	_fontSize = _TextMetric.tmHeight;

	_x = -32768;
	_y = -32768;

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
        WS_EX_TOPMOST |  WS_EX_LAYERED |
		WS_EX_TOOLWINDOW, 
        WS_BORDER | WS_POPUP,
        NULL, 0, 0, parentWndHandle))
    {
        return FALSE;
    }
	SetLayeredWindowAttributes(_GetWnd(), 0, (255 * 5) / 100, LWA_ALPHA);  // animation started from 5% alpha

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
        _DeleteShadowWnd();
        goto Exit;
    }

    _pVScrollBarWnd->_SetUIWnd(this);

    if (!_pVScrollBarWnd->_Create(Global::AtomCandidateScrollBarWindow, 
		WS_EX_TOPMOST | WS_EX_TOOLWINDOW , WS_CHILD , this))
    {
        _DeleteVScrollBarWnd();
        _DeleteShadowWnd();
        goto Exit;
    }
    
    ret = TRUE;

Exit:
    pFactory->Release();
    return ret;
}

void CCandidateWindow::_ResizeWindow()
{

	debugPrint(L"CCandidateWindow::_ResizeWindow() _cxTitle = %d", _cxTitle);
	if(_pIndexRange == nullptr) return;
    int candidateListPageCnt = _pIndexRange->Count();
	int VScrollWidth = GetSystemMetrics(SM_CXVSCROLL) * 3/2;
	CBaseWindow::_Resize(_x, _y, _cxTitle + VScrollWidth +  CANDWND_BORDER_WIDTH*2, 
		_cyRow * candidateListPageCnt + _cyRow /2  + CANDWND_BORDER_WIDTH *2);

    RECT rcCandRect = {0, 0, 0, 0};
    _GetClientRect(&rcCandRect);


    int left = rcCandRect.right - VScrollWidth;
    int top = rcCandRect.top;
    int width = VScrollWidth;
    int height = rcCandRect.bottom - rcCandRect.top;

	if(_pVScrollBarWnd)
		_pVScrollBarWnd->_Resize(left, top, width, height);
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
#ifdef NO_ANIMATION
    SetLayeredWindowAttributes(_GetWnd(), 0, (255 * 95) / 100, LWA_ALPHA);
#else
    SetLayeredWindowAttributes(_GetWnd(), 0, 255 * (5 / 100), LWA_ALPHA); // staring from 30% transparent 
	_animationStage = 10;	
	_EndTimer(ANIMATION_TIMER_ID);
	_StartTimer(ANIMATION_STEP_TIME, ANIMATION_TIMER_ID);
#endif
}

void CCandidateWindow::_OnTimerID(UINT_PTR timerID)
{   //animate the window faded out with layered transparency
	debugPrint(L"CCandidateWindow::_OnTimer(): timerID = %d,  _animationStage = %d", timerID, _animationStage);
	switch (timerID)
	{
	case ANIMATION_TIMER_ID:
		if(_animationStage)
		{
			BYTE transparentLevel = (255 * (5 + 9 * (11 - _animationStage))) / 100; 
			debugPrint(L"CCandidateWindow::_OnTimer() transparentLevel = %d", transparentLevel);

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
	}
}

//+---------------------------------------------------------------------------
//
// _Show
//
//----------------------------------------------------------------------------

void CCandidateWindow::_Show(BOOL isShowWnd)
{
    if (_pShadowWnd)
		_pShadowWnd->_Show(isShowWnd);
	if (_pVScrollBarWnd)
#ifdef ALWAYS_SHOW_SCROLL
		_pVScrollBarWnd->_Show(isShowWnd && _pVScrollBarWnd->_IsEnabled());
#else
		_pVScrollBarWnd->_Show(FALSE);
#endif
    CBaseWindow::_Show(isShowWnd);
	if (isShowWnd && !_IsCapture())
	{
		SetCapture(_GetWnd());
	}
	if (!isShowWnd && _IsCapture())
	{
		ReleaseCapture();
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

const int PageCountPosition = 1;
const int StringPosition = 2;

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
				for (UINT i = 0; i < _wndWidth; i++) StringCchCatN(pwszTestString, static_cast<size_t>(_wndWidth) + 1, L"寬", 1);

				SIZE candSize;
				GetTextExtentPoint32(dcHandle, pwszTestString, _wndWidth, &candSize); //don't trust the TextMetrics. Measurement the font height and width directly.
				delete[]pwszTestString;



				_cxTitle = candSize.cx + StringPosition * (candSize.cx / _wndWidth);
				_cyRow = candSize.cy * 5 / 4;

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
			_pVScrollBarWnd->_Show((BOOL)wParam);
		}
        break;

    case WM_PAINT:
        {
			debugPrint(L"CCandidateWindow::_WindowProcCallback():WM_PAINT gdiObjects = %d", GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS));
            HDC dcHandle = nullptr;
            PAINTSTRUCT ps;

            dcHandle = BeginPaint(wndHandle, &ps);
			_DrawBorder(wndHandle, CANDWND_BORDER_WIDTH);
            _OnPaint(dcHandle, &ps);
            EndPaint(wndHandle, &ps);
			ReleaseDC(wndHandle, dcHandle);
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
    case WM_VSCROLL:
        _OnVScroll(LOWORD(wParam), HIWORD(wParam));
        return 0;
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
	if(_pIndexRange == nullptr) return;

    RECT rcWindow = {0, 0, 0, 0};;
    _GetClientRect(&rcWindow);

    int cyLine = _cyRow;
    
    UINT candidateListPageCnt = _pIndexRange->Count();
    UINT index = 0;
    int currentPage = 0;

    if (FAILED(_GetCurrentPage(&currentPage)))
    {
        return;
    }

    // Hit test on list items
    index = *_PageIndex.GetAt(currentPage);


	RECT rc = {0, 0, 0, 0};
	rc.left = rcWindow.left + PageCountPosition* _TextMetric.tmAveCharWidth;
    rc.right = rcWindow.right  - GetSystemMetrics(SM_CXVSCROLL) * 3/2 - CANDWND_BORDER_WIDTH;

    for (UINT pageCount = 0; (index < _candidateList.Count()) && (pageCount < candidateListPageCnt); index++, pageCount++)
    {	
        rc.top = rcWindow.top + (pageCount * cyLine);
        rc.bottom = rcWindow.top + ((pageCount + 1) * cyLine);

        if (PtInRect(&rc, pt) && _pfnCallback)
        {
            SetCursor(LoadCursor(NULL, IDC_HAND));
            _currentSelection = index;
            _pfnCallback(_pObj, CANDWND_ACTION::CAND_ITEM_SELECT);
            return;
        }
    }

    SetCursor(LoadCursor(NULL, IDC_ARROW));

    if (_pVScrollBarWnd && _pVScrollBarWnd->_IsEnabled())
    {
        rc = {0, 0, 0, 0};

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
#ifndef ALWAYS_SHOW_SCROLL
	if (PtInRect(&rcWindow, pt))
	{		
		if(_pVScrollBarWnd)
		 _pVScrollBarWnd->_Show(_pVScrollBarWnd->_IsEnabled());
	}
	else
	{
		if (_pVScrollBarWnd)
	 		_pVScrollBarWnd->_Show(FALSE);
	}
#endif

	rc.left = rcWindow.left;
	rc.right = rcWindow.right - GetSystemMetrics(SM_CXVSCROLL) * 3 / 2 - CANDWND_BORDER_WIDTH;

	rc.top = rcWindow.top;
	rc.bottom = rcWindow.bottom;

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
    switch (dwSB)
    {
    case SB_LINEDOWN:
        _SetSelectionOffset(+1);
        _InvalidateRect();
        break;
    case SB_LINEUP:
        _SetSelectionOffset(-1);
        _InvalidateRect();
        break;
    case SB_PAGEDOWN:
        _MovePage(+1, FALSE);
        _InvalidateRect();
        break;
    case SB_PAGEUP:
        _MovePage(-1, FALSE);
        _InvalidateRect();
        break;
    case SB_THUMBPOSITION:
        _SetSelection(nPos, FALSE);
        _InvalidateRect();
        break;
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
	int VScrollWidth = GetSystemMetrics(SM_CXVSCROLL) *3/2;

    RECT rc = { 0,0,0,0 };
	const size_t numStringLen = 2;
	
	GetTextMetrics(dcHandle, &_TextMetric);
	SIZE candSize = { 0,0 };
	PWCHAR pwszTestString = new (std::nothrow) WCHAR[_wndWidth + 1];
	if (pwszTestString)
	{
		pwszTestString[0] = L'\0';
		for (UINT i = 0; i < _wndWidth; i++) StringCchCatN(pwszTestString, static_cast<size_t>(_wndWidth) + 1, L"寬", 1);
		GetTextExtentPoint32(dcHandle, pwszTestString, _wndWidth, &candSize); //don't trust the TextMetrics. Measurement the font height and width directly.
		delete[]pwszTestString;

	}

	
	int cxLine = max((int)_TextMetric.tmAveCharWidth, (int)candSize.cx / (int)_wndWidth);
	int cyLine = candSize.cy * 5 / 4; 

	int fistLineOffset = cyLine / 4;

	_cxTitle = candSize.cx + StringPosition * cxLine;
	_cyRow = cyLine;

	int cyOffset = candSize.cy / 8 + fistLineOffset; //offset in line + blank before 1st line.
	

    for (;
		(currentPageIndex < _candidateList.Count()) && (indexInPage < candidateListPageCnt);
		currentPageIndex++, indexInPage++)
    {
        WCHAR wszNumString[numStringLen] = {'\0'};
        CCandidateListItem* pItemList = nullptr;

		rc.top = prc->top + indexInPage * cyLine + fistLineOffset;
        rc.bottom = rc.top + cyLine;


        rc.left = prc->left + PageCountPosition * cxLine;
        rc.right = prc->left + StringPosition * cxLine;

        // Number Font Color And BK
        SetTextColor(dcHandle, _crNumberColor);
        SetBkColor(dcHandle, _crNumberBkColor);

        // Print cand list index 
		StringCchPrintf(wszNumString, ARRAYSIZE(wszNumString), L"%c", _pIndexRange->GetAt(indexInPage)->CandIndex);
		ExtTextOut(dcHandle, PageCountPosition * cxLine, indexInPage * cyLine + cyOffset, ETO_OPAQUE, &rc, wszNumString, numStringLen, NULL);

        rc.left = prc->left + StringPosition * cxLine;
        rc.right = prc->right - VScrollWidth;

        // Candidate Font Color And BK
        if (_currentSelection != (INT)currentPageIndex)
        {
            SetTextColor(dcHandle, _crTextColor);
            SetBkColor(dcHandle, _crBkColor); 
        }
        else
        {
            SetTextColor(dcHandle, _crSelectedTextColor);
            SetBkColor(dcHandle, _crSelectedBkColor);
        }

		pItemList = _candidateList.GetAt(currentPageIndex);
		if(pItemList == nullptr) continue;
		SIZE size;
		WCHAR itemText[MAX_CAND_ITEM_LENGTH + 3] = { '\0' };
		int itemLength = (int)pItemList->_ItemString.GetLength();
		if (itemLength > MAX_CAND_ITEM_LENGTH) // if the length of item text > MAX_CAND_ITEM_LENGTH (13) , truncate the item text as "First 6 chars of itemtext"+"..."+"last 6 chars of itemtext"
		{
			StringCchCatN(itemText, MAX_CAND_ITEM_LENGTH + 3, pItemList->_ItemString.Get(), 6);
			StringCchCatN(itemText, MAX_CAND_ITEM_LENGTH + 3, L"…", 1);
			StringCchCatN(itemText, MAX_CAND_ITEM_LENGTH + 3, (pItemList->_ItemString.Get() + itemLength - 6), 6);
			itemLength = MAX_CAND_ITEM_LENGTH;
		}
		else
			StringCchCopyN(itemText, MAX_CAND_ITEM_LENGTH + 3, pItemList->_ItemString.Get(), itemLength);

		if (itemLength > (int)_wndWidth)
		{
			GetTextExtentPoint32(dcHandle, pItemList->_ItemString.Get(), (int)pItemList->_ItemString.GetLength(), &size);
			if (_cxTitle < (int)size.cx + StringPosition * cxLine)
			{
				_cxTitle = size.cx + StringPosition * cxLine;
				_wndWidth = itemLength;
			}
		}

		ExtTextOut(dcHandle, StringPosition * cxLine, indexInPage * cyLine + cyOffset, ETO_OPAQUE, &rc, itemText, (DWORD)itemLength, NULL);
	
    }
	for (; (indexInPage < candidateListPageCnt); indexInPage++)
    {
		rc.top = prc->top + indexInPage * cyLine + fistLineOffset;
        rc.bottom = rc.top + cyLine;

        rc.left   = prc->left + PageCountPosition * cxLine;
        rc.right  = prc->left + (PageCountPosition+1) * cxLine;

        if(_brshBkColor) FillRect(dcHandle, &rc, _brshBkColor);
    }

	if(_cxTitle != prc->right - prc->left - VScrollWidth) _ResizeWindow();
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

    GetWindowRect(wndHandle, &rcWnd);
    // zero based
    OffsetRect(&rcWnd, -rcWnd.left, -rcWnd.top); 

    HPEN hPen = CreatePen(PS_SOLID, cx, CANDWND_BORDER_COLOR);
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

void CCandidateWindow::_AddString(_Inout_ CCandidateListItem *pCandidateItem, _In_ BOOL isAddFindKeyCode)
{
	debugPrint(L"CCandidateWindow::_AddString()");
	if(pCandidateItem == nullptr) return;
    DWORD_PTR dwItemString = pCandidateItem->_ItemString.GetLength();
    const WCHAR* pwchString = nullptr;
    if (dwItemString)
    {
        pwchString = new (std::nothrow) WCHAR[ dwItemString ];
        if (!pwchString)
        {
            return;
        }
        memcpy((void*)pwchString, pCandidateItem->_ItemString.Get(), dwItemString * sizeof(WCHAR));
    }

    DWORD_PTR itemWildcard = pCandidateItem->_FindKeyCode.GetLength();
    const WCHAR* pwchWildcard = nullptr;
    if (itemWildcard && isAddFindKeyCode)
    {
        pwchWildcard = new (std::nothrow) WCHAR[ itemWildcard ];
        if (!pwchWildcard)
        {
            if (pwchString)
            {
                delete [] pwchString;
            }
            return;
        }
        memcpy((void*)pwchWildcard, pCandidateItem->_FindKeyCode.Get(), itemWildcard * sizeof(WCHAR));
    }

    CCandidateListItem* pLI = nullptr;
    pLI = _candidateList.Append();
    if (!pLI)
    {
        if (pwchString)
        {
            delete [] pwchString;
            pwchString = nullptr;
        }
        if (pwchWildcard)
        {
            delete [] pwchWildcard;
            pwchWildcard = nullptr;
        }
        return;
    }

    if (pwchString && pLI)
    {
        pLI->_ItemString.Set(pwchString, dwItemString);
    }
    if (pwchWildcard)
    {
        pLI->_FindKeyCode.Set(pwchWildcard, itemWildcard);
    }
#ifndef ALWAYS_SHOW_SCROLL
	if(_pVScrollBarWnd) _pVScrollBarWnd->_Show(FALSE); // hide the scrollbar in the beginning
#endif
    return;
}

//+---------------------------------------------------------------------------
//
// _ClearList
//
//----------------------------------------------------------------------------

void CCandidateWindow::_ClearList()
{
    for (UINT index = 0; index < _candidateList.Count(); index++)
    {
        CCandidateListItem* pItemList = nullptr;
        pItemList = _candidateList.GetAt(index);
        delete [] pItemList->_ItemString.Get();
        delete [] pItemList->_FindKeyCode.Get();
    }
    _currentSelection = 0;
    _candidateList.Clear();
    _PageIndex.Clear();
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
// _GetCandidateString
//
//----------------------------------------------------------------------------

DWORD CCandidateWindow::_GetCandidateString(_In_ int iIndex, _Inout_opt_ const WCHAR **ppwchCandidateString)
{
    CCandidateListItem* pItemList = nullptr;

    if (iIndex < 0)
    {
		if (ppwchCandidateString)
			*ppwchCandidateString = nullptr;
        return 0;
    }

    UINT index = static_cast<UINT>(iIndex);
	
	if (index >= _candidateList.Count() )
    {
		if(ppwchCandidateString)
			*ppwchCandidateString = nullptr;
        return 0;
    }

    pItemList = _candidateList.GetAt(iIndex);
	if(pItemList == nullptr) return 0;
    if (ppwchCandidateString)
    {
        *ppwchCandidateString = pItemList->_ItemString.Get();
    }
    return (DWORD)pItemList->_ItemString.GetLength();
}

//+---------------------------------------------------------------------------
//
// _GetSelectedCandidateString
//
//----------------------------------------------------------------------------

DWORD CCandidateWindow::_GetSelectedCandidateString(_Inout_opt_ const WCHAR **ppwchCandidateString)
{
    CCandidateListItem* pItemList = nullptr;

    if (_currentSelection < 0 || (UINT) _currentSelection >= _candidateList.Count())
    {
		if(ppwchCandidateString)
			*ppwchCandidateString = nullptr;
        return 0;
    }

    pItemList = _candidateList.GetAt(_currentSelection);
	if(pItemList == nullptr) return 0;
    if (ppwchCandidateString)
    {
        *ppwchCandidateString = pItemList->_ItemString.Get();
    }
    return (DWORD)pItemList->_ItemString.GetLength();
}

// +-------------------------------------------------------------------------- -
//
// _GetSelectedCandidateKeyCode
//
//----------------------------------------------------------------------------

DWORD CCandidateWindow::_GetSelectedCandidateKeyCode(_Inout_opt_ const WCHAR **ppwchCandidateString)
{
	CCandidateListItem* pItemList = nullptr;

	if (_currentSelection < 0 || (UINT)_currentSelection >= _candidateList.Count())
	{
		if(ppwchCandidateString)
			*ppwchCandidateString = nullptr;
		return 0;
	}

	pItemList = _candidateList.GetAt(_currentSelection);
	if (pItemList == nullptr) return 0;
	if (ppwchCandidateString)
	{
		*ppwchCandidateString = pItemList->_FindKeyCode.Get();
	}
	return (DWORD)pItemList->_FindKeyCode.GetLength();
}

//+---------------------------------------------------------------------------
//
// _SetSelectionInPage
//
//----------------------------------------------------------------------------

BOOL CCandidateWindow::_SetSelectionInPage(int nPos)
{	
    if (nPos < 0)
    {
        return FALSE;
    }

    UINT pos = static_cast<UINT>(nPos);

    if (pos >= _candidateList.Count())
    {
        return FALSE;
    }

    int currentPage = 0;
    if (FAILED(_GetCurrentPage(&currentPage)))
    {
        return FALSE;
    }

    _currentSelection = *_PageIndex.GetAt(currentPage) + nPos;

    return TRUE;
}

//+---------------------------------------------------------------------------
//
// _MoveSelection
//
//----------------------------------------------------------------------------

BOOL CCandidateWindow::_MoveSelection(_In_ int offSet, _In_ BOOL isNotify)
{
    if (_currentSelection + offSet >= (INT) _candidateList.Count() || _currentSelection + offSet <0)
    {
        return FALSE;
    }

    _currentSelection += offSet;

    _dontAdjustOnEmptyItemPage = TRUE;

    if (_pVScrollBarWnd && isNotify)
    {
        _pVScrollBarWnd->_ShiftLine(offSet, isNotify);
    }

    return TRUE;
}

//+---------------------------------------------------------------------------
//
// _SetSelection
//
//----------------------------------------------------------------------------

BOOL CCandidateWindow::_SetSelection(_In_ int selectedIndex, _In_ BOOL isNotify)
{
    if (selectedIndex == -2)
    {
        selectedIndex = _candidateList.Count() - 1;
    }

    if (selectedIndex < -2) //-1 is used for phrase candidates
    {
        return FALSE;
    }

    int candCnt = static_cast<int>(_candidateList.Count());
    if (selectedIndex >= candCnt)
    {
        return FALSE;
    }

    _currentSelection = static_cast<UINT>(selectedIndex);

    BOOL ret = _AdjustPageIndexForSelection();

    if (_pVScrollBarWnd && isNotify)
    {
        _pVScrollBarWnd->_ShiftPosition(selectedIndex, isNotify);
    }

    return ret;
}

//+---------------------------------------------------------------------------
//
// _SetSelection
//
//----------------------------------------------------------------------------
void CCandidateWindow::_SetSelection(_In_ int nIndex)
{
    _currentSelection = nIndex;
}

//+---------------------------------------------------------------------------
//
// _MovePage
//
//----------------------------------------------------------------------------

BOOL CCandidateWindow::_MovePage(_In_ int offSet, _In_ BOOL isNotify)
{
    if (offSet == 0)
    {
        return TRUE;
    }

    int currentPage = 0;
    int selectionOffset = 0;
    int newPage = 0;

    if (FAILED(_GetCurrentPage(&currentPage)))
    {
        return FALSE;
    }

    newPage = currentPage + offSet;
    if (newPage < 0)
	{
		_currentSelection = 0;
		_InvalidateRect();
		return FALSE;
	}
	else if(newPage >= static_cast<int>(_PageIndex.Count()))
    {
		newPage = currentPage;
		//_currentSelection = 0;
		_InvalidateRect();
        return FALSE;
    }
	if(_currentSelection <0 ) _currentSelection = 0;//reset the selection position for phrase cand (_currentselection is -1);

    // If current selection is at the top of the page AND 
    // we are on the "default" page border, then we don't
    // want adjustment to eliminate empty entries.
    //
    // We do this for keeping behavior inline with down level.
    if (_pIndexRange && _currentSelection % _pIndexRange->Count() == 0 && 
        _currentSelection == (INT) *_PageIndex.GetAt(currentPage)) 
    {
        _dontAdjustOnEmptyItemPage = TRUE;
    }

    selectionOffset = _currentSelection - *_PageIndex.GetAt(currentPage);
    _currentSelection = *_PageIndex.GetAt(newPage) + selectionOffset;
    _currentSelection = _candidateList.Count() > (UINT)_currentSelection ? _currentSelection : _candidateList.Count() - 1;

    // adjust scrollbar position
    if (_pVScrollBarWnd && isNotify)
    {
        _pVScrollBarWnd->_ShiftPage(offSet, isNotify);
    }

    return TRUE;
}

//+---------------------------------------------------------------------------
//
// _SetSelectionOffset
//
//----------------------------------------------------------------------------

BOOL CCandidateWindow::_SetSelectionOffset(_In_ int offSet)
{
	if (_currentSelection + offSet >=(INT) _candidateList.Count() || _currentSelection + offSet <0)
    {
        return FALSE;
    }

    BOOL fCurrentPageHasEmptyItems = FALSE;
    BOOL fAdjustPageIndex = TRUE;

    _CurrentPageHasEmptyItems(&fCurrentPageHasEmptyItems);

    int newOffset = _currentSelection + offSet;

    // For SB_LINEUP and SB_LINEDOWN, we need to special case if CurrentPageHasEmptyItems.
    // CurrentPageHasEmptyItems if we are on the last page.
    if ((offSet == 1 || offSet == -1) &&
        fCurrentPageHasEmptyItems && _PageIndex.Count() > 1)
    {
        int iPageIndex = *_PageIndex.GetAt(static_cast<size_t>(_PageIndex.Count()) - 1);
        // Moving on the last page and last page has empty items.
        if (newOffset >= iPageIndex)
        {
            fAdjustPageIndex = FALSE;
        }
        // Moving across page border.
        else if (newOffset < iPageIndex)
        {
            fAdjustPageIndex = TRUE;
        }

        _dontAdjustOnEmptyItemPage = TRUE;
    }

    _currentSelection = newOffset;

    if (fAdjustPageIndex)
    {
        return _AdjustPageIndexForSelection();
    }

    return TRUE;
}

//+---------------------------------------------------------------------------
//
// _GetPageIndex
//
//----------------------------------------------------------------------------

HRESULT CCandidateWindow::_GetPageIndex(UINT *pIndex, _In_ UINT uSize, _Inout_ UINT *puPageCnt)
{
    HRESULT hr = S_OK;

    if (uSize > _PageIndex.Count())
    {
        uSize = _PageIndex.Count();
    }
    else
    {
        hr = S_FALSE;
    }

    if (pIndex)
    {
        for (UINT i = 0; i < uSize; i++)
        {
            *pIndex = *_PageIndex.GetAt(i);
            pIndex++;
        }
    }

    *puPageCnt = _PageIndex.Count();

    return hr;
}

//+---------------------------------------------------------------------------
//
// _SetPageIndex
//
//----------------------------------------------------------------------------

HRESULT CCandidateWindow::_SetPageIndex(UINT *pIndex, _In_ UINT uPageCnt)
{
    uPageCnt;

    _PageIndex.Clear();

    for (UINT i = 0; i < uPageCnt; i++)
    {
        UINT *pLastNewPageIndex = _PageIndex.Append();
        if (pLastNewPageIndex != nullptr)
        {
            *pLastNewPageIndex = *pIndex;
            pIndex++;
        }
    }

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _GetCurrentPage
//
//----------------------------------------------------------------------------

HRESULT CCandidateWindow::_GetCurrentPage(_Inout_ UINT *pCurrentPage)
{
    HRESULT hr = S_OK;

    if (pCurrentPage == nullptr)
    {
        hr = E_INVALIDARG;
        goto Exit;
    }

    *pCurrentPage = 0;

    if (_PageIndex.Count() == 0)
    {
        hr = E_UNEXPECTED;
        goto Exit;
    }

    if (_PageIndex.Count() == 1 || _currentSelection <0)
    {
        *pCurrentPage = 0;
         goto Exit;
    }

    UINT i = 0;
    for (i = 1; i < _PageIndex.Count(); i++)
    {
        UINT uPageIndex = *_PageIndex.GetAt(i);

        if (uPageIndex >(UINT) _currentSelection)
        {
            break;
        }
    }

    *pCurrentPage = i - 1;

Exit:
    return hr;
}

//+---------------------------------------------------------------------------
//
// _GetCurrentPage
//
//----------------------------------------------------------------------------

HRESULT CCandidateWindow::_GetCurrentPage(_Inout_ int *pCurrentPage)
{
    HRESULT hr = E_FAIL;
    UINT needCastCurrentPage = 0;
    
    if (nullptr == pCurrentPage)
    {
        goto Exit;
    }

    *pCurrentPage = 0;

    hr = _GetCurrentPage(&needCastCurrentPage);
    if (FAILED(hr))
    {
       goto Exit;
    }

    hr = UIntToInt(needCastCurrentPage, pCurrentPage);
    if (FAILED(hr))
    {
        goto Exit;
    }

Exit:
    return hr;
}

//+---------------------------------------------------------------------------
//
// _AdjustPageIndexForSelection
//
//----------------------------------------------------------------------------

BOOL CCandidateWindow::_AdjustPageIndexForSelection()
{
	if (_pIndexRange == nullptr) return FALSE;
    UINT candidateListPageCnt = _pIndexRange->Count();
    UINT* pNewPageIndex = nullptr;
    UINT newPageCnt = 0;

    if (_candidateList.Count() < candidateListPageCnt)
    {
        // no needed to reconstruct page index
        return TRUE;
    }

    // B is number of pages before the current page
    // A is number of pages after the current page
    // uNewPageCount is A + B + 1;
    // A is (uItemsAfter - 1) / candidateListPageCnt + 1 -> 
    //      (_CandidateListCount - _currentSelection - CandidateListPageCount - 1) / candidateListPageCnt + 1->
    //      (_CandidateListCount - _currentSelection - 1) / candidateListPageCnt
    // B is (uItemsBefore - 1) / candidateListPageCnt + 1 ->
    //      (_currentSelection - 1) / candidateListPageCnt + 1
    // A + B is (_CandidateListCount - 2) / candidateListPageCnt + 1

	BOOL isBefore = _currentSelection > 0; //has to consider _currentSelection = -1 for associated phrase
	BOOL isAfter = (_currentSelection <0 && _candidateList.Count() > candidateListPageCnt) || _candidateList.Count() > _currentSelection + candidateListPageCnt;

    // only have current page
    if (!isBefore && !isAfter) 
    {
        newPageCnt = 1;
    }
    // only have after pages; just count the total number of pages
    else if (!isBefore && isAfter)
    {
        newPageCnt = (_candidateList.Count() - 1) / candidateListPageCnt + 1;
    }
    // we are at the last page
    else if (isBefore && !isAfter)
    {
        newPageCnt = 2 + (_currentSelection - 1) / candidateListPageCnt;
    }
    else if (isBefore && isAfter)
    {
        newPageCnt = (_candidateList.Count() - 2) / candidateListPageCnt + 2;
    }

    pNewPageIndex = new (std::nothrow) UINT[ newPageCnt ];
    if (pNewPageIndex == nullptr)
    {
        return FALSE;
    }
    pNewPageIndex[0] = 0;
	UINT firstPage = (_currentSelection < 0) ? candidateListPageCnt : _currentSelection % candidateListPageCnt;
	if (firstPage && newPageCnt > 1)
    {
        pNewPageIndex[1] = firstPage;
    }

    for (UINT i = firstPage ? 2 : 1; i < newPageCnt; ++i)
    {
        pNewPageIndex[i] = pNewPageIndex[i - 1] + candidateListPageCnt;
    }

    _SetPageIndex(pNewPageIndex, newPageCnt);

    delete [] pNewPageIndex;

    return TRUE;
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
// _CurrentPageHasEmptyItems
//
//----------------------------------------------------------------------------

HRESULT CCandidateWindow::_CurrentPageHasEmptyItems(_Inout_ BOOL *hasEmptyItems)
{
	if(_pIndexRange == nullptr) return E_FAIL;
    int candidateListPageCnt = _pIndexRange->Count();
    UINT currentPage = 0;

    if (FAILED(_GetCurrentPage(&currentPage)))
    {
        return S_FALSE;
    }

    if ((currentPage == 0 || currentPage == _PageIndex.Count()-1) &&
        (_PageIndex.Count() > 0) &&
        (*_PageIndex.GetAt(currentPage) > (UINT)(_candidateList.Count() - candidateListPageCnt)))
    {
        *hasEmptyItems = TRUE;
    }
    else 
    {
        *hasEmptyItems = FALSE;
    }

    return S_OK;
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

HRESULT CCandidateWindow::_AdjustPageIndex(_Inout_ UINT & currentPage, _Inout_ UINT & currentPageIndex)
{
    HRESULT hr = E_FAIL;
	if(_pIndexRange == nullptr) return hr;
    UINT candidateListPageCnt = _pIndexRange->Count();

    currentPageIndex = *_PageIndex.GetAt(currentPage);

    BOOL hasEmptyItems = FALSE;
    if (FAILED(_CurrentPageHasEmptyItems(&hasEmptyItems)))
    {
        goto Exit; 
    }

    if (FALSE == hasEmptyItems)
    {
        goto Exit;
    }

    if (TRUE == _dontAdjustOnEmptyItemPage)
    {
        goto Exit;
    }

    UINT tempSelection = _currentSelection;

    // Last page
    UINT candNum = _candidateList.Count();
    UINT pageNum = _PageIndex.Count();

    if ((currentPageIndex > candNum - candidateListPageCnt) && (pageNum > 0) && (currentPage == (pageNum - 1)))
    {
        _currentSelection = candNum - candidateListPageCnt;

        _AdjustPageIndexForSelection();

        _currentSelection = tempSelection;

        if (FAILED(_GetCurrentPage(&currentPage)))
        {
            goto Exit;
        }

        currentPageIndex = *_PageIndex.GetAt(currentPage);
    }
    // First page
    else if ((currentPageIndex < candidateListPageCnt) && (currentPage == 0))
    {
        _currentSelection = 0;

        _AdjustPageIndexForSelection();

        _currentSelection = tempSelection;
    }

    _dontAdjustOnEmptyItemPage = FALSE;
    hr = S_OK;

Exit:
    return hr;
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