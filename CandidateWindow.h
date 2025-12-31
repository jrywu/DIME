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



#pragma once

#include "private.h"
#include "BaseWindow.h"
#include "ShadowWindow.h"
#include "ScrollBarWindow.h"
#include "BaseStructure.h"
#include "Define.h"

enum class CANDWND_ACTION
{
    CAND_ITEM_SELECT,
	CAND_CANCEL
};

typedef HRESULT (*CANDWNDCALLBACK)(void *pv, enum CANDWND_ACTION action);

class CCandidateWindow : public CBaseWindow
{
public:
    CCandidateWindow(_In_ CANDWNDCALLBACK pfnCallback, _In_ void *pv, _In_ CCandidateRange *pIndexRange, _In_ BOOL isStoreAppMode);
    virtual ~CCandidateWindow();

    BOOL _Create(_In_ UINT wndWidth, _In_ UINT fontSize, _In_opt_ HWND parentWndHandle);

	void _SetCandIndexRange(CCandidateRange* pIndexRange){ _pIndexRange = pIndexRange; }

	void _SetCandStringLength(_In_ UINT wndWidth) { _wndWidth = wndWidth; } // in chararacters
	UINT _GetWidth() { return _cxTitle + GetSystemMetrics(SM_CXVSCROLL) * 3/2  + CANDWND_BORDER_WIDTH *2;}


    void _Move(int x, int y);
	void _OnTimerID(UINT_PTR timerID);
    void _Show(BOOL isShowWnd);

	VOID _SetNumberColor(_In_ COLORREF crColor, _In_ COLORREF crBkColor);
    VOID _SetTextColor(_In_ COLORREF crColor, _In_ COLORREF crBkColor);
	VOID _SetSelectedTextColor(_In_ COLORREF crColor, _In_ COLORREF crBkColor);
    VOID _SetFillColor(_In_ COLORREF fiColor);

    LRESULT CALLBACK _WindowProcCallback(_In_ HWND wndHandle, UINT uMsg, _In_ WPARAM wParam, _In_ LPARAM lParam);
    void _OnPaint(_In_ HDC dcHandle, _In_ PAINTSTRUCT *pps);
    void _OnLButtonDown(POINT pt);
    void _OnLButtonUp(POINT pt);
    void _OnMouseMove(POINT pt);
    void _OnVScroll(DWORD dwSB, _In_ DWORD nPos);

    void _AddString(_Inout_ CCandidateListItem *pCandidateItem, _In_ BOOL isAddFindKeyCode);
    void _ClearList();
    UINT _GetCount() { return _candidateList.Count(); }
    INT _GetSelection()  { return _currentSelection;}
    void _SetScrollInfo(_In_ int nMax, _In_ int nPage);

    DWORD _GetCandidateString(_In_ int iIndex, _Inout_opt_ const WCHAR **ppwchCandidateString);
    DWORD _GetSelectedCandidateString(_Inout_opt_ const WCHAR **ppwchCandidateString);
	DWORD _GetSelectedCandidateKeyCode(_Inout_opt_ const WCHAR **ppwchCandidateString);

    BOOL _MoveSelection(_In_ int offSet, _In_ BOOL isNotify);
    BOOL _SetSelection(_In_ int iPage, _In_ BOOL isNotify);
    void _SetSelection(_In_ int nIndex);
    BOOL _MovePage(_In_ int offSet, _In_ BOOL isNotify);
    BOOL _SetSelectionInPage(int nPos);

    HRESULT _GetPageIndex(UINT *pIndex, _In_ UINT uSize, _Inout_ UINT *puPageCnt);
    HRESULT _SetPageIndex(UINT *pIndex, _In_ UINT uPageCnt);
    HRESULT _GetCurrentPage(_Inout_ UINT *pCurrentPage);
    HRESULT _GetCurrentPage(_Inout_ int *pCurrentPage);

private:
    void _HandleMouseMsg(_In_ UINT mouseMsg, _In_ POINT point);
    void _DrawList(_In_ HDC dcHandle, _In_ UINT iIndex, _In_ RECT *prc);
    void _DrawBorder(_In_ HWND wndHandle, _In_ int cx);
    BOOL _SetSelectionOffset(_In_ int offSet);
    BOOL _AdjustPageIndexForSelection();
    HRESULT _CurrentPageHasEmptyItems(_Inout_ BOOL *pfHasEmptyItems);

	// LightDismiss feature support, it will fire messages lightdismiss-related to the light dismiss layout.
    void _FireMessageToLightDismiss(_In_ HWND wndHandle, _In_opt_ WINDOWPOS *pWndPos);

    BOOL _CreateMainWindow(_In_opt_ HWND parentWndHandle);
    BOOL _CreateBackGroundShadowWindow();
    BOOL _CreateVScrollWindow();

    HRESULT _AdjustPageIndex(_Inout_ UINT & currentPage, _Inout_ UINT & currentPageIndex);

    void _ResizeWindow();
    void _DeleteShadowWnd();
    void _DeleteVScrollBarWnd();

    friend COLORREF _AdjustTextColor(_In_ COLORREF crColor, _In_ COLORREF crBkColor);

private:
    INT _currentSelection;
    CDIMEArray<CCandidateListItem> _candidateList;
    CDIMEArray<UINT> _PageIndex;

	COLORREF _crNumberColor;
    COLORREF _crNumberBkColor;
    COLORREF _crTextColor;
    COLORREF _crBkColor;
	COLORREF _crSelectedTextColor;
    COLORREF _crSelectedBkColor;
    HBRUSH _brshBkColor;

    TEXTMETRIC _TextMetric;
    int _cyRow;
    int _cxTitle;
    UINT _wndWidth;
	UINT _fontSize;

    CCandidateRange* _pIndexRange;

    CANDWNDCALLBACK _pfnCallback;
    void* _pObj;

    std::unique_ptr<CShadowWindow> _pShadowWnd;
    std::unique_ptr<CScrollBarWindow> _pVScrollBarWnd;

    BOOL _dontAdjustOnEmptyItemPage;
    BOOL _isStoreAppMode;

	BYTE _animationStage;

	int _x;
	int _y;
};
