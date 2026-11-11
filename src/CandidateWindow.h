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
#include "RustShadowWindow.h"
#if defined(DIME_UNIT_TESTING) && defined(DIME_RUST_AVAILABLE)
#include "RustScrollBarWindow.h"   // the Rust scroll bar beside a C++ candidate window (parity tests)
#endif
#include "ScrollBarWindow.h"
#include "BaseStructure.h"
#include "Define.h"
#include "CandidateListModel.h"

enum class CANDWND_ACTION
{
    CAND_ITEM_SELECT,
	CAND_CANCEL
};

typedef HRESULT (*CANDWNDCALLBACK)(void *pv, enum CANDWND_ACTION action);

#if defined(DIME_UNIT_TESTING) && defined(DIME_USE_RUST_ENGINE)
// The Rust build registers DIME.CandidateWindow with Rust's window procedure (dime-ui::candwin);
// C++ CCandidateWindow objects in tests are created with this class instead (WindowClassTestHelper.h).
extern ATOM g_cppCandidateWindowAtomForTest;
#endif

class CCandidateWindow : public CBaseWindow
{
public:
    CCandidateWindow(_In_ CANDWNDCALLBACK pfnCallback, _In_ void *pv, _In_ CCandidateRange *pIndexRange, _In_ BOOL isStoreAppMode);
    virtual ~CCandidateWindow();

    BOOL _Create(_In_ UINT wndWidth, _In_ UINT fontSize, _In_opt_ HWND parentWndHandle);

	void _SetCandIndexRange(CCandidateRange* pIndexRange){ _pIndexRange = pIndexRange; _model->SetIndexRange(pIndexRange); }

	void _SetCandStringLength(_In_ UINT wndWidth) { _wndWidth = wndWidth; } // in chararacters
	UINT _GetWidth();


    void _Move(int x, int y);
	void _OnTimerID(UINT_PTR timerID);
    void _Show(BOOL isShowWnd);

	VOID _SetNumberColor(_In_ COLORREF crColor, _In_ COLORREF crBkColor);
    VOID _SetTextColor(_In_ COLORREF crColor, _In_ COLORREF crBkColor);
	VOID _SetSelectedTextColor(_In_ COLORREF crColor, _In_ COLORREF crBkColor);
    VOID _SetFillColor(_In_ COLORREF fiColor);

    LRESULT CALLBACK _WindowProcCallback(_In_ HWND wndHandle, UINT uMsg, _In_ WPARAM wParam, _In_ LPARAM lParam);
    void _OnPaint(_In_ HDC dcHandle, _In_ PAINTSTRUCT *pps);
#ifdef DIME_UNIT_TESTING
    // The layout members painting updates (CandidatePaintParityTest).
    struct LayoutStateForTest { int cxTitle; int cyRow; int padding; UINT wndWidth; };
    LayoutStateForTest _GetLayoutStateForTest() const { return { _cxTitle, _cyRow, _padding, _wndWidth }; }
    void _SetLayoutStateForTest(const LayoutStateForTest& s) { _cxTitle = s.cxTitle; _cyRow = s.cyRow; _padding = s.padding; _wndWidth = s.wndWidth; }
    IDimeScrollBar* _GetScrollBarForTest() { return _pVScrollBarWnd.get(); }
    // Which scroll bar the window creates and with which class (before _Create; the class
    // must belong to the implementation: CBaseWindow's or dime-ui's window procedure).
    void _SetScrollBarForTest(BOOL useRust, ATOM atom) { _useRustScrollBar = useRust; _scrollBarAtomForTest = atom; }
    DimeShadowWindow* _GetShadowForTest() { return _pShadowWnd.get(); }
    BOOL _IsTrackingMouseForTest() const { return _isTrackingCandMouse; }
#endif
    void _OnLButtonDown(POINT pt);
    void _OnLButtonUp(POINT pt);
    void _OnMouseMove(POINT pt);
    void _OnVScroll(DWORD dwSB, _In_ DWORD nPos);

    void _AddString(_Inout_ CCandidateListItem *pCandidateItem, _In_ BOOL isAddFindKeyCode);
    void _ClearList();
    UINT _GetCount() { return _model->Count(); }
    INT _GetSelection()  { return _model->Selection();}
    CCandidateListModel* _GetModel() { return _model.get(); }
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

    void _ResizeWindow();

private:
    void _HandleMouseMsg(_In_ UINT mouseMsg, _In_ POINT point);
    int _GetScrollBarWidth();
    void _DrawList(_In_ HDC dcHandle, _In_ UINT iIndex, _In_ RECT *prc);
    void _DrawPageIndicator(_In_ HDC dcHandle, _In_ UINT currentPage, _In_ RECT *prc);
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
    void _DeleteShadowWnd();
    void _DeleteVScrollBarWnd();

    friend COLORREF _AdjustTextColor(_In_ COLORREF crColor, _In_ COLORREF crBkColor);
    friend struct CCandidateWindowSbParent;

private:
    std::unique_ptr<CCandidateListModel> _model;   // items, selection, pages

	COLORREF _crNumberColor;
    COLORREF _crNumberBkColor;
    COLORREF _crTextColor;
    COLORREF _crBkColor;
	COLORREF _crSelectedTextColor;
    COLORREF _crSelectedBkColor;
    HBRUSH _brshBkColor;

    TEXTMETRIC _TextMetric;
    int _cyRow;
    int _padding;
    int _cxTitle;
    UINT _wndWidth;
	UINT _fontSize;

    CCandidateRange* _pIndexRange;

    CANDWNDCALLBACK _pfnCallback;
    void* _pObj;

    std::unique_ptr<DimeShadowWindow> _pShadowWnd;
    std::unique_ptr<IDimeScrollBar> _pVScrollBarWnd;

    BOOL _isStoreAppMode;
    BOOL _isTrackingCandMouse;   // TRUE while TrackMouseEvent(TME_LEAVE) is active
    BOOL _useRustScrollBar;      // tests: CRustScrollBarWindow; otherwise always CScrollBarWindow
    ATOM _scrollBarAtomForTest;  // 0: Global::AtomCandidateScrollBarWindow

	BYTE _animationStage;

	int _x;
	int _y;
};
