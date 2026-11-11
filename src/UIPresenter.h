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

#include <array>
#include "KeyHandlerEditSession.h"
#include "CandidateWindow.h"
#include "NotifyWindow.h"
#include "TfTextLayoutSink.h"
#include "DIME.h"
#include "BaseStructure.h"
#include "UIACaretTracker.h"

// The C++ windows and placement state. The Rust product build leaves them out:
// dime-ui::presenterwin owns that state (phase 4b-3). Tests still compile it so
// a presenter with _useRustPresenter off is a real C++ presenter.
#if !defined(DIME_USE_RUST_ENGINE) || defined(DIME_UNIT_TESTING)
#define DIME_CPP_PRESENTER 1
typedef CCandidateWindow DimeCandidateWindow;
typedef CNotifyWindow DimeNotifyWindow;
#endif

#ifdef DIME_UNIT_TESTING
// Forward declaration for integration tests
namespace DIMEIntegratedTests {
    class UIPresenterIntegrationTest;
}
#endif

class CReadingLine;
struct DimePresenter;   // dime_ffi.h: the presenter owned by Rust (phase 4b-2a)
class CCompositionProcessorEngine;
class CDIME;
#ifdef DIME_RUST_AVAILABLE
class CRustSessionHost;   // the key-handler host calls the private TSF pieces (phase 4b-2c)
#endif

// What _LayoutChangeNotification reads from TSF / Win32 before placing the windows
// (DimeLayoutFacts in the Rust build; phase 4b-1, docs/RUST_TSF_SHELL.md).
struct PresenterLayoutFacts
{
    RECT comp;          // *lpRect
    BOOL firstCall;
    BOOL hasParent;     // the context view's window, else the foreground one
    POINT caret;        // GetCaretPos on the screen (with a parent)
};

// What ShowNotifyText reads before placing a notify window (DimeShowFacts).
struct PresenterShowFacts
{
    BOOL refresh;       // a transient slot: reverse lookup, special code, beep
    BOOL hasParent;     // the context view's window
    RECT rcCaret;       // GUITHREADINFO::rcCaret as returned (zero if GetGUIThreadInfo failed)
    POINT guiCaret;     // (rcCaret.left, rcCaret.bottom) on the screen
};

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
#ifdef DIME_UNIT_TESTING
    // Friend declaration for integration tests
    friend class DIMEIntegratedTests::UIPresenterIntegrationTest;
    friend class PresenterLayoutParityAccess;   // tests/PresenterLayoutParityTest.cpp
#endif
#ifdef DIME_RUST_AVAILABLE
    friend class CRustSessionHost;
#endif

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

    virtual HRESULT _StartCandidateList(TfClientId tfClientId, _In_ ITfDocumentMgr *pDocumentMgr, _In_ ITfContext *pContextDocument, TfEditCookie ec, _In_opt_ ITfRange *pRangeComposition, UINT wndWidth);
    void _EndCandidateList();

	void _SetCandidateText(_In_ CDIMEArray<CCandidateListItem> *pCandidateList, _In_ CCandidateRange* pIndexRange, BOOL isAddFindKeyCode, UINT candWidth, bool isPhraseMode = false);
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
    BOOL _SetCandidateSelectionInPage(int nPos) { return _CandSelect(3, nPos, FALSE); }

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

	HRESULT MakeNotifyWindow(_In_ ITfContext *pContextDocument, _In_ CStringRange *pNotifyText =nullptr, _In_ enum NOTIFY_TYPE notifyType = NOTIFY_TYPE::NOTIFY_CHN_ENG);
	void SetNotifyText(_In_ CStringRange *pNotifyText, _In_opt_ NOTIFY_TYPE notifyType = NOTIFY_TYPE::NOTIFY_CHN_ENG);
	void ShowNotify(_In_ BOOL showMode, _In_opt_ UINT delayShow = 0, _In_opt_ UINT timeToHide = 0, _In_opt_ NOTIFY_TYPE notifyType = NOTIFY_TYPE::NOTIFY_CHN_ENG);
	void ClearNotify();
	void ClearAll();
	void ShowNotifyText(_In_ CStringRange *pNotifyText, _In_opt_ UINT delayShow = 0, _In_opt_ UINT timeToHide = 0, _In_opt_ NOTIFY_TYPE notifyType  = NOTIFY_TYPE::NOTIFY_CHN_ENG);
	BOOL IsNotifyShown();
	LONG GetCompRangeHeight() const;
	void ResetCompRange();
#ifdef DIME_RUST_AVAILABLE
	DimePresenter* HostPresenter() const { return _useRustPresenter ? _rp : nullptr; }   // phase 4b-2c
#endif

	BOOL isUILessMode() const {return !_isShowMode;}
