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
#ifndef DIMEUIPRESENTER_H
#define DIMEUIPRESENTER_H


#pragma once

#include "KeyHandlerEditSession.h"
#include "CandidateWindow.h"
#include "NotifyWindow.h"
#include "TfTextLayoutSink.h"
#include "DIME.h"
#include "BaseStructure.h"

class CReadingLine;
class CCompositionProcessorEngine;
class CDIME;

//+---------------------------------------------------------------------------
//
// CUIPresenter
//
// ITfCandidateListUIElement / ITfIntegratableCandidateListUIElement is used for 
// UILess mode support
// ITfCandidateListUIElementBehavior sends the Selection behavior message to 
// 3rd party IME.
//----------------------------------------------------------------------------

class CUIPresenter : 
	public CTfTextLayoutSink,
    public ITfCandidateListUIElementBehavior,
    public ITfIntegratableCandidateListUIElement
{
public:
    CUIPresenter(_In_ CDIME *pTextService, CCompositionProcessorEngine *pCompositionProcessorEngine);
    virtual ~CUIPresenter();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, _Outptr_ void **ppvObj);
    STDMETHODIMP_(ULONG) AddRef(void);
    STDMETHODIMP_(ULONG) Release(void);

    // ITfUIElement
    STDMETHODIMP GetDescription(BSTR *pbstr);
    STDMETHODIMP GetGUID(GUID *pguid);
    STDMETHODIMP Show(BOOL showCandidateWindow);
    STDMETHODIMP IsShown(BOOL *pIsShow);

    // ITfCandidateListUIElement (UI-less)
    STDMETHODIMP GetUpdatedFlags(DWORD *pdwFlags);
    STDMETHODIMP GetDocumentMgr(ITfDocumentMgr **ppdim);
    STDMETHODIMP GetCount(UINT *pCandidateCount);
    STDMETHODIMP GetSelection(UINT *pSelectedCandidateIndex);
    STDMETHODIMP GetString(UINT uIndex, BSTR *pbstr);
    STDMETHODIMP GetPageIndex(UINT *pIndex, UINT uSize, UINT *puPageCnt);
    STDMETHODIMP SetPageIndex(UINT *pIndex, UINT uPageCnt);
    STDMETHODIMP GetCurrentPage(UINT *puPage);

    // ITfCandidateListUIElementBehavior methods 
	STDMETHODIMP SetSelection(UINT nIndex); 
    STDMETHODIMP Finalize(void);
    STDMETHODIMP Abort(void);

    // ITfIntegratableCandidateListUIElement (UI-less)
    STDMETHODIMP SetIntegrationStyle(GUID guidIntegrationStyle);
    STDMETHODIMP GetSelectionStyle(_Out_ TfIntegratableCandidateListSelectionStyle *ptfSelectionStyle);
    STDMETHODIMP OnKeyDown(_In_ WPARAM wParam, _In_ LPARAM lParam, _Out_ BOOL *pIsEaten);
    STDMETHODIMP ShowCandidateNumbers(_Out_ BOOL *pIsShow); 
    STDMETHODIMP FinalizeExactCompositionString();

    virtual HRESULT _StartCandidateList(TfClientId tfClientId, _In_ ITfDocumentMgr *pDocumentMgr, _In_ ITfContext *pContextDocument, TfEditCookie ec, _In_ ITfRange *pRangeComposition, UINT wndWidth);
    void _EndCandidateList();

	void _SetCandidateText(_In_ CDIMEArray<CCandidateListItem> *pCandidateList, _In_ CCandidateRange* pIndexRange, BOOL isAddFindKeyCode, UINT candWidth);
    void _ClearCandidateList();
	VOID _SetNotifyTextColor(COLORREF crColor, COLORREF crBkColor);
	VOID _SetCandidateNumberColor(COLORREF crColor, COLORREF crBkColor);
    VOID _SetCandidateTextColor(COLORREF crColor, COLORREF crBkColor);
	VOID _SetCandidateSelectedTextColor(COLORREF crColor, COLORREF crBkColor);
    VOID _SetCandidateFillColor(COLORREF fiColor);
	//VOID _SetCandidateNotifyColor(COLORREF crColor, COLORREF crBkColor);
	BOOL IsCandShown();

	DWORD_PTR _GetSelectedCandidateKeyCode(_Inout_ const WCHAR **ppwchCandidateString);
    DWORD_PTR _GetSelectedCandidateString(_Inout_ const WCHAR **ppwchCandidateString);
    BOOL _SetCandidateSelectionInPage(int nPos) { return _pCandidateWnd->_SetSelectionInPage(nPos); }

    BOOL _MoveCandidateSelection(_In_ int offSet);
	BOOL _SetCandidateSelection(_In_ int selectedIndex, _In_opt_ BOOL isNotify = TRUE);
	INT	 _GetCandidateSelection();
    BOOL _MoveCandidatePage(_In_ int offSet);

    void _MoveUIWindowsToTextExt();

    // CTfTextLayoutSink
    virtual VOID _LayoutChangeNotification(_In_ RECT *lpRect);
    virtual VOID _LayoutDestroyNotification();

    // Event for ITfThreadFocusSink
    virtual HRESULT OnSetThreadFocus();
    virtual HRESULT OnKillThreadFocus();

    void RemoveSpecificCandidateFromList(_In_ LCID Locale, _Inout_ CDIMEArray<CCandidateListItem> &candidateList, _In_ CStringRange &srgCandidateString);
    void AdviseUIChangedByArrowKey(_In_ KEYSTROKE_FUNCTION arrowKey);

	void GetCandLocation(_Out_ POINT *lpPoint) const;

	HRESULT MakeNotifyWindow(_In_ ITfContext *pContextDocument, _In_ CStringRange *pNotifyText =nullptr, _In_ enum NOTIFY_TYPE notifyType = NOTIFY_TYPE::NOTIFY_OTHERS);
	void SetNotifyText(_In_ CStringRange *pNotifyText);
	void ShowNotify(_In_ BOOL showMode, _In_opt_ UINT delayShow = 0, _In_opt_ UINT timeToHide = 0);
	void ClearNotify();
	void ClearAll();
	void ShowNotifyText(_In_ CStringRange *pNotifyText, _In_opt_ UINT delayShow = 0, _In_opt_ UINT timeToHide = 0, _In_opt_ NOTIFY_TYPE notifyType  = NOTIFY_TYPE::NOTIFY_OTHERS);
	BOOL IsNotifyShown();

	BOOL isUILessMode() const {return !_isShowMode;}
