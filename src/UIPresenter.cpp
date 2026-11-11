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
//May need to disable when debugging
#define CANCEL_CAND_ON_KILL_FOCUS

#include "Private.h"
#include "UIConstants.h"
#include "CandidateWindow.h"
#include "ShadowWindow.h"
#include "UIPresenter.h"
#include "CompositionProcessorEngine.h"
#include "BaseStructure.h"
#include <memory> // Include for smart pointers
#ifdef DIME_RUST_AVAILABLE
#include "dime_ffi.h"

// The environment of the notify windows the Rust presenter makes.
namespace PresenterEnv
{
    uint32_t Dpi(void* hwnd) { return CConfig::GetDpiForHwnd((HWND)hwnd); }
    int32_t Dark() { return CConfig::GetEffectiveDarkMode() ? 1 : 0; }
    int32_t AlwaysShow() { return CConfig::GetAlwaysShowScrollbars() ? 1 : 0; }
    void* Font() { return Global::defaultlFontHandle; }
    void SetFont(void* hwnd) { CConfig::SetDefaultTextFont((HWND)hwnd); }
}

// The candidate window's side of the Rust presenter (phase 4b-2b): its callback and the
// selection keys, read live as CCandidateWindow read them.
namespace PresenterCand
{
    void Invoke(void* ctx, int32_t action)
    {
        CUIPresenter::_CandWndCallbackForRust(ctx, action);
    }
    int32_t RangeCount(const void* range) { return range ? ((const CCandidateRange*)range)->Count() : -1; }
    uint16_t RangeChar(const void* range, int32_t row)
    {
        CCandidateRange* r = (CCandidateRange*)range;
        // GetAt throws out of range, and a callback must not throw.
        if (!r || row < 0 || row >= r->Count()) return 0;
        return (uint16_t)r->GetAt(row)->CandIndex;
    }
}
#endif

//////////////////////////////////////////////////////////////////////
//
// UIPresenter class
//
//////////////////////////////////////////////////////////////////////+---------------------------------------------------------------------------
//
// ctor
//
//----------------------------------------------------------------------------

CUIPresenter::CUIPresenter(_In_ CDIME *pTextService, CCompositionProcessorEngine *pCompositionProcessorEngine)
    : CTfTextLayoutSink(pTextService), _Category(KEYSTROKE_CATEGORY::CATEGORY_NONE), _pIndexRange(nullptr)
{
    debugPrint(L"CUIPresenter::CUIPresenter() constructor");
    _pTextService = pTextService;
    if (_pTextService)
        _pTextService->AddRef();

    if (pCompositionProcessorEngine)
        _pIndexRange = pCompositionProcessorEngine->GetCandidateListIndexRange();

    _parentWndHandle = nullptr;
    _updatedFlags = 0;
    _uiElementId = (DWORD)-1;
    _isShowMode = TRUE;
    _refCount = 1;

    // Initialize locations off-screen (the C++ presenter; Rust keeps its own).
#ifdef DIME_CPP_PRESENTER
    _candLocation = { UI::DEFAULT_WINDOW_X, UI::DEFAULT_WINDOW_Y };
    _notifyLocation = { UI::DEFAULT_WINDOW_X, UI::DEFAULT_WINDOW_Y };
    _rectCompRange = { UI::DEFAULT_WINDOW_X, UI::DEFAULT_WINDOW_Y, UI::DEFAULT_WINDOW_X, UI::DEFAULT_WINDOW_Y };
#endif
    _inFocus = FALSE;
#ifdef DIME_USE_RUST_ENGINE
    _useRustPresenter = TRUE;
#else
    _useRustPresenter = FALSE;
#endif
    _rp = nullptr;
#ifdef DIME_RUST_AVAILABLE
    static const DimeCandEnv env = { PresenterEnv::Dpi, PresenterEnv::Dark, PresenterEnv::AlwaysShow, PresenterEnv::Font, PresenterEnv::SetFont };
    const DimeNotifyCallback callback = { this, _NotifyWndInvoke };
    const DimePresenterCandSource source = { this, PresenterCand::Invoke, PresenterCand::RangeCount, PresenterCand::RangeChar };
    _rp = dime_ui_presenter_create(&env, &callback, &source);
#endif
}

//+---------------------------------------------------------------------------
//
// dtor
//
//----------------------------------------------------------------------------

CUIPresenter::~CUIPresenter()
{
    debugPrint(L"CUIPresenter::~CUIPresenter() destructor");
    _EndCandidateList();
    DisposeNotifyWindow();
#ifdef DIME_RUST_AVAILABLE
    dime_ui_presenter_free(_rp);
    _rp = nullptr;
#endif
    if (_pTextService)
    {
        _pTextService->Release();
        _pTextService = nullptr;
    }
}

//+---------------------------------------------------------------------------
//
// ITfCandidateListUIElement::IUnknown::QueryInterface
//
//----------------------------------------------------------------------------

STDAPI CUIPresenter::QueryInterface(REFIID riid, _Outptr_ void **ppvObj)
{
    if (CTfTextLayoutSink::QueryInterface(riid, ppvObj) == S_OK)
    {
        return S_OK;
    }

    if (ppvObj == nullptr)
    {
        return E_INVALIDARG;
    }

    *ppvObj = nullptr;

    if (IsEqualIID(riid, IID_ITfUIElement) ||
        IsEqualIID(riid, IID_ITfCandidateListUIElement))
    {
        *ppvObj = (ITfCandidateListUIElement*)this;
    }
    else if (IsEqualIID(riid, IID_IUnknown) || 
        IsEqualIID(riid, IID_ITfCandidateListUIElementBehavior)) 
    {
        *ppvObj = (ITfCandidateListUIElementBehavior*)this;
    }
    else if (IsEqualIID(riid, __uuidof(ITfIntegratableCandidateListUIElement))) 
    {
        *ppvObj = (ITfIntegratableCandidateListUIElement*)this;
    }

    if (*ppvObj)
    {
        AddRef();
        return S_OK;
    }

    return E_NOINTERFACE;
}

//+---------------------------------------------------------------------------
//
// ITfCandidateListUIElement::IUnknown::AddRef
//
//----------------------------------------------------------------------------

STDAPI_(ULONG) CUIPresenter::AddRef()
{
    // Increment both refcounts to maintain consistency
    // Parent class manages ITfTextLayoutSink refcount
    // This class manages overall COM object refcount
    CTfTextLayoutSink::AddRef();
    return ++_refCount;
}

//+---------------------------------------------------------------------------
//
// ITfCandidateListUIElement::IUnknown::Release
//
//----------------------------------------------------------------------------

STDAPI_(ULONG) CUIPresenter::Release()
{
    // Must decrement our refcount first and check for zero
    // before calling parent Release, because parent Release
    // may delete the object if its refcount hits zero
    LONG cr = --_refCount;

    assert(_refCount >= 0);

    if (_refCount == 0)
    {
        delete this;  // Destructor calls parent destructor which will handle parent refcount
        return 0;
    }

    // Still alive, so also decrement parent refcount
    CTfTextLayoutSink::Release();

    return cr;
}

//+---------------------------------------------------------------------------
//
// ITfCandidateListUIElement::ITfUIElement::GetDescription
//
//----------------------------------------------------------------------------