private:
	VOID _LayoutChangeNotification(_In_ RECT *lpRect, BOOL firstCall);
	// The placement decisions (dime-ui::presenter in the Rust build, phase 4b-1): the facts are
	// read by the callers; _XxxCpp are the C++ originals.
	void _LayoutChangeApply(const PresenterLayoutFacts& facts);
	void _LayoutChangeApplyCpp(const PresenterLayoutFacts& facts);
	void _ReadLayoutFacts(const RECT& comp, BOOL firstCall, PresenterLayoutFacts& facts);
	BOOL _NotifyLocationForShow(const PresenterShowFacts& facts);      // caretUpdated
	BOOL _NotifyLocationForShowCpp(const PresenterShowFacts& facts);
	void _RestackForShow(BOOL caretUpdated);
	void _RestackForShowCpp(BOOL caretUpdated);
	// The presenter state and notify stack owned by Rust (phase 4b-2a) when _useRustPresenter;
	// else the C++ placement and windows (DIME_CPP_PRESENTER).
	BOOL _RustPresenter() const;
	void _GetPlacement(_Out_opt_ POINT* cand, _Out_opt_ POINT* notify, _Out_opt_ RECT* comp) const;
	void _SetPlacement(POINT cand, POINT notify, RECT comp);   // tests
	BOOL _NotifySlotExists(NOTIFY_TYPE notifyType) const;
	HWND _NotifySlotWnd(NOTIFY_TYPE notifyType, _Out_opt_ UINT* width = nullptr, _Out_opt_ UINT* height = nullptr) const;   // tests
	static void _NotifyWndInvoke(void* ctx, int32_t action, uintptr_t wParam, intptr_t lParam);
	// The candidate window (phase 4b-2b): the Rust presenter's, or the C++ window.
	BOOL _HasCand() const;
	BOOL _CandVisible() const;   // and its window is visible
	void _CandShow(BOOL show);
	void _CandOp(UINT op, int a = 0, int b = 0);
	BOOL _CandSelect(UINT op, int a, BOOL notify);
	HWND _CandWnd() const;
public:
	static void _CandWndCallbackForRust(void* ctx, int32_t action);   // the Rust candidate window's callback
private:
#ifdef DIME_UNIT_TESTING
	void _PaintNotifySlotForTest(NOTIFY_TYPE notifyType, HDC dc, PAINTSTRUCT* ps);   // as WM_PAINT
	void _PaintCandidateForTest(HDC dc, PAINTSTRUCT* ps);
	BOOL _CandScrollInfoForTest(int* info);   // the scroll bar's nMax, nPage, nPos
	// The classes the Rust presenter makes its notify windows with (default: Global::Atom*; the C++
	// test build registers those with CBaseWindow's window procedure).
	void _SetNotifyClassesForTest(ATOM notify, ATOM shadow) { _notifyAtomForTest = notify; _notifyShadowAtomForTest = shadow; }
#ifdef DIME_RUST_AVAILABLE
	void _SetCandidateClassesForTest(const DimeCandClasses& classes) { _candClassesForTest = classes; }
#endif
#endif
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
	void DisposeNotifyWindow(_In_opt_ NOTIFY_TYPE notifyType = NOTIFY_TYPE::NOTIFY_COUNT);  // NOTIFY_COUNT = dispose all slots
	// Stack-layout helpers (see docs/MULTI_NOTIFY.md).
	BOOL _AnyHintSlotVisible() const;
	BOOL _DecideStackGrowUp() const;
	POINT _ComputeStackPosForNewSlot(_In_ NOTIFY_TYPE newSlot, _In_ BOOL growUp) const;
	void _RestackVisibleSlots(_In_ BOOL growUp);

    void AddCandidateToUI(_In_ CDIMEArray<CCandidateListItem> *pCandidateList, BOOL isAddFindKeyCode);

    void SetPageIndexWithScrollInfo(_In_ CDIMEArray<CCandidateListItem> *pCandidateList);

protected:
#ifdef DIME_CPP_PRESENTER
    std::unique_ptr<DimeCandidateWindow> _pCandidateWnd;
	// One notify window per NOTIFY_TYPE. Lazily instantiated by MakeNotifyWindow.
	// Concurrency invariant (see docs/MULTI_NOTIFY.md): at most NOTIFY_REVERSE_LOOKUP
	// and NOTIFY_SPECIAL_CODE coexist, both wiped together by ClearNotify on next
	// keystroke; CHN_ENG cannot coexist with hint slots in practice. Future hints
	// with timeToHide>0 would need restack-on-hide — not implemented here.
	std::array<std::unique_ptr<DimeNotifyWindow>, (size_t)NOTIFY_TYPE::NOTIFY_COUNT> _pNotifyWnds;
#endif
    BOOL _isShowMode;


private:
    HWND _parentWndHandle;
    CCandidateRange* _pIndexRange;
    KEYSTROKE_CATEGORY _Category;
    DWORD _updatedFlags;
    DWORD _uiElementId;
    CDIME* _pTextService;
    LONG _refCount;
#ifdef DIME_CPP_PRESENTER
	POINT _candLocation;
	POINT _notifyLocation;
	RECT _rectCompRange;
#endif
	CUIACaretTracker _uiaCaretTracker;

	BOOL _inFocus;
	BOOL _useRustPresenter;   // the presenter state, notify stack and placement decisions in Rust (DIME_USE_RUST_ENGINE) or C++
	DimePresenter* _rp;       // with DIME_RUST_AVAILABLE (else null)
#ifdef DIME_UNIT_TESTING
	ATOM _notifyAtomForTest = 0, _notifyShadowAtomForTest = 0;
#ifdef DIME_RUST_AVAILABLE
	DimeCandClasses _candClassesForTest = {};
#endif
#endif
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