private:
	VOID _LayoutChangeNotification(_In_ RECT *lpRect, BOOL firstCall);
    virtual HRESULT CALLBACK _CandidateChangeNotification(_In_ enum CANDWND_ACTION action);
	virtual HRESULT CALLBACK _NotifyChangeNotification(_In_ enum NOTIFY_WND action, _In_ WPARAM wParam, _In_ LPARAM lParam);

    static HRESULT _CandWndCallback(_In_ void *pv, _In_ enum CANDWND_ACTION action);
	static HRESULT _NotifyWndCallback(_In_ void *pv, _In_ enum NOTIFY_WND action, _In_ WPARAM wParam, _In_ LPARAM lParam);

    friend COLORREF _AdjustTextColor(_In_ COLORREF crColor, _In_ COLORREF crBkColor);

    HRESULT _UpdateUIElement();
	

    HRESULT ToShowUIWindows();
    HRESULT ToHideUIWindows();

    HRESULT BeginUIElement();
    HRESULT EndUIElement();

    HRESULT MakeCandidateWindow(_In_ ITfContext *pContextDocument, _In_ UINT wndWidth);
	
    void DisposeCandidateWindow();
	void DisposeNotifyWindow();

    void AddCandidateToUI(_In_ CDIMEArray<CCandidateListItem> *pCandidateList, BOOL isAddFindKeyCode);

    void SetPageIndexWithScrollInfo(_In_ CDIMEArray<CCandidateListItem> *pCandidateList);

protected:
    std::unique_ptr<CCandidateWindow> _pCandidateWnd;
	std::unique_ptr<CNotifyWindow> _pNotifyWnd;
    BOOL _isShowMode;


private:
    HWND _parentWndHandle;
    CCandidateRange* _pIndexRange;
    KEYSTROKE_CATEGORY _Category;
    DWORD _updatedFlags;
    DWORD _uiElementId;
    CDIME* _pTextService;
    LONG _refCount;
	POINT _candLocation;
	POINT _notifyLocation;
	RECT _rectCompRange;

	BOOL _inFocus;
};


//////////////////////////////////////////////////////////////////////
//
// CDIME candidate key handler methods
//
//////////////////////////////////////////////////////////////////////

const int MOVEUP_ONE = -1;
const int MOVEDOWN_ONE = 1;
const int MOVETO_TOP = 0;
const int MOVETO_BOTTOM = -1;
#endif