STDAPI CUIPresenter::GetDescription(BSTR *pbstr)
{
    if (pbstr)
    {
        *pbstr = SysAllocString(L"Cand");
    }
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfCandidateListUIElement::ITfUIElement::GetGUID
//
//----------------------------------------------------------------------------

STDAPI CUIPresenter::GetGUID(GUID *pguid)
{
    *pguid = Global::DIMEGuidCandUIElement;
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfDIMEUIElement::ITfUIElement::Show
//
//----------------------------------------------------------------------------

STDAPI CUIPresenter::Show(BOOL showWindow)
{
	debugPrint(L"CUIPresenter::Show(), showWindow=%d", showWindow);
    if (showWindow)
    {
        ToShowUIWindows();
    }
    else
    {
        ToHideUIWindows();
    }
    return S_OK;
}

HRESULT CUIPresenter::ToShowUIWindows()
{
	debugPrint(L"CUIPresenter::ToShowUIWindows()");
    _MoveUIWindowsToTextExt();
    _CandShow(TRUE);
	// Re-show any compositional-hint slot that was created but is currently hidden.
	// Mode-state slot (NOTIFY_CHN_ENG) is excluded — its visibility is driven by
	// its own delay-/auto-hide timers, not by candidate-window UI cycling.
	for (size_t i = 0; i < (size_t)NOTIFY_TYPE::NOTIFY_COUNT; ++i)
	{
		if (i == (size_t)NOTIFY_TYPE::NOTIFY_CHN_ENG) continue;
		ShowNotify(TRUE, 0, 0, (NOTIFY_TYPE)i);   // nothing for a missing slot
	}


    return S_OK;
}

HRESULT CUIPresenter::ToHideUIWindows()
{
	debugPrint(L"CUIPresenter::ToHideUIWindows()");
	_CandShow(FALSE);
	ClearNotify();

    _updatedFlags = TF_CLUIE_SELECTION | TF_CLUIE_CURRENTPAGE;
    _UpdateUIElement();

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfCandidateListUIElement::ITfUIElement::IsShown
//
//----------------------------------------------------------------------------

STDAPI CUIPresenter::IsShown(BOOL *pIsShow)
{
	if(_HasCand())
		*pIsShow = _CandVisible();
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfCandidateListUIElement::GetUpdatedFlags
//
//----------------------------------------------------------------------------

STDAPI CUIPresenter::GetUpdatedFlags(DWORD *pdwFlags)
{
	debugPrint(L"CUIPresenter::GetUpdatedFlags(), _updatedFlags = %x", _updatedFlags);
    *pdwFlags = _updatedFlags;
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfCandidateListUIElement::GetDocumentMgr
//
//----------------------------------------------------------------------------

STDAPI CUIPresenter::GetDocumentMgr(ITfDocumentMgr **ppdim)
{
	debugPrint(L"CUIPresenter::GetDocumentMgr()");
    *ppdim = nullptr;

    return E_NOTIMPL;
}

//+---------------------------------------------------------------------------
//
// ITfCandidateListUIElement::GetCount
//
//----------------------------------------------------------------------------

STDAPI CUIPresenter::GetCount(UINT *pCandidateCount)
{
#ifdef DIME_RUST_AVAILABLE
    if (_RustPresenter())
        *pCandidateCount = _HasCand() ? (UINT)dime_ui_presenter_cand_query(_rp, 3) : 0;
    else
#endif
#ifdef DIME_CPP_PRESENTER
    if (_pCandidateWnd)
    {
        *pCandidateCount = _pCandidateWnd->_GetCount();
    }
    else
#endif
    {
        *pCandidateCount = 0;
    }
	debugPrint(L"CUIPresenter::GetCount(), *pCandidateCount = %d", *pCandidateCount);
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfCandidateListUIElement::GetSelection
//
//----------------------------------------------------------------------------

STDAPI CUIPresenter::GetSelection(UINT *pSelectedCandidateIndex)
{
    if (_HasCand())
    {
		INT selection = _GetCandidateSelection();
		if(selection >= 0 )
			*pSelectedCandidateIndex = selection;
		else 
			*pSelectedCandidateIndex = 0;
    }
    else
    {
        *pSelectedCandidateIndex = 0;
    }
	debugPrint(L"CUIPresenter::GetSelection(), *pSelectedCandidateIndex = %d", *pSelectedCandidateIndex);
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfCandidateListUIElement::GetString
//
//----------------------------------------------------------------------------

STDAPI CUIPresenter::GetString(UINT uIndex, BSTR *pbstr)
{
    UINT count = 0;
    GetCount(&count);
    if (!_HasCand() || (uIndex > count))
    {
        return E_FAIL;
    }

    DWORD candidateLen = 0;
    const WCHAR* pCandidateString = nullptr;

#ifdef DIME_RUST_AVAILABLE
    if (_RustPresenter())
        candidateLen = dime_ui_presenter_cand_string(_rp, 0, (int32_t)uIndex, (const uint16_t**)&pCandidateString);
    else
#endif
#ifdef DIME_CPP_PRESENTER
    candidateLen = _pCandidateWnd->_GetCandidateString(uIndex, &pCandidateString);
#else
    candidateLen = 0;
#endif

    *pbstr = (candidateLen == 0) ? nullptr : SysAllocStringLen(pCandidateString, candidateLen);
	//debugPrint(L"CUIPresenter::GetString(), uIndex = %d, pbstr = %s", uIndex, pbstr);  
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfCandidateListUIElement::GetPageIndex
//
//----------------------------------------------------------------------------

STDAPI CUIPresenter::GetPageIndex(UINT *pIndex, UINT uSize, UINT *puPageCnt)
{
	debugPrint(L"CUIPresenter::GetPageIndex()");
    if (!_HasCand())
    {
        if (pIndex)
        {
            *pIndex = 0;
        }
        *puPageCnt = 0;
        return S_OK;
    }
#ifdef DIME_RUST_AVAILABLE
    if (_RustPresenter())
        return dime_ui_presenter_cand_get_page_index(_rp, pIndex, uSize, puPageCnt);
#endif
#ifdef DIME_CPP_PRESENTER
    return _pCandidateWnd->_GetPageIndex(pIndex, uSize, puPageCnt);
#else
    return E_FAIL;
#endif
}

//+---------------------------------------------------------------------------
//
// ITfCandidateListUIElement::SetPageIndex
//
//----------------------------------------------------------------------------

STDAPI CUIPresenter::SetPageIndex(UINT *pIndex, UINT uPageCnt)
{
	debugPrint(L"CUIPresenter::SetPageIndex(), index = %d, page count =%d", *pIndex, uPageCnt  );
    if (!_HasCand())
    {
        return E_FAIL;
    }
#ifdef DIME_RUST_AVAILABLE
    if (_RustPresenter())
    {
        dime_ui_presenter_cand_set_page_index(_rp, pIndex, uPageCnt);
        return S_OK;
    }
#endif
#ifdef DIME_CPP_PRESENTER
    return _pCandidateWnd->_SetPageIndex(pIndex, uPageCnt);
#else
    return E_FAIL;
#endif
}

//+---------------------------------------------------------------------------
//
// ITfCandidateListUIElement::GetCurrentPage
//
//----------------------------------------------------------------------------

STDAPI CUIPresenter::GetCurrentPage(UINT *puPage)
{
	debugPrint(L"CUIPresenter::GetCurrentPage()");
    if (!_HasCand())
    {
        *puPage = 0;
        return S_OK;
    }
#ifdef DIME_RUST_AVAILABLE
    if (_RustPresenter())
    {
        if (puPage == nullptr) return E_INVALIDARG;
        return dime_ui_presenter_cand_current_page(_rp, puPage);
    }
#endif
#ifdef DIME_CPP_PRESENTER
    return _pCandidateWnd->_GetCurrentPage(puPage);
#else
    return E_FAIL;
#endif
}

//+---------------------------------------------------------------------------
//
// ITfCandidateListUIElementBehavior::SetSelection
// It is related of the mouse clicking behavior upon the suggestion window
//----------------------------------------------------------------------------

STDAPI CUIPresenter::SetSelection(UINT nIndex)
{
	debugPrint(L"CUIPresenter::SetSelection(), nIndex =%d", nIndex);
#ifdef DIME_RUST_AVAILABLE
    if (_RustPresenter())
    {
        dime_ui_presenter_cand_select(_rp, 4, (int32_t)nIndex, 0);
        return S_OK;
    }
#endif
#ifdef DIME_CPP_PRESENTER
    if (_pCandidateWnd)
    {
        _pCandidateWnd->_SetSelection(nIndex);
    }
#endif

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfCandidateListUIElementBehavior::Finalize
// It is related of the mouse clicking behavior upon the suggestion window
//----------------------------------------------------------------------------

STDAPI CUIPresenter::Finalize(void)
{
	debugPrint(L"CUIPresenter::Finalize()");
    _CandidateChangeNotification(CANDWND_ACTION::CAND_ITEM_SELECT);
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfCandidateListUIElementBehavior::Abort
//
//----------------------------------------------------------------------------

STDAPI CUIPresenter::Abort(void)
{
	debugPrint(L"CUIPresenter::Abort()");
    return E_NOTIMPL;
}

//+---------------------------------------------------------------------------
//
// ITfCandidateListUIElementBehavior::SetIntegrationStyle
// To show candidateNumbers on the suggestion window
//----------------------------------------------------------------------------

STDAPI CUIPresenter::SetIntegrationStyle(GUID guidIntegrationStyle)
{
	debugPrint(L"CUIPresenter::SetIntegrationStyle() ok? = %d", (guidIntegrationStyle == GUID_INTEGRATIONSTYLE_SEARCHBOX));
    return (guidIntegrationStyle == GUID_INTEGRATIONSTYLE_SEARCHBOX) ? S_OK : E_NOTIMPL;
}

//+---------------------------------------------------------------------------
//
// ITfIntegratableCandidateListUIElement::GetSelectionStyle
//
//----------------------------------------------------------------------------

STDAPI CUIPresenter::GetSelectionStyle(_Out_ TfIntegratableCandidateListSelectionStyle *ptfSelectionStyle)
{
	debugPrint(L"CUIPresenter::GetSelectionStyle()");
    *ptfSelectionStyle = STYLE_ACTIVE_SELECTION;
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfIntegratableCandidateListUIElement::OnKeyDown
//
//----------------------------------------------------------------------------

STDAPI CUIPresenter::OnKeyDown(_In_ WPARAM wParam, _In_ LPARAM lParam, _Out_ BOOL *pIsEaten)
{
	debugPrint(L"CUIPresenter::OnKeyDown() wParam=%x, lpwaram=%x", wParam, lParam);
    wParam;
    lParam;

    *pIsEaten = TRUE;
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfIntegratableCandidateListUIElement::ShowCandidateNumbers
//
//----------------------------------------------------------------------------

STDAPI CUIPresenter::ShowCandidateNumbers(_Out_ BOOL *pIsShow)
{
	debugPrint(L"CUIPresenter::ShowCandidateNumbers()");
    *pIsShow = TRUE;
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfIntegratableCandidateListUIElement::FinalizeExactCompositionString
//
//----------------------------------------------------------------------------

STDAPI CUIPresenter::FinalizeExactCompositionString()
{
	debugPrint(L"CUIPresenter::FinalizeExactCompositionString()");
    return E_NOTIMPL;
}


//+---------------------------------------------------------------------------
//
// _StartCandidateList
//
//----------------------------------------------------------------------------

HRESULT CUIPresenter::_StartCandidateList(TfClientId tfClientId, _In_ ITfDocumentMgr *pDocumentMgr, _In_ ITfContext *pContextDocument, TfEditCookie ec, _In_opt_ ITfRange *pRangeComposition, UINT wndWidth)
{
	debugPrint(L"\nCUIPresenter::_StartCandidateList()");
	pDocumentMgr;tfClientId;
    HRESULT hr = E_FAIL;
	CStringRange notify;
	RECT rcTextExt = { 0,0,0,0 };

    if (FAILED(_StartLayout(pContextDocument, ec, pRangeComposition)))
    {
        goto Exit;
    }

    BeginUIElement();


    if (_HasCand())
    {
        // Candidate window already exists (e.g. reusing for phrase candidates after
        // commit — _ClearCandidateList + _SetCandidateText repopulated it). Skip
        // creation; MakeCandidateWindow returns E_FAIL when _pCandidateWnd != nullptr,
        // which would tear down the just-populated window via the Exit branch.
        // (Regression from f898dd6 that reverted a97d654's guard. See debug log
        //  pattern: phrase list shows briefly then _EndCandidateList fires with
        //  _StartCandidateList returning E_FAIL.)
        hr = S_OK;
    }
    else
    {
        hr = MakeCandidateWindow(pContextDocument, wndWidth);
        if (FAILED(hr))
        {
            goto Exit;
        }
    }

	Show(_isShowMode);


    if (SUCCEEDED(_GetTextExt(&rcTextExt)))
    {
        _LayoutChangeNotification(&rcTextExt);
    }
    else
    {
        // No composition range (e.g. phrase candidates after commit).
        // Reuse the last saved composition rect — the caret hasn't moved.
        RECT comp = {};
        _GetPlacement(nullptr, nullptr, &comp);
        _LayoutChangeNotification(&comp, TRUE);
    }

Exit:
    if (FAILED(hr))
    {
        _EndCandidateList();
    }
	debugPrint(L"CUIPresenter::_StartCandidateList(), hresult = %d\n", hr);
    return hr;
}

//+---------------------------------------------------------------------------
//
// _EndCandidateList
//
//----------------------------------------------------------------------------

void CUIPresenter::_EndCandidateList()
{
	debugPrint(L"CUIPresenter::_EndCandidateList()");
    
	EndUIElement();
	_ClearCandidateList();
	//ClearNotify(); //should not clear notify to preserve array special code notify
	_UpdateUIElement();

    DisposeCandidateWindow();

    _EndLayout();

}

//+---------------------------------------------------------------------------
//
// _SetText
//
//----------------------------------------------------------------------------

void CUIPresenter::_SetCandidateText(_In_ CDIMEArray<CCandidateListItem> *pCandidateList,_In_ CCandidateRange* pIndexRange, BOOL isAddFindKeyCode, UINT candWidth, bool isPhraseMode)
{
	debugPrint(L"CUIPresenter::_SetCandidateText() candWidth = %d", candWidth);
	CConfig::ApplyIMEColorSet(this, isPhraseMode);

#ifdef DEBUG_PRINT
	for (UINT i = 0; i < pCandidateList->Count(); i++)
	{
		WCHAR key[255], value[255];
		StringCchCopyNW(key, 255, pCandidateList->GetAt(i)->_ItemString.Get(), pCandidateList->GetAt(i)->_ItemString.GetLength());
		StringCchCopyNW(value, 255, pCandidateList->GetAt(i)->_FindKeyCode.Get(), pCandidateList->GetAt(i)->_FindKeyCode.GetLength());
		debugPrint(L"Cand item %d:%s:%s", i, key, value);
	}
#endif

    AddCandidateToUI(pCandidateList, isAddFindKeyCode);

	_pIndexRange = pIndexRange;

	
    SetPageIndexWithScrollInfo(pCandidateList);

	_CandOp(4, (int)candWidth);   // _SetCandStringLength

	Show(_isShowMode);
    if (_isShowMode && _HasCand())
    {
		_CandOp(3);   // _InvalidateRect
    }
    else
    {
        _updatedFlags = TF_CLUIE_COUNT       |
            TF_CLUIE_SELECTION   |
            TF_CLUIE_STRING      |
            TF_CLUIE_PAGEINDEX   |
            TF_CLUIE_CURRENTPAGE;
        _UpdateUIElement();
    }
}

void CUIPresenter::AddCandidateToUI(_In_ CDIMEArray<CCandidateListItem> *pCandidateList, BOOL isAddFindKeyCode)
{

	debugPrint(L"UIPresenter:AddCandidateToUI()");
	if(pCandidateList == nullptr || !_HasCand()) return;
#ifdef DIME_RUST_AVAILABLE
	if (_RustPresenter())
	{
		std::vector<DimeCandItem> items(pCandidateList->Count());
		for (UINT index = 0; index < pCandidateList->Count(); index++)
		{
			CCandidateListItem* it = pCandidateList->GetAt(index);
			items[index] = { (const uint16_t*)it->_ItemString.Get(), it->_ItemString.GetLength(),
			                 (const uint16_t*)it->_FindKeyCode.Get(), it->_FindKeyCode.GetLength() };
		}
		dime_ui_presenter_cand_add_items(_rp, items.data(), items.size(), isAddFindKeyCode ? 1 : 0);
		return;
	}
#endif
#ifdef DIME_CPP_PRESENTER
    for (UINT index = 0; index < pCandidateList->Count(); index++)
    {
		_pCandidateWnd->_AddString(pCandidateList->GetAt(index), isAddFindKeyCode);
    }
#endif

}

void CUIPresenter::SetPageIndexWithScrollInfo(_In_ CDIMEArray<CCandidateListItem> *pCandidateList)
{
    if (!_pIndexRange || !pCandidateList || !_HasCand())
        return;

#ifdef DIME_RUST_AVAILABLE
    if (_RustPresenter())
        dime_ui_presenter_cand_set_index_range(_rp, _pIndexRange);
    else
#endif
#ifdef DIME_CPP_PRESENTER
    _pCandidateWnd->_SetCandIndexRange(_pIndexRange);
#else
    return;
#endif

    UINT candCntInPage = _pIndexRange->Count();
    UINT bufferSize = (pCandidateList->Count() + candCntInPage - 1) / candCntInPage;
    std::vector<UINT> pageIndex(bufferSize);

    for (UINT i = 0; i < bufferSize; i++)
    {
        pageIndex[i] = i * candCntInPage;
    }

#ifdef DIME_RUST_AVAILABLE
    if (_RustPresenter())
    {
        dime_ui_presenter_cand_set_page_index(_rp, pageIndex.data(), bufferSize);
        _CandOp(9, (int)pCandidateList->Count(), (int)candCntInPage);   // _SetScrollInfo
        return;
    }
#endif
#ifdef DIME_CPP_PRESENTER
    _pCandidateWnd->_SetPageIndex(pageIndex.data(), bufferSize);
    _pCandidateWnd->_SetScrollInfo(pCandidateList->Count(), candCntInPage);
#endif
}

//+---------------------------------------------------------------------------
//
// _ClearList
//
//----------------------------------------------------------------------------

void CUIPresenter::_ClearCandidateList()
{
#ifdef DIME_RUST_AVAILABLE
	if (_RustPresenter())
	{
		dime_ui_presenter_cand_clear(_rp);
		_CandOp(3);   // _InvalidateRect
		return;
	}
#endif
#ifdef DIME_CPP_PRESENTER
	if(_pCandidateWnd)
	{
		_pCandidateWnd->_ClearList();
		_pCandidateWnd->_InvalidateRect();
	}
#endif
}

//+---------------------------------------------------------------------------
//
// _SetTextColor
// _SetFillColor
//
//----------------------------------------------------------------------------
void CUIPresenter::_SetNotifyTextColor(COLORREF crColor, COLORREF crBkColor)
{
#ifdef DIME_RUST_AVAILABLE
	if (_RustPresenter())
	{
		dime_ui_presenter_notify_set_text_color(_rp, crColor, crBkColor);
		return;
	}
#endif
#ifdef DIME_CPP_PRESENTER
	for (auto& w : _pNotifyWnds)
	{
		if (!w) continue;
		w->_SetTextColor(crColor, crBkColor);
		w->_SetFillColor(crBkColor);
		w->_InvalidateRect();
	}
#endif
}


void CUIPresenter::_SetCandidateNumberColor(COLORREF crColor, COLORREF crBkColor)
{
#ifdef DIME_RUST_AVAILABLE
	if (_RustPresenter())
	{
		dime_ui_presenter_cand_set_color(_rp, 0, crColor, crBkColor);
		return;
	}
#endif
#ifdef DIME_CPP_PRESENTER
	if(_pCandidateWnd)
		_pCandidateWnd->_SetNumberColor(crColor, crBkColor);
#endif
}


void CUIPresenter::_SetCandidateTextColor(COLORREF crColor, COLORREF crBkColor)
{
#ifdef DIME_RUST_AVAILABLE
	if (_RustPresenter())
	{
		dime_ui_presenter_cand_set_color(_rp, 1, crColor, crBkColor);
		return;
	}
#endif
#ifdef DIME_CPP_PRESENTER
	if(_pCandidateWnd)
		_pCandidateWnd->_SetTextColor(crColor, crBkColor);
#endif
}

void CUIPresenter::_SetCandidateSelectedTextColor(COLORREF crColor, COLORREF crBkColor)
{
#ifdef DIME_RUST_AVAILABLE
	if (_RustPresenter())
	{
		dime_ui_presenter_cand_set_color(_rp, 2, crColor, crBkColor);
		return;
	}
#endif
#ifdef DIME_CPP_PRESENTER
	if(_pCandidateWnd)
		_pCandidateWnd->_SetSelectedTextColor(crColor, crBkColor);
#endif
}


void CUIPresenter::_SetCandidateFillColor(COLORREF fiColor)
{
	_CandOp(5, (int)fiColor);   // _SetFillColor
}


// +-------------------------------------------------------------------------- -
//
// _GetSelectedCandidateKeyCode
//
//----------------------------------------------------------------------------

DWORD_PTR CUIPresenter::_GetSelectedCandidateKeyCode(_Inout_ const WCHAR **ppwchCandidateString)
{
#ifdef DIME_RUST_AVAILABLE
	if (_RustPresenter())
		return _HasCand() ? dime_ui_presenter_cand_string(_rp, 2, 0, (const uint16_t**)ppwchCandidateString) : 0;
#endif
#ifdef DIME_CPP_PRESENTER
	if (_pCandidateWnd)
		return _pCandidateWnd->_GetSelectedCandidateKeyCode(ppwchCandidateString);
#endif
	return 0;
}


//+---------------------------------------------------------------------------
//
// _GetSelectedCandidateString
//
//----------------------------------------------------------------------------

DWORD_PTR CUIPresenter::_GetSelectedCandidateString(_Inout_ const WCHAR **ppwchCandidateString)
{
#ifdef DIME_RUST_AVAILABLE
	if (_RustPresenter())
		return _HasCand() ? dime_ui_presenter_cand_string(_rp, 1, 0, (const uint16_t**)ppwchCandidateString) : 0;
#endif
#ifdef DIME_CPP_PRESENTER
	if(_pCandidateWnd)
		return _pCandidateWnd->_GetSelectedCandidateString(ppwchCandidateString);
#endif
	return 0;
}

//+---------------------------------------------------------------------------
//
// _MoveSelection
//
//----------------------------------------------------------------------------

BOOL CUIPresenter::_MoveCandidateSelection(_In_ int offSet)
{
	if(!_HasCand()) return FALSE;
    BOOL ret = _CandSelect(0, offSet, TRUE);
    if (ret)
    {
        if (_isShowMode)
        {
            _CandOp(3);
        }
        else
        {
            _updatedFlags = TF_CLUIE_SELECTION;
            _UpdateUIElement();
        }
    }
    return ret;
}

//+---------------------------------------------------------------------------
//
// _SetCandidateSelection
//
//----------------------------------------------------------------------------

BOOL CUIPresenter::_SetCandidateSelection(_In_ int selectedIndex, _In_opt_ BOOL isNotify)
{
	debugPrint(L"CUIPresenter::_SetCandidateSelection(), selectedIndex = %d, isNotify = %d", selectedIndex, isNotify);
	if(!_HasCand()) return FALSE;
    BOOL ret = _CandSelect(1, selectedIndex, isNotify);
    if (ret)
    {
        if (_isShowMode)
        {
            _CandOp(3);
        }
        else
        {
            _updatedFlags = TF_CLUIE_SELECTION |
                TF_CLUIE_CURRENTPAGE;
            _UpdateUIElement();
        }
    }
    return ret;
}
//+---------------------------------------------------------------------------
//
// _GetCandidateSelection
//
//----------------------------------------------------------------------------
INT CUIPresenter::_GetCandidateSelection()
{	
#ifdef DIME_RUST_AVAILABLE
	if (_RustPresenter())
		return dime_ui_presenter_cand_query(_rp, 4);
#endif
#ifdef DIME_CPP_PRESENTER
	if (_pCandidateWnd)
		return _pCandidateWnd->_GetSelection();
#endif
	return -1;
}

//+---------------------------------------------------------------------------
//
// _MovePage
//
//----------------------------------------------------------------------------

BOOL CUIPresenter::_MoveCandidatePage(_In_ int offSet)
{
	debugPrint(L"CUIPresenter::_MoveCandidatePage(), offSet = %d", offSet);
	if(!_HasCand()) return FALSE;
    BOOL ret = _CandSelect(2, offSet, TRUE);
    if (ret)
    {
        if (_isShowMode)
        {
            _CandOp(3);
        }
        else
        {
            _updatedFlags = TF_CLUIE_SELECTION |
                TF_CLUIE_CURRENTPAGE;
            _UpdateUIElement();
        }
    }
    return ret;
}

//+---------------------------------------------------------------------------
//
// _MoveWindowToTextExt
//
//----------------------------------------------------------------------------

void CUIPresenter::_MoveUIWindowsToTextExt()
{
	debugPrint(L"CUIPresenter::_MoveUIWindowToTextExt()");
	RECT compRect = { 0,0,0,0 };

	if (SUCCEEDED(_GetTextExt(&compRect)))
	{
		debugPrint(L"CUIPresenter::_MoveUIWindowToTextExt(), top = %d, bottom = %d, right = %d, left = %d", compRect.top, compRect.bottom, compRect.right, compRect.left);
		_LayoutChangeNotification(&compRect, true);
	}
	else
	{
		RECT comp = {};
		_GetPlacement(nullptr, nullptr, &comp);
		_LayoutChangeNotification(&comp, true);
	}

	
	
}
//+---------------------------------------------------------------------------
//
// _LayoutChangeNotification
//
//----------------------------------------------------------------------------

VOID CUIPresenter::_LayoutChangeNotification(_In_ RECT *lpRect)
{
	_LayoutChangeNotification(lpRect, false);
}
VOID CUIPresenter::_LayoutChangeNotification(_In_ RECT *lpRect, BOOL firstCall)
{
	if(lpRect == nullptr) return;
	debugPrint(L"CUIPresenter::_LayoutChangeNotification(), top = %d, bottom = %d, right = %d, left = %d", lpRect->top, lpRect->bottom, lpRect->right, lpRect->left);
	PresenterLayoutFacts facts;
	_ReadLayoutFacts(*lpRect, firstCall, facts);
	_LayoutChangeApply(facts);
}

void CUIPresenter::_ReadLayoutFacts(const RECT& comp, BOOL firstCall, PresenterLayoutFacts& facts)
{
	facts = {};
	facts.comp = comp;
	facts.firstCall = firstCall;

	ITfContext *pContext =  _GetContextDocument();
	ITfContextView * pView = nullptr;
	HWND parentWndHandle = nullptr;

	if (pContext && SUCCEEDED(pContext->GetActiveView(&pView)) && pView)
	{
		debugPrint(L"parentWndHandle got from pContext");
		pView->GetWnd(&parentWndHandle);
	}
	else
	{
		parentWndHandle = GetForegroundWindow();
	}

	if (parentWndHandle)
	{
		POINT caretPt = { 0, 0 };
		GetCaretPos(&caretPt);
		ClientToScreen(parentWndHandle, &caretPt);
		debugPrint(L"current caret position from GetCaretPos, x = %d, y = %d", caretPt.x, caretPt.y);
		// Recreate font for fitting current monitor resolution and font size
		if (Global::isWindows8) CConfig::SetDefaultTextFont(parentWndHandle);
		facts.hasParent = TRUE;
		facts.caret = caretPt;
	}
}

#ifdef DIME_CPP_PRESENTER
void CUIPresenter::_LayoutChangeApplyCpp(const PresenterLayoutFacts& facts)
{
	const RECT* lpRect = &facts.comp;
	BOOL firstCall = facts.firstCall;

	RECT compRect = * lpRect;
    RECT candRect = {0, 0, 0, 0};
	RECT notifyRect = {0, 0, 0, 0};
    POINT candPt = {0, 0};
	POINT notifyPt = {0, 0};

	// No parent: the caret stays (0, 0) and the first call cannot place at it.
	BOOL hasParent = facts.hasParent;
	POINT caretPt = facts.hasParent ? facts.caret : POINT{ 0, 0 };

	if (_pCandidateWnd && lpRect)
	{

		if (lpRect->bottom - lpRect->top >=0  || lpRect->right - lpRect->left >=0 ) // confirm the extent rect is valid.
		{
			_pCandidateWnd->_GetClientRect(&candRect);
			if (((compRect.right - compRect.left) > (candRect.right - candRect.left)) && caretPt.x < compRect.right && caretPt.x >= compRect.left)
				compRect.left = caretPt.x;

			_pCandidateWnd->_GetWindowExtent(&compRect, &candRect, &candPt);
			_pCandidateWnd->_Move(candPt.x, candPt.y);
			_candLocation.x = candPt.x;
			_candLocation.y = candPt.y;
			debugPrint(L"move cand to x = %d, y = %d", candPt.x, candPt.y);
		}
		else if (firstCall && hasParent)
		{
			_pCandidateWnd->_GetClientRect(&candRect);
			compRect.left = caretPt.x;
			compRect.right = caretPt.x;
			compRect.top = caretPt.y;
			compRect.bottom = caretPt.y;

			_pCandidateWnd->_GetWindowExtent(&compRect, &candRect, &candPt);
			_pCandidateWnd->_Move(candPt.x, candPt.y);
			_candLocation.x = candPt.x;
			_candLocation.y = candPt.y;
			debugPrint(L"firstCall and compRect is not valid, force move cand to x = %d, y = %d", candPt.x, candPt.y);

		}
	}
	// Find the slot to use as the "anchor" for max-width calc and stack-vs-candidate
	// positioning. Use the first visible slot (index order) — typically slot 0
	// (CHN_ENG) when only mode-state is visible, otherwise the lowest hint slot.
	DimeNotifyWindow* anchorWnd = nullptr;
	UINT stackMaxWidth = 0;
	for (auto& w : _pNotifyWnds)
	{
		if (!w || !w->_IsWindowVisible()) continue;
		if (!anchorWnd) anchorWnd = w.get();
		if (w->_GetWidth() > stackMaxWidth) stackMaxWidth = w->_GetWidth();
	}
	if (anchorWnd && lpRect)
	{
		// Recreate font for fitting current monitor resolution and font size
		if (lpRect->bottom - lpRect->top > 1 || lpRect->right - lpRect->left > 1)   // confirm the extent rect is valid.
		{

			if (_pCandidateWnd && _pCandidateWnd->_IsWindowVisible())
			{
				debugPrint(L"notify max width = %d, candwidth = %d", stackMaxWidth, _pCandidateWnd->_GetWidth());
				// Per-slot x (right-edge aligned to candidate's left) is applied
				// inside _RestackVisibleSlots when the candidate is visible.
				// Here we just set the y anchor: snap to candidate's top edge,
				// or its bottom-minus-anchorHeight when candidate is above caret.
				if (candPt.y < lpRect->top)
					_notifyLocation.y = candPt.y + (candRect.bottom - candRect.top) - anchorWnd->_GetHeight();
				else
					_notifyLocation.y = candPt.y;
				debugPrint(L"_LayoutChangeNotification cand-visible: candPt.y=%d lpRect->top=%d _notifyLocation.y=%d", candPt.y, lpRect->top, _notifyLocation.y);
			}
			else
			{
				anchorWnd->_GetClientRect(&notifyRect);
				if (((compRect.right - compRect.left) > (notifyRect.right - notifyRect.left)) && caretPt.x < compRect.right && caretPt.x >= compRect.left)
					compRect.left = caretPt.x;

				anchorWnd->_GetWindowExtent(&compRect, &notifyRect, &notifyPt);
				_notifyLocation.x = notifyPt.x;
				_notifyLocation.y = notifyPt.y;
				debugPrint(L"_LayoutChangeNotification no-cand: notifyPt.x=%d notifyPt.y=%d compRect.top=%d compRect.bottom=%d", notifyPt.x, notifyPt.y, compRect.top, compRect.bottom);
			}
			// Translate the whole visible stack to the new anchor; keep slot order.
			// Mirror the flipped-candidate detection from ShowNotifyText: if the
			// candidate was above the composition (flipped) and is now gone, force
			// growUp and realign the bottom anchor so a layout event fired between
			// notify shows doesn't restack the slots downward into the composition line.
			{
				BOOL layoutGrowUp = _DecideStackGrowUp();
				if (!(_pCandidateWnd && _pCandidateWnd->_IsWindowVisible())
					&& compRect.bottom > compRect.top
					&& _candLocation.y < compRect.top)
				{
					_notifyLocation.y = compRect.top - SHADOW_SPREAD / 2;
					layoutGrowUp = TRUE;
				}
				_RestackVisibleSlots(layoutGrowUp);
			}
			debugPrint(L"move notify stack to x = %d, y = %d", _notifyLocation.x, _notifyLocation.y);
		}
	}
	_rectCompRange = compRect;
}
#endif

void CUIPresenter::GetCandLocation(_Out_ POINT *lpPoint) const
{
	if(lpPoint)
		_GetPlacement(lpPoint, nullptr, nullptr);
}

LONG CUIPresenter::GetCompRangeHeight() const
{
	RECT comp = {};
	_GetPlacement(nullptr, nullptr, &comp);
	return comp.bottom - comp.top;
}

//+---------------------------------------------------------------------------
//
// _LayoutDestroyNotification
//
//----------------------------------------------------------------------------

VOID CUIPresenter::_LayoutDestroyNotification()
{
	debugPrint(L"CUIPresenter::_LayoutDestroyNotification()");
    _EndCandidateList();
}

//+---------------------------------------------------------------------------
//
// _NotifyChangeNotification
//
//----------------------------------------------------------------------------

HRESULT CUIPresenter::_NotifyChangeNotification(_In_ enum NOTIFY_WND action, _In_ WPARAM wParam, _In_ LPARAM lParam)
{

	debugPrint(L"CUIPresenter::_NotifyChangeNotification() action = %d, _inFocus =%d", action, _inFocus);
	switch(action)
	{
		case NOTIFY_WND::SWITCH_CHN_ENG:
			BOOL isEaten;
			Global::IsShiftKeyDownOnly = TRUE;
			if(_pTextService)
				_pTextService->OnPreservedKey(NULL, Global::DIMEGuidImeModePreserveKey, &isEaten);
			Global::IsShiftKeyDownOnly = FALSE;
			break;
		case NOTIFY_WND::SHOW_NOTIFY:
		{
			NOTIFY_TYPE nt = (NOTIFY_TYPE)lParam;
			BOOL isHintSlot = (nt == NOTIFY_TYPE::NOTIFY_REVERSE_LOOKUP
			                   || nt == NOTIFY_TYPE::NOTIFY_SPECIAL_CODE
			                   || nt == NOTIFY_TYPE::NOTIFY_BEEP);
			// Issue #127: probe should fire for compositional-hint slots too so
			// those popups track the caret even when the floating CHN/ENG notify
			// (which used to be the sole probe trigger) is disabled.
			BOOL needProbe = (nt == NOTIFY_TYPE::NOTIFY_CHN_ENG && _pTextService && !_pTextService->_IsComposing())
			                 || isHintSlot;
			if (needProbe && _pTextService && _GetContextDocument() == nullptr)  //layout is not started. we need to do probecomposition to start layout
			{
				ITfContext* pContext = nullptr;
				ITfThreadMgr* pThreadMgr = nullptr;
				ITfDocumentMgr* pDocumentMgr = nullptr;
				pThreadMgr = _pTextService->_GetThreadMgr();
				if (pThreadMgr)
				{
					if (SUCCEEDED(pThreadMgr->GetFocus(&pDocumentMgr)) && pDocumentMgr)
					{
						if(SUCCEEDED(pDocumentMgr->GetTop(&pContext)) && pContext)
						{
							ShowNotify(TRUE, 0, (UINT) wParam, nt);
							_pTextService->_ProbeComposition(pContext);
						}

					}
				}
			}
			else if(isHintSlot || (_HasCand() && !_CandVisible()))
				ShowNotify(TRUE, 0, (UINT) wParam, nt);
			break;
		}
	}
	return S_OK;

}

//+---------------------------------------------------------------------------
//
// _CandidateChangeNotification
//
//----------------------------------------------------------------------------

HRESULT CUIPresenter::_CandidateChangeNotification(_In_ enum CANDWND_ACTION action)
{
    HRESULT hr = E_FAIL;
    if (_pTextService == nullptr) return hr;

    TfClientId tfClientId = _pTextService->_GetClientId();
    ITfThreadMgr* pThreadMgr = nullptr;
    ITfDocumentMgr* pDocumentMgr = nullptr;
    ITfContext* pContext = _GetContextDocument();
    BOOL needReleaseContext = FALSE;

    // Initialize KeyState to avoid uninitialized variable usage
    _KEYSTROKE_STATE KeyState = { KEYSTROKE_CATEGORY::CATEGORY_NONE, KEYSTROKE_FUNCTION::FUNCTION_NONE };

    KeyState.Category = KEYSTROKE_CATEGORY::CATEGORY_CANDIDATE;
    // Select from the UI. Send FUNCTION_FINALIZE_CANDIDATELIST or FUNCTION_CANCEL to the key handler
    KeyState.Function = (CANDWND_ACTION::CAND_CANCEL == action) ? KEYSTROKE_FUNCTION::FUNCTION_CANCEL : KEYSTROKE_FUNCTION::FUNCTION_FINALIZE_CANDIDATELIST;

    if (!(CANDWND_ACTION::CAND_ITEM_SELECT == action || CANDWND_ACTION::CAND_CANCEL == action))
    {
        goto Exit;
    }

    // Keep context alive during the entire operation
    if (pContext)
    {
        pContext->AddRef();
        needReleaseContext = TRUE;
    }
    else
    {
        pThreadMgr = _pTextService->_GetThreadMgr();
        if (nullptr == pThreadMgr)
        {
            goto Exit;
        }

        hr = pThreadMgr->GetFocus(&pDocumentMgr);
        if (FAILED(hr) || pDocumentMgr == nullptr)
        {
            goto Exit;
        }

        hr = pDocumentMgr->GetTop(&pContext);
        if (FAILED(hr) || pContext == nullptr)
        {
            if (pDocumentMgr)
                pDocumentMgr->Release();
            goto Exit;
        }
        needReleaseContext = TRUE;
        if (pDocumentMgr)
            pDocumentMgr->Release();
    }

    CKeyHandlerEditSession* pEditSession = new (std::nothrow) CKeyHandlerEditSession(_pTextService, pContext, 0, 0, KeyState);
    if (pEditSession && pContext)
    {
        HRESULT hrSession = S_OK;
        hr = pContext->RequestEditSession(tfClientId, pEditSession, TF_ES_SYNC | TF_ES_READWRITE, &hrSession);
        if (hrSession == TF_E_SYNCHRONOUS || hrSession == TS_E_READONLY)
        {
            hr = pContext->RequestEditSession(tfClientId, pEditSession, TF_ES_ASYNC | TF_ES_READWRITE, &hrSession);
        }
        pEditSession->Release();
    }

Exit:
    if (needReleaseContext && pContext)
    {
        pContext->Release();
    }
    return hr;
}

//+---------------------------------------------------------------------------
//
// _CandWndCallback
//
//----------------------------------------------------------------------------

// static
HRESULT CUIPresenter::_CandWndCallback(_In_ void *pv, _In_ enum CANDWND_ACTION action)
{
    CUIPresenter* fakeThis = (CUIPresenter*)pv;
	if(fakeThis == nullptr) return E_INVALIDARG;
    return fakeThis->_CandidateChangeNotification(action);
}

//+---------------------------------------------------------------------------
//
// _NotifyWndCallback
//
//----------------------------------------------------------------------------

// static
HRESULT CUIPresenter::_NotifyWndCallback(_In_ void *pv,_In_ enum NOTIFY_WND action, _In_ WPARAM wParam, _In_ LPARAM lParam)
{
	
    CUIPresenter* fakeThis = (CUIPresenter*)pv;
	if(fakeThis == nullptr) return E_INVALIDARG;
    return fakeThis->_NotifyChangeNotification(action, wParam, lParam);
}

//+---------------------------------------------------------------------------
//
// _UpdateUIElement
//
//----------------------------------------------------------------------------

HRESULT CUIPresenter::_UpdateUIElement()
{
	
    HRESULT hr = S_OK;
	if(_pTextService == nullptr) return hr;
    ITfThreadMgr* pThreadMgr = _pTextService->_GetThreadMgr();
    if (nullptr == pThreadMgr)
    {
        return hr;
    }

    ITfUIElementMgr* pUIElementMgr = nullptr;

    hr = pThreadMgr->QueryInterface(IID_ITfUIElementMgr, (void **)&pUIElementMgr);
    if (hr == S_OK && pUIElementMgr)
    {
        pUIElementMgr->UpdateUIElement(_uiElementId);
        pUIElementMgr->Release();
    }
	debugPrint(L"CUIPresenter::_UpdateUIElement(), hresult = %d", hr);
    return hr;
}

//+---------------------------------------------------------------------------
//
// OnSetThreadFocus
//
//----------------------------------------------------------------------------

HRESULT CUIPresenter::OnSetThreadFocus()
{
	debugPrint(L"CUIPresenter::OnSetThreadFocus()");
    if (_isShowMode)
    {
        Show(TRUE);
    }
	_inFocus = TRUE;
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// OnKillThreadFocus
//
//----------------------------------------------------------------------------

HRESULT CUIPresenter::OnKillThreadFocus()
{
	debugPrint(L"CUIPresenter::OnKillThreadFocus()");

#ifdef CANCEL_CAND_ON_KILL_FOCUS
	if (_CandVisible())
		_CandidateChangeNotification(CANDWND_ACTION::CAND_CANCEL);
#endif

	 ToHideUIWindows();
    _inFocus = FALSE;
    return S_OK;
}

void CUIPresenter::RemoveSpecificCandidateFromList(_In_ LCID Locale, _Inout_ CDIMEArray<CCandidateListItem> &candidateList, _In_ CStringRange &candidateString)
{
    for (UINT index = 0; index < candidateList.Count();)
    {
        CCandidateListItem* pLI = candidateList.GetAt(index);

        if (pLI && CStringRange::Compare(Locale, &candidateString, &pLI->_ItemString) == CSTR_EQUAL)
        {
            candidateList.RemoveAt(index);
            continue;
        }

        index++;
    }
}

void CUIPresenter::AdviseUIChangedByArrowKey(_In_ KEYSTROKE_FUNCTION arrowKey)
{
    switch (arrowKey)
    {
    case KEYSTROKE_FUNCTION::FUNCTION_MOVE_UP:
        {
            if(!isUILessMode())_MoveCandidateSelection(MOVEUP_ONE);
			else if(CConfig::GetArrowKeySWPages())  _MoveCandidatePage(MOVEUP_ONE);
            break;
        }
    case KEYSTROKE_FUNCTION::FUNCTION_MOVE_DOWN:
        {
            if(!isUILessMode()) _MoveCandidateSelection(MOVEDOWN_ONE);
			else if(CConfig::GetArrowKeySWPages())  _MoveCandidatePage(MOVEDOWN_ONE);
            break;
        }
	case KEYSTROKE_FUNCTION::FUNCTION_MOVE_LEFT:
        {
            if(isUILessMode()) _MoveCandidateSelection(MOVEUP_ONE);
			else if(CConfig::GetArrowKeySWPages())  _MoveCandidatePage(MOVEUP_ONE);
            break;
        }
    case KEYSTROKE_FUNCTION::FUNCTION_MOVE_RIGHT:
        {
			if(isUILessMode()) _MoveCandidateSelection(MOVEDOWN_ONE);  //UI less mode is horizontal layout
			else if(CConfig::GetArrowKeySWPages())  _MoveCandidatePage(MOVEDOWN_ONE);
            break;
        }
    case KEYSTROKE_FUNCTION::FUNCTION_MOVE_PAGE_UP:
        {
            _MoveCandidatePage(MOVEUP_ONE);
            break;
        }
    case KEYSTROKE_FUNCTION::FUNCTION_MOVE_PAGE_DOWN:
        {
            _MoveCandidatePage(MOVEDOWN_ONE);
            break;
        }
    case KEYSTROKE_FUNCTION::FUNCTION_MOVE_PAGE_TOP:
        {
            _SetCandidateSelection(MOVETO_TOP);
            break;
        }
    case KEYSTROKE_FUNCTION::FUNCTION_MOVE_PAGE_BOTTOM:
        {
            _SetCandidateSelection(MOVETO_BOTTOM);
            break;
        }
    default:
        break;
    }
}

HRESULT CUIPresenter::BeginUIElement()
{
	HRESULT hr = S_OK;

	if(_pTextService == nullptr) 
	{
		hr = E_FAIL;
		goto Exit; 
	}

    ITfThreadMgr* pThreadMgr = _pTextService->_GetThreadMgr();
    if (nullptr ==pThreadMgr)
    {
        hr = E_FAIL;
        goto Exit;
    }

    ITfUIElementMgr* pUIElementMgr = nullptr;
    hr = pThreadMgr->QueryInterface(IID_ITfUIElementMgr, (void **)&pUIElementMgr);
    if (hr == S_OK)
    {
        pUIElementMgr->BeginUIElement(this, &_isShowMode, &_uiElementId);
        pUIElementMgr->Release();
    }

Exit:
	debugPrint(L"CUIPresenter::BeginUIElement(), _isShowMode = %d, _uiElementId = %d, hresult = %d"
		, _isShowMode, _uiElementId, hr);
    return hr;
}

HRESULT CUIPresenter::EndUIElement()
{
	debugPrint(L"CUIPresenter::EndUIElement(), _uiElementId = %d ", _uiElementId);
    HRESULT hr = S_OK;

	if(_pTextService == nullptr) 
	{
		hr = E_FAIL;
		goto Exit; 
	}

    ITfThreadMgr* pThreadMgr = _pTextService->_GetThreadMgr();
    if ((nullptr == pThreadMgr) || (-1 == _uiElementId))
    {
        hr = E_FAIL;
        goto Exit;
    }

    ITfUIElementMgr* pUIElementMgr = nullptr;
    hr = pThreadMgr->QueryInterface(IID_ITfUIElementMgr, (void **)&pUIElementMgr);
    if (hr == S_OK && pUIElementMgr)
    {
        pUIElementMgr->EndUIElement(_uiElementId);
        pUIElementMgr->Release();
    }

Exit:
	debugPrint(L"CUIPresenter::EndUIElement(), hresult = %d ", hr);
    return hr;
}

HRESULT CUIPresenter::MakeNotifyWindow(_In_ ITfContext *pContextDocument, _In_ CStringRange * notifyText, _In_ enum NOTIFY_TYPE notifyType)
{
	size_t idx = (size_t)notifyType;
	if (idx >= (size_t)NOTIFY_TYPE::NOTIFY_COUNT) return S_FALSE;
#ifdef DIME_RUST_AVAILABLE
	if (_RustPresenter())
	{
		HWND parent = nullptr;
		ITfContextView* pRustView = nullptr;
		if (pContextDocument && SUCCEEDED(pContextDocument->GetActiveView(&pRustView)) && pRustView)
			pRustView->GetWnd(&parent);
		DimeNotifyClasses classes = { Global::AtomNotifyWindow, Global::AtomNotifyShadowWindow };
#ifdef DIME_UNIT_TESTING
		if (_notifyAtomForTest) classes = { _notifyAtomForTest, _notifyShadowAtomForTest };
#endif
		dime_ui_presenter_notify_make(_rp, (uint32_t)idx, Global::dllInstanceHandle, &classes, parent,
		                              notifyText ? (const uint16_t*)notifyText->Get() : nullptr, notifyText ? notifyText->GetLength() : 0);
		return S_OK;   // also when the window could not be created
	}
#endif
#ifdef DIME_CPP_PRESENTER
	HRESULT hr = S_OK;
	auto& slot = _pNotifyWnds[idx];

	if (nullptr == slot)
    {
		slot = std::make_unique<DimeNotifyWindow>(_NotifyWndCallback, this, notifyType);
	}

    if (nullptr == slot)	return S_FALSE;

	HWND parentWndHandle = nullptr;
    ITfContextView* pView = nullptr;
    if (pContextDocument && SUCCEEDED(pContextDocument->GetActiveView(&pView)) && pView)
    {
        pView->GetWnd(&parentWndHandle);
    }

	if (!slot->_IsCreated())
	{
		if( !slot->_Create(CConfig::GetFontSize(), parentWndHandle, notifyText))
		{
			hr = E_OUTOFMEMORY;
			return hr;
		}
	}

	return hr;
#else
	return S_FALSE;
#endif

}


void CUIPresenter::SetNotifyText(_In_ CStringRange *pNotifyText, _In_opt_ NOTIFY_TYPE notifyType)
{
	debugPrint(L"CUIPresenter::SetNotifyText()");
	size_t idx = (size_t)notifyType;
#ifdef DIME_RUST_AVAILABLE
	if (_RustPresenter())
	{
		if (idx < (size_t)NOTIFY_TYPE::NOTIFY_COUNT && pNotifyText)
			dime_ui_presenter_notify_set_string(_rp, (uint32_t)idx, (const uint16_t*)pNotifyText->Get(), pNotifyText->GetLength());
		return;
	}
#endif
#ifdef DIME_CPP_PRESENTER
	if (idx < _pNotifyWnds.size() && _pNotifyWnds[idx])
	{
		_pNotifyWnds[idx]->_SetString(pNotifyText);
	}
#endif
}
void CUIPresenter::ShowNotify(_In_ BOOL showMode,  _In_opt_ UINT delayShow, _In_opt_ UINT timeToHide, _In_opt_ NOTIFY_TYPE notifyType)
{
	debugPrint(L"CUIPresenter::ShowNotify(slot=%d)", (int)notifyType);
	size_t idx = (size_t)notifyType;
#ifdef DIME_RUST_AVAILABLE
	if (_RustPresenter())
	{
		if (idx < (size_t)NOTIFY_TYPE::NOTIFY_COUNT)
			dime_ui_presenter_notify_show(_rp, (uint32_t)idx, showMode ? 1 : 0, delayShow, timeToHide);
		return;
	}
#endif
#ifdef DIME_CPP_PRESENTER
	if (idx < _pNotifyWnds.size() && _pNotifyWnds[idx])
		_pNotifyWnds[idx]->_Show(showMode, delayShow, timeToHide);
#endif
}
void CUIPresenter::ClearAll()
{
	debugPrint(L"CUIPresenter::ClearAll()");
	ClearNotify();
	if(_HasCand())
		_EndCandidateList();
}

void CUIPresenter::ClearNotify()
{
	debugPrint(L"CUIPresenter::ClearNotify()");
	BOOL anySlot = FALSE;
	for (size_t i = 0; i < (size_t)NOTIFY_TYPE::NOTIFY_COUNT && !anySlot; i++)
		anySlot = _NotifySlotExists((NOTIFY_TYPE)i);
	if (!anySlot) return;

	if (_GetContextDocument() && !_HasCand()) //cand is not here. the layoutsink was start by notify, and we need to end here.
		_EndLayout();

	DisposeNotifyWindow();  // disposes all slots
}

BOOL CUIPresenter::_AnyHintSlotVisible() const
{
#ifdef DIME_RUST_AVAILABLE
	if (_RustPresenter())
		return dime_ui_presenter_notify_query(_rp, (uint32_t)NOTIFY_TYPE::NOTIFY_COUNT, 2) ? TRUE : FALSE;
#endif
#ifdef DIME_CPP_PRESENTER
	auto& rl = _pNotifyWnds[(size_t)NOTIFY_TYPE::NOTIFY_REVERSE_LOOKUP];
	auto& sc = _pNotifyWnds[(size_t)NOTIFY_TYPE::NOTIFY_SPECIAL_CODE];
	return ((rl && rl->_IsWindowVisible()) || (sc && sc->_IsWindowVisible())) ? TRUE : FALSE;
#else
	return FALSE;
#endif
}

#ifdef DIME_CPP_PRESENTER
// The notify slots as UILayout sees them (a missing window: not visible, height 0).
template <typename Windows>
static void _NotifySlots(const Windows& wnds, std::vector<UILayout::NotifySlot>& slots)
{
	slots.clear();
	for (auto& w : wnds)
	{
		UILayout::NotifySlot slot = { 0, 0, 0 };
		if (w)
		{
			slot.visible = w->_IsWindowVisible() ? 1 : 0;
			slot.width = (int)w->_GetWidth();
			slot.height = (int)w->_GetHeight();
		}
		slots.push_back(slot);
	}
}

BOOL CUIPresenter::_DecideStackGrowUp() const
{
	BOOL candVisible = (_pCandidateWnd && _pCandidateWnd->_IsWindowVisible());
	RECT cr = { 0,0,0,0 };
	if (candVisible)
		_pCandidateWnd->_GetWindowRect(&cr);
	std::vector<UILayout::NotifySlot> slots;
	_NotifySlots(_pNotifyWnds, slots);
	// Without a candidate: only flip up if the downward stack would overflow the screen bottom.
	return UILayout::DecideStackGrowUp(candVisible, cr.top, _notifyLocation.y, slots.data(), slots.size(),
		candVisible ? 0 : GetSystemMetrics(SM_CYSCREEN));
}

POINT CUIPresenter::_ComputeStackPosForNewSlot(_In_ NOTIFY_TYPE newSlot, _In_ BOOL growUp) const
{
	std::vector<UILayout::NotifySlot> slots;
	_NotifySlots(_pNotifyWnds, slots);
	return UILayout::ComputeStackPosForNewSlot(_notifyLocation, slots.data(), slots.size(), (size_t)newSlot, growUp);
}
#endif

void CUIPresenter::_RestackVisibleSlots(_In_ BOOL growUp)
{
#ifdef DIME_RUST_AVAILABLE
	if (_RustPresenter())
	{
		dime_ui_presenter_restack_visible_slots(_rp, growUp ? 1 : 0);
		return;
	}
#endif
#ifdef DIME_CPP_PRESENTER
	// X rule: candidate visible → each slot's RIGHT edge aligned to candidate's
	// left edge (per-slot x). No candidate → all share _notifyLocation.x.
	// Y rule (candidate visible):
	//   growUp=false → first slot's TOP at candidate's TOP, stack grows down.
	//   growUp=true  → first slot's BOTTOM at candidate's BOTTOM, stack grows up.
	// Y rule (no candidate): anchor y is _notifyLocation.y as set by caller.
	// Slots are placed in index order (0 → N): slot 0 (CHN_ENG) lands nearest the
	// candidate / composition either way.
	BOOL candVisible = (_pCandidateWnd && _pCandidateWnd->_IsWindowVisible());
	RECT cr = { 0,0,0,0 };
	if (candVisible)
		_pCandidateWnd->_GetWindowRect(&cr);
	std::vector<UILayout::NotifySlot> slots;
	_NotifySlots(_pNotifyWnds, slots);
	std::vector<POINT> positions(slots.size());
	std::vector<BOOL> placed(slots.size());
	UILayout::RestackVisibleSlots(candVisible, cr, _candLocation, _notifyLocation, slots.data(), slots.size(), growUp,
		positions.data(), placed.data());
	for (size_t i = 0; i < slots.size(); ++i)
	{
		if (placed[i])
			_pNotifyWnds[i]->_Move(positions[i].x, positions[i].y);
	}
#endif
}
void CUIPresenter::ResetCompRange()
{
#ifdef DIME_RUST_AVAILABLE
	if (_RustPresenter())
	{
		dime_ui_presenter_reset_comp_range(_rp);
		debugPrint(L"ResetCompRange: _notifyLocation reset to DEFAULT_WINDOW");
		return;
	}
#endif
#ifdef DIME_CPP_PRESENTER
	SetRect(&_rectCompRange, 0, 0, 0, 0);
	_notifyLocation = { UI::DEFAULT_WINDOW_X, UI::DEFAULT_WINDOW_Y };
	debugPrint(L"ResetCompRange: _notifyLocation reset to DEFAULT_WINDOW");
#endif
}
void CUIPresenter::ShowNotifyText(_In_ CStringRange* pNotifyText, _In_opt_ UINT delayShow, _In_opt_ UINT timeToHide, _In_opt_ NOTIFY_TYPE notifyType)
{
	//if(pNotifyText) debugPrint(L"CUIPresenter::ShowNotifyText(): text = %s, delayShow = %d, timeTimeHide = %d, notifyType= %d, _inFocus = %d, ", pNotifyText->Get(), delayShow, timeToHide, notifyType, _inFocus);
	ITfContext* pContext = _GetContextDocument();
	ITfThreadMgr* pThreadMgr = nullptr;
	ITfDocumentMgr* pDocumentMgr = nullptr;

	size_t idx = (size_t)notifyType;
	if (idx >= (size_t)NOTIFY_TYPE::NOTIFY_COUNT) return;

	// Issue #127 / multi-notify: CHN_ENG (and other mode-state slots) must not
	// clobber a visible compositional-hint slot. Generalised guard from the
	// historical CHN_ENG-vs-OTHERS rule.
	if (notifyType == NOTIFY_TYPE::NOTIFY_CHN_ENG && _AnyHintSlotVisible())
		return;

	// Replace-in-place for the SAME slot; leave OTHER slots untouched (was: ClearNotify all).
	DisposeNotifyWindow(notifyType);

	if(pContext == nullptr && _pTextService)
	{
		pThreadMgr = _pTextService->_GetThreadMgr();
		if (pThreadMgr)
		{
			if (SUCCEEDED(pThreadMgr->GetFocus(&pDocumentMgr)) && pDocumentMgr)
			{
				pDocumentMgr->GetTop(&pContext);
			}
		}
	}

	if(pContext != nullptr)
	{
		if(MakeNotifyWindow(pContext, pNotifyText, notifyType)== S_OK)
		{

			if (CConfig::GetEffectiveDarkMode())
			_SetNotifyTextColor(CConfig::GetDarkItemColor(), CConfig::GetDarkItemBGColor());
		else if (CConfig::GetColorMode() == IME_COLOR_MODE::IME_COLOR_MODE_CUSTOM)
			_SetNotifyTextColor(CConfig::GetItemColor(), CConfig::GetItemBGColor());
		else
			_SetNotifyTextColor(CConfig::GetLightItemColor(), CConfig::GetLightItemBGColor());

			HWND parentWndHandle = nullptr;
			ITfContextView* pView = nullptr;

			if (SUCCEEDED(pContext->GetActiveView(&pView)) && pView)
			{
				pView->GetWnd(&parentWndHandle);
				debugPrint(L" parentWndHandle = %x , FocusHwnd = %x, ActiveHwnd =%x, ForeGroundHWnd = %x", parentWndHandle, GetFocus(), GetActiveWindow(), GetForegroundWindow());
				// Issue #127: for transient slots (compositional hints + BEEP),
				// refresh _notifyLocation each show so a stale value from an earlier
				// show does not stick (see _NotifyLocationForShowCpp).
				PresenterShowFacts facts = {};
				facts.refresh = (notifyType == NOTIFY_TYPE::NOTIFY_REVERSE_LOOKUP
				                 || notifyType == NOTIFY_TYPE::NOTIFY_SPECIAL_CODE
				                 || notifyType == NOTIFY_TYPE::NOTIFY_BEEP);
				if (parentWndHandle)
				{   //for ancient non TSF aware apps with a floating composition window.  The caret position we can get is always the caret in the floating composition window.
					GUITHREADINFO guiInfo = {};
					guiInfo.cbSize = sizeof(GUITHREADINFO);
					if (!GetGUIThreadInfo(NULL, &guiInfo))
						guiInfo.rcCaret = { 0, 0, 0, 0 };
					POINT pt = { guiInfo.rcCaret.left, guiInfo.rcCaret.bottom };
					ClientToScreen(parentWndHandle, &pt);
					debugPrint(L"current caret position from GetGUIThreadInfo, x = %d, y = %d", pt.x, pt.y);
					facts.hasParent = TRUE;
					facts.rcCaret = guiInfo.rcCaret;
					facts.guiCaret = pt;
				}
				BOOL caretUpdated = _NotifyLocationForShow(facts);

				ShowNotify(TRUE, delayShow, timeToHide, notifyType);

				if(delayShow == 0 && _NotifySlotExists(notifyType))
					_RestackForShow(caretUpdated);
				//means TextLayoutSink is not working. We need to ProbeComposition to start layout.
				// Issue #127: also probe for compositional-hint slots and BEEP so they
				// track the caret when ShowNotifyDesktop pref is OFF.
				if(_pTextService && delayShow == 0 && _GetContextDocument() == nullptr
				   && (notifyType == NOTIFY_TYPE::NOTIFY_CHN_ENG
				       || notifyType == NOTIFY_TYPE::NOTIFY_REVERSE_LOOKUP
				       || notifyType == NOTIFY_TYPE::NOTIFY_SPECIAL_CODE
				       || notifyType == NOTIFY_TYPE::NOTIFY_BEEP) )
					_pTextService->_ProbeComposition(pContext);

			}

		}


	}


}

//+---------------------------------------------------------------------------
//
// ShowNotifyText's location decisions (C++ originals)
//
//----------------------------------------------------------------------------

#ifdef DIME_CPP_PRESENTER
BOOL CUIPresenter::_NotifyLocationForShowCpp(const PresenterShowFacts& facts)
{
	BOOL refreshLocation = facts.refresh;
	// Track whether a valid caret position was actually written to _notifyLocation
	// from GetGUIThreadInfo. When FALSE (e.g. Firefox always returns
	// zero rcCaret), the _candLocation fallback below must cover the refreshLocation
	// case too — not just the uninitialised case — to avoid reusing a stale
	// _notifyLocation from a previous typing session at a different screen position.
	BOOL caretUpdated = FALSE;
	if (facts.hasParent)
	{
		// Check raw rcCaret BEFORE ClientToScreen. Firefox/Chromium return
		// rcCaret={0,0,0,0}; ClientToScreen then maps that to the window's
		// screen origin (non-zero), which would wrongly overwrite a cached
		// valid position with the browser window's top-left corner.
		BOOL validCaretPos = (facts.rcCaret.left   != 0 || facts.rcCaret.top    != 0
		                   || facts.rcCaret.right  != 0 || facts.rcCaret.bottom != 0);
		// Only update when rcCaret is non-zero. When validCaretPos=FALSE,
		// pt (after ClientToScreen) is the browser window's screen origin —
		// not the caret — so we must not write it even for initialisation.
		if (validCaretPos) {
			if (refreshLocation || _notifyLocation.x < 0) _notifyLocation.x = facts.guiCaret.x;
			if (refreshLocation || _notifyLocation.y < 0) _notifyLocation.y = facts.guiCaret.y;
			caretUpdated = TRUE;
		}
	}
	// (The original's GetCaretPos branch ran only when `new GUITHREADINFO` returned null,
	// which a throwing new never does.)
	// Firefox fallback: when no valid caret was obtained and
	// _notifyLocation is uninitialised (reset on focus change by ResetCompRange),
	// seed from the last known candidate position. Conditioned on _notifyLocation.x < 0
	// only — not refreshLocation — so a freshly-set _notifyLocation from
	// _LayoutChangeNotification during active composition is never overwritten by a
	// stale _candLocation from a previous text field (e.g. Gmail compose box Y
	// bleeding into the search bar).
	if (!caretUpdated && _notifyLocation.x < 0 && _candLocation.x >= 0)
	{
		_notifyLocation.x = _candLocation.x;
		// If the candidate was flipped above the caret (candLocation.y < compRange.top),
		// _notifyLocation.y is used directly as slot TOP in the no-candidate restack path,
		// so place it below the composition text (not at candidate top or candidate bottom).
		if (_rectCompRange.bottom > _rectCompRange.top && _candLocation.y < _rectCompRange.top)
			_notifyLocation.y = _rectCompRange.bottom + SHADOW_SPREAD / 2;
		else
			_notifyLocation.y = _candLocation.y;
	}
	debugPrint(L"ShowNotifyText post-fallback: notifyLoc=(%d,%d)", _notifyLocation.x, _notifyLocation.y);
	return caretUpdated;
}

void CUIPresenter::_RestackForShowCpp(BOOL caretUpdated)
{
	// Restack ALL visible slots from the same _notifyLocation anchor
	// so every slot — new or pre-existing — shares the same X edge.
	// Using _ComputeStackPosForNewSlot+_Move for only the new slot
	// caused X misalignment when _notifyLocation changed between the
	// first and second ShowNotifyText calls (e.g. probe fired in between).
	BOOL growUp = _DecideStackGrowUp();
	if (_pCandidateWnd && _pCandidateWnd->_IsWindowVisible())
	{
		// Set _notifyLocation.y above candidate top so _DecideStackGrowUp returns
		// TRUE when the candidate is flipped. _RestackVisibleSlots then uses
		// cr.bottom as the actual anchor (slots grow upward from candidate bottom).
		if (_rectCompRange.top > 0 && _candLocation.y < _rectCompRange.top)
		{
			RECT _candWndRect = {};
			_pCandidateWnd->_GetWindowRect(&_candWndRect);
			_notifyLocation.y = _candLocation.y + (_candWndRect.bottom - _candWndRect.top);
		}
		else
			_notifyLocation.y = _candLocation.y;
	}
	else if (!caretUpdated && _rectCompRange.bottom > _rectCompRange.top
	         && _candLocation.y < _rectCompRange.top)
	{
		// Candidate was flipped above caret. Anchor = candidate window bottom,
		// which _GetWindowExtent always places SHADOW_SPREAD/2 above comp top:
		//   candBottom = _rectCompRange.top - SHADOW_SPREAD/2
		// Using _rectCompRange.top directly lands the slot 7px too low.
		// Also realign X to candidate left edge.
		_notifyLocation.y = _rectCompRange.top - SHADOW_SPREAD / 2;
		growUp = TRUE;
		if (_candLocation.x >= 0)
			_notifyLocation.x = _candLocation.x;
	}
	debugPrint(L"ShowNotifyText restack: notifyLoc=(%d,%d) candVisible=%d rectComp.top=%d candLoc.y=%d",
	           _notifyLocation.x, _notifyLocation.y,
	           (_pCandidateWnd && _pCandidateWnd->_IsWindowVisible()) ? 1 : 0,
	           _rectCompRange.top, _candLocation.y);
	_RestackVisibleSlots(growUp);
}
#endif

#ifdef DIME_RUST_AVAILABLE
static_assert(sizeof(PresenterLayoutFacts) == sizeof(DimeLayoutFacts), "DimeLayoutFacts layout");
static_assert(sizeof(PresenterShowFacts) == sizeof(DimeShowFacts), "DimeShowFacts layout");
static_assert(sizeof(DimeRect) == sizeof(RECT) && sizeof(DimePoint) == sizeof(POINT), "RECT / POINT layout");

// The candidate window's callback (as CRustCandidateWindowModel::Invoke): may destroy the window.
void CUIPresenter::_CandWndCallbackForRust(void* ctx, int32_t action)
{
    _CandWndCallback(ctx, action == 0 ? CANDWND_ACTION::CAND_ITEM_SELECT : CANDWND_ACTION::CAND_CANCEL);
}

// The notify windows' callback (as CRustNotifyWindow::Invoke): may destroy the windows.
void CUIPresenter::_NotifyWndInvoke(void* ctx, int32_t action, uintptr_t wParam, intptr_t lParam)
{
    _NotifyWndCallback(ctx, action == 0 ? NOTIFY_WND::SWITCH_CHN_ENG : NOTIFY_WND::SHOW_NOTIFY, (WPARAM)wParam, (LPARAM)lParam);
}
#else
void CUIPresenter::_NotifyWndInvoke(void*, int32_t, uintptr_t, intptr_t) {}
void CUIPresenter::_CandWndCallbackForRust(void*, int32_t) {}
#endif

//+---------------------------------------------------------------------------
//
// The presenter state: Rust or C++
//
//----------------------------------------------------------------------------

BOOL CUIPresenter::_RustPresenter() const
{
    return (_useRustPresenter && _rp) ? TRUE : FALSE;
}

void CUIPresenter::_GetPlacement(_Out_opt_ POINT* cand, _Out_opt_ POINT* notify, _Out_opt_ RECT* comp) const
{
    POINT c = {}, n = {};
    RECT r = {};
#ifdef DIME_CPP_PRESENTER
    c = _candLocation;
    n = _notifyLocation;
    r = _rectCompRange;
#endif
#ifdef DIME_RUST_AVAILABLE
    if (_RustPresenter())
    {
        DimePresenterState st = {};
        dime_ui_presenter_get_state(_rp, &st);
        c = { st.cand_location.x, st.cand_location.y };
        n = { st.notify_location.x, st.notify_location.y };
        r = { st.rect_comp_range.left, st.rect_comp_range.top, st.rect_comp_range.right, st.rect_comp_range.bottom };
    }
#endif
    if (cand) *cand = c;
    if (notify) *notify = n;
    if (comp) *comp = r;
}

void CUIPresenter::_SetPlacement(POINT cand, POINT notify, RECT comp)
{
#ifdef DIME_RUST_AVAILABLE
    if (_RustPresenter())
    {
        DimePresenterState st = {};
        st.cand_location = { cand.x, cand.y };
        st.notify_location = { notify.x, notify.y };
        st.rect_comp_range = { comp.left, comp.top, comp.right, comp.bottom };
        dime_ui_presenter_set_state(_rp, &st);
        return;
    }
#endif
#ifdef DIME_CPP_PRESENTER
    _candLocation = cand;
    _notifyLocation = notify;
    _rectCompRange = comp;
#endif
}

BOOL CUIPresenter::_HasCand() const
{
#ifdef DIME_RUST_AVAILABLE
    if (_RustPresenter())
        return dime_ui_presenter_cand_query(_rp, 0) ? TRUE : FALSE;
#endif
#ifdef DIME_CPP_PRESENTER
    return _pCandidateWnd ? TRUE : FALSE;
#else
    return FALSE;
#endif
}

BOOL CUIPresenter::_CandVisible() const
{
#ifdef DIME_RUST_AVAILABLE
    if (_RustPresenter())
        return dime_ui_presenter_cand_query(_rp, 1) ? TRUE : FALSE;
#endif
#ifdef DIME_CPP_PRESENTER
    return (_pCandidateWnd && _pCandidateWnd->_IsWindowVisible()) ? TRUE : FALSE;
#else
    return FALSE;
#endif
}

void CUIPresenter::_CandShow(BOOL show)
{
    _CandOp(0, show ? 1 : 0);
}

// The candidate window operations of dime_ui_presenter_cand_op: 0 _Show(a), 3 _InvalidateRect,
// 4 _SetCandStringLength(a), 5 _SetFillColor(a), 9 _SetScrollInfo(a, b). Nothing without a window.
void CUIPresenter::_CandOp(UINT op, int a, int b)
{
#ifdef DIME_RUST_AVAILABLE
    if (_RustPresenter())
    {
        dime_ui_presenter_cand_op(_rp, op, a, b);
        return;
    }
#endif
#ifdef DIME_CPP_PRESENTER
    if (!_pCandidateWnd) return;
    switch (op)
    {
    case 0: _pCandidateWnd->_Show(a ? TRUE : FALSE); break;
    case 3: _pCandidateWnd->_InvalidateRect(); break;
    case 4: _pCandidateWnd->_SetCandStringLength((UINT)a); break;
    case 5: _pCandidateWnd->_SetFillColor((COLORREF)a); break;
    case 9: _pCandidateWnd->_SetScrollInfo(a, b); break;
    default: break;
    }
#endif
}

// Selection: 0 _MoveSelection, 1 _SetSelection, 2 _MovePage (with notify), 3 _SetSelectionInPage.
BOOL CUIPresenter::_CandSelect(UINT op, int a, BOOL notify)
{
#ifdef DIME_RUST_AVAILABLE
    if (_RustPresenter())
        return dime_ui_presenter_cand_select(_rp, op, a, notify ? 1 : 0) ? TRUE : FALSE;
#endif
#ifdef DIME_CPP_PRESENTER
    if (!_pCandidateWnd) return FALSE;
    switch (op)
    {
    case 0: return _pCandidateWnd->_MoveSelection(a, notify);
    case 1: return _pCandidateWnd->_SetSelection(a, notify);
    case 2: return _pCandidateWnd->_MovePage(a, notify);
    case 3: return _pCandidateWnd->_SetSelectionInPage(a);
    default: return FALSE;
    }
#else
    return FALSE;
#endif
}

HWND CUIPresenter::_CandWnd() const
{
#ifdef DIME_RUST_AVAILABLE
    if (_RustPresenter())
        return (HWND)dime_ui_presenter_cand_hwnd(_rp);
#endif
#ifdef DIME_CPP_PRESENTER
    return _pCandidateWnd ? _pCandidateWnd->_GetWnd() : nullptr;
#else
    return nullptr;
#endif
}

BOOL CUIPresenter::_NotifySlotExists(NOTIFY_TYPE notifyType) const
{
    size_t idx = (size_t)notifyType;
    if (idx >= (size_t)NOTIFY_TYPE::NOTIFY_COUNT) return FALSE;
#ifdef DIME_RUST_AVAILABLE
    if (_RustPresenter())
        return dime_ui_presenter_notify_query(_rp, (uint32_t)idx, 0) ? TRUE : FALSE;
#endif
#ifdef DIME_CPP_PRESENTER
    return _pNotifyWnds[idx] ? TRUE : FALSE;
#else
    return FALSE;
#endif
}

HWND CUIPresenter::_NotifySlotWnd(NOTIFY_TYPE notifyType, _Out_opt_ UINT* width, _Out_opt_ UINT* height) const
{
    size_t idx = (size_t)notifyType;
    if (width) *width = 0;
    if (height) *height = 0;
    if (idx >= (size_t)NOTIFY_TYPE::NOTIFY_COUNT) return nullptr;
#ifdef DIME_RUST_AVAILABLE
    if (_RustPresenter())
    {
        int32_t cx = 0, cy = 0;
        HWND h = (HWND)dime_ui_presenter_notify_window(_rp, (uint32_t)idx, &cx, &cy);
        if (width) *width = (UINT)cx;
        if (height) *height = (UINT)cy;
        return h;
    }
#endif
#ifdef DIME_CPP_PRESENTER
    auto& w = _pNotifyWnds[idx];
    if (!w) return nullptr;
    if (width) *width = w->_GetWidth();
    if (height) *height = w->_GetHeight();
    return w->_GetWnd();
#else
    return nullptr;
#endif
}

#ifdef DIME_UNIT_TESTING
void CUIPresenter::_PaintCandidateForTest(HDC dc, PAINTSTRUCT* ps)
{
#ifdef DIME_RUST_AVAILABLE
    DimeRect pr = { ps->rcPaint.left, ps->rcPaint.top, ps->rcPaint.right, ps->rcPaint.bottom };
    if (_RustPresenter())
    {
        dime_ui_presenter_cand_paint(_rp, dc, &pr);
        return;
    }
#endif
#ifdef DIME_CPP_PRESENTER
    if (!_pCandidateWnd) return;
    _pCandidateWnd->_OnPaint(dc, ps);
#endif
}

BOOL CUIPresenter::_CandScrollInfoForTest(int* info)
{
#ifdef DIME_RUST_AVAILABLE
    if (_RustPresenter())
        return dime_ui_presenter_cand_sb_info(_rp, (int32_t*)info) ? TRUE : FALSE;
#endif
#ifdef DIME_CPP_PRESENTER
    if (!_pCandidateWnd) return FALSE;
    IDimeScrollBar* sb = _pCandidateWnd->_GetScrollBarForTest();
    if (!sb) return FALSE;
    SbStateForTest ss = sb->SbGetStateForTest();
    info[0] = ss.si.nMax; info[1] = ss.si.nPage; info[2] = ss.si.nPos;
    return TRUE;
#else
    return FALSE;
#endif
}

void CUIPresenter::_PaintNotifySlotForTest(NOTIFY_TYPE notifyType, HDC dc, PAINTSTRUCT* ps)
{
    size_t idx = (size_t)notifyType;
    if (idx >= (size_t)NOTIFY_TYPE::NOTIFY_COUNT) return;
#ifdef DIME_RUST_AVAILABLE
    DimeRect pr = {};
    if (ps) pr = { ps->rcPaint.left, ps->rcPaint.top, ps->rcPaint.right, ps->rcPaint.bottom };
    if (_RustPresenter())
    {
        dime_ui_presenter_notify_paint(_rp, (uint32_t)idx, dc, ps ? &pr : nullptr);
        return;
    }
#endif
#ifdef DIME_CPP_PRESENTER
    auto& w = _pNotifyWnds[idx];
    if (!w) return;
    w->_OnPaint(dc, ps);
#endif
}
#endif

//+---------------------------------------------------------------------------
//
// The dispatchers (the Rust branch first in each body; see CLAUDE.md)
//
//----------------------------------------------------------------------------

void CUIPresenter::_LayoutChangeApply(const PresenterLayoutFacts& facts)
{
#ifdef DIME_RUST_AVAILABLE
    if (_RustPresenter())
    {
        dime_ui_presenter_layout_change(_rp, (const DimeLayoutFacts*)&facts);
        return;
    }
#endif
#ifdef DIME_CPP_PRESENTER
    _LayoutChangeApplyCpp(facts);
#endif
}

BOOL CUIPresenter::_NotifyLocationForShow(const PresenterShowFacts& facts)
{
#ifdef DIME_RUST_AVAILABLE
    if (_RustPresenter())
        return dime_ui_presenter_notify_location_for_show(_rp, (const DimeShowFacts*)&facts) ? TRUE : FALSE;
#endif
#ifdef DIME_CPP_PRESENTER
    return _NotifyLocationForShowCpp(facts);
#else
    return FALSE;
#endif
}

void CUIPresenter::_RestackForShow(BOOL caretUpdated)
{
#ifdef DIME_RUST_AVAILABLE
    if (_RustPresenter())
    {
        dime_ui_presenter_restack_for_show(_rp, caretUpdated ? 1 : 0);
        return;
    }
#endif
#ifdef DIME_CPP_PRESENTER
    _RestackForShowCpp(caretUpdated);
#endif
}

BOOL CUIPresenter::IsNotifyShown()
{
#ifdef DIME_RUST_AVAILABLE
	if (_RustPresenter())
		return dime_ui_presenter_notify_query(_rp, (uint32_t)NOTIFY_TYPE::NOTIFY_COUNT, 1) ? TRUE : FALSE;
#endif
#ifdef DIME_CPP_PRESENTER
	for (auto& w : _pNotifyWnds)
	{
		if (w && w->_IsWindowVisible()) return TRUE;
	}
#endif
	return FALSE;
}
BOOL CUIPresenter::IsCandShown()
{
	if(_CandVisible())
		return TRUE;
	else
		return FALSE;
}



HRESULT CUIPresenter::MakeCandidateWindow(_In_ ITfContext *pContextDocument, _In_ UINT wndWidth)
{
    HRESULT hr = S_OK;

    if (_HasCand() || _pTextService == nullptr)
    {
        hr = E_FAIL;
		goto Exit;
    }
#ifdef DIME_RUST_AVAILABLE
    if (_RustPresenter())
    {
        HWND parent = nullptr;
        ITfContextView* pRustView = nullptr;
        if (pContextDocument && SUCCEEDED(pContextDocument->GetActiveView(&pRustView)) && pRustView)
            pRustView->GetWnd(&parent);
        DimeCandClasses classes = { Global::AtomCandidateWindow, Global::AtomCandidateShadowWindow,
                                    Global::AtomCandidateScrollBarWindow, Global::AtomNotifyWindow, Global::AtomNotifyShadowWindow };
#ifdef DIME_UNIT_TESTING
        if (_candClassesForTest.candidate) classes = _candClassesForTest;
#endif
        // Black text on the window color, the 3D highlight fill.
        COLORREF text = CANDWND_ITEM_COLOR, bk = GetSysColor(COLOR_WINDOW);
        const DimeCandColors colors = { text, bk, text, bk, text, bk };
        dime_ui_presenter_cand_make(_rp, Global::dllInstanceHandle, &classes, parent, wndWidth, _pIndexRange, &colors,
                                    GetSysColor(COLOR_3DHIGHLIGHT));
        return S_OK;   // also when the window could not be created
    }
#endif

#ifdef DIME_CPP_PRESENTER
	_pCandidateWnd = std::make_unique <DimeCandidateWindow>(_CandWndCallback, this, _pIndexRange, _pTextService->_IsStoreAppMode());
    if (_pCandidateWnd == nullptr)
    {
        hr = E_OUTOFMEMORY;
        goto Exit;
    }

    HWND parentWndHandle = nullptr;
    ITfContextView* pView = nullptr;
    if (pContextDocument && SUCCEEDED(pContextDocument->GetActiveView(&pView)) && pView)
    {
        pView->GetWnd(&parentWndHandle);
    }

	if (_pCandidateWnd && !_pCandidateWnd->_Create(wndWidth, CConfig::GetFontSize(), parentWndHandle))
    {
        hr = E_OUTOFMEMORY;
        goto Exit;
    }
#endif
	
Exit:
    return hr;
}

void CUIPresenter::DisposeCandidateWindow()
{
#ifdef DIME_RUST_AVAILABLE
    if (_RustPresenter())
    {
        dime_ui_presenter_cand_dispose(_rp);
        return;
    }
#endif
#ifdef DIME_CPP_PRESENTER
    if (_pCandidateWnd)
    {
        _pCandidateWnd->_Destroy();
        _pCandidateWnd.reset(); // Automatically deletes the object
    }
#endif
}

void CUIPresenter::DisposeNotifyWindow(_In_opt_ NOTIFY_TYPE notifyType)
{
#ifdef DIME_RUST_AVAILABLE
    if (_RustPresenter())
    {
        dime_ui_presenter_notify_dispose(_rp, (uint32_t)notifyType);   // NOTIFY_COUNT: every slot
        return;
    }
#endif
#ifdef DIME_CPP_PRESENTER
    if (notifyType == NOTIFY_TYPE::NOTIFY_COUNT)
    {
        // Sentinel: dispose every slot.
        for (auto& w : _pNotifyWnds)
        {
            if (w)
            {
                w->_Destroy();
                w.reset();
            }
        }
        return;
    }
    size_t idx = (size_t)notifyType;
    if (idx >= _pNotifyWnds.size()) return;
    auto& slot = _pNotifyWnds[idx];
    if (slot)
    {
        slot->_Destroy();
        slot.reset();
    }
#endif
}