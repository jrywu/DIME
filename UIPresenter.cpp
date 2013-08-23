//
//
// Derived from Microsoft Sample IME by Jeremy '13,7,17
//
//
//#define DEBUG_PRINT

#include "Private.h"
#include "CandidateWindow.h"
#include "UIPresenter.h"
#include "CompositionProcessorEngine.h"
#include "BaseStructure.h"

//////////////////////////////////////////////////////////////////////
//
// UIPresenter class
//
//////////////////////////////////////////////////////////////////////+---------------------------------------------------------------------------
//
// ctor
//
//----------------------------------------------------------------------------

CUIPresenter::CUIPresenter(_In_ CTSFTTS *pTextService, CCompositionProcessorEngine *pCompositionProcessorEngine) 
	: CTfTextLayoutSink(pTextService)
{

	_pIndexRange = pCompositionProcessorEngine->GetCandidateListIndexRange();

    _parentWndHandle = nullptr;
    _pCandidateWnd = nullptr;
	_pNotifyWnd = nullptr;

    _updatedFlags = 0;

    _uiElementId = (DWORD)-1;
    _isShowMode = TRUE;   // store return value from BeginUIElement

    _pTextService = pTextService;
    _pTextService->AddRef();

    _refCount = 1;

	//intialized the location out of screen to avoid showing before correct tracking position obtained.
	_candLocation.x = -32768;
	_candLocation.y = -32768;
	
	_notifyLocation.x = -32768;
	_notifyLocation.y = -32768;
	
}

//+---------------------------------------------------------------------------
//
// dtor
//
//----------------------------------------------------------------------------

CUIPresenter::~CUIPresenter()
{
    _EndCandidateList();
	DisposeNotifyWindow();
    _pTextService->Release();
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
    CTfTextLayoutSink::Release();

    LONG cr = --_refCount;

    assert(_refCount >= 0);

    if (_refCount == 0)
    {
        delete this;
    }

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
    *pguid = Global::TSFTTSGuidCandUIElement;
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfTSFTTSUIElement::ITfUIElement::Show
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
    if (_pCandidateWnd) _pCandidateWnd->_Show(TRUE);
	if(_pNotifyWnd) _pNotifyWnd->_Show(TRUE);


    return S_OK;
}

HRESULT CUIPresenter::ToHideUIWindows()
{
	debugPrint(L"CUIPresenter::ToHideUIWindows()");
	if (_pCandidateWnd)	_pCandidateWnd->_Show(FALSE);	

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
    *pIsShow = _pCandidateWnd->_IsWindowVisible();
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
    if (_pCandidateWnd)
    {
        *pCandidateCount = _pCandidateWnd->_GetCount();
    }
    else
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
    if (_pCandidateWnd)
    {
        *pSelectedCandidateIndex = _pCandidateWnd->_GetSelection();
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
    if (!_pCandidateWnd || (uIndex > _pCandidateWnd->_GetCount()))
    {
        return E_FAIL;
    }

    DWORD candidateLen = 0;
    const WCHAR* pCandidateString = nullptr;

    candidateLen = _pCandidateWnd->_GetCandidateString(uIndex, &pCandidateString);

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
    if (!_pCandidateWnd)
    {
        if (pIndex)
        {
            *pIndex = 0;
        }
        *puPageCnt = 0;
        return S_OK;
    }
	
    return _pCandidateWnd->_GetPageIndex(pIndex, uSize, puPageCnt);
}

//+---------------------------------------------------------------------------
//
// ITfCandidateListUIElement::SetPageIndex
//
//----------------------------------------------------------------------------

STDAPI CUIPresenter::SetPageIndex(UINT *pIndex, UINT uPageCnt)
{
	debugPrint(L"CUIPresenter::SetPageIndex(), index = %d, page count =%d", *pIndex, uPageCnt  );
    if (!_pCandidateWnd)
    {
        return E_FAIL;
    }
    return _pCandidateWnd->_SetPageIndex(pIndex, uPageCnt);
}

//+---------------------------------------------------------------------------
//
// ITfCandidateListUIElement::GetCurrentPage
//
//----------------------------------------------------------------------------

STDAPI CUIPresenter::GetCurrentPage(UINT *puPage)
{
	debugPrint(L"CUIPresenter::GetCurrentPage(), puPage =%d", _pCandidateWnd->_GetCurrentPage(puPage) );
    if (!_pCandidateWnd)
    {
        *puPage = 0;
        return S_OK;
    }
    return _pCandidateWnd->_GetCurrentPage(puPage);
}

//+---------------------------------------------------------------------------
//
// ITfCandidateListUIElementBehavior::SetSelection
// It is related of the mouse clicking behavior upon the suggestion window
//----------------------------------------------------------------------------

STDAPI CUIPresenter::SetSelection(UINT nIndex)
{
	debugPrint(L"CUIPresenter::SetSelection(), nIndex =%d", nIndex);
    if (_pCandidateWnd)
    {
        _pCandidateWnd->_SetSelection(nIndex);
    }

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
    _CandidateChangeNotification(CAND_ITEM_SELECT);
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

HRESULT CUIPresenter::_StartCandidateList(TfClientId tfClientId, _In_ ITfDocumentMgr *pDocumentMgr, _In_ ITfContext *pContextDocument, TfEditCookie ec, _In_ ITfRange *pRangeComposition, UINT wndWidth)
{
	debugPrint(L"\nCUIPresenter::_StartCandidateList()");
	pDocumentMgr;tfClientId;
    HRESULT hr = E_FAIL;
	CStringRange notify;

    if (FAILED(_StartLayout(pContextDocument, ec, pRangeComposition)))
    {
        goto Exit;
    }

    BeginUIElement();

	
    hr = MakeCandidateWindow(pContextDocument, wndWidth);
    if (FAILED(hr))
    {
        goto Exit;
    }

	Show(_isShowMode);

    RECT rcTextExt;
    if (SUCCEEDED(_GetTextExt(&rcTextExt)))
    {
        _LayoutChangeNotification(&rcTextExt);
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
	_UpdateUIElement();

    DisposeCandidateWindow();

    _EndLayout();

}

//+---------------------------------------------------------------------------
//
// _SetText
//
//----------------------------------------------------------------------------

void CUIPresenter::_SetCandidateText(_In_ CTSFTTSArray<CCandidateListItem> *pCandidateList, BOOL isAddFindKeyCode, UINT candWidth)
{
	debugPrint(L"CUIPresenter::_SetCandidateText() candWidth = %d", candWidth);
    AddCandidateToTSFTTSUI(pCandidateList, isAddFindKeyCode);

    SetPageIndexWithScrollInfo(pCandidateList);

	_pCandidateWnd->_SetWidth(candWidth);

	Show(_isShowMode);
    if (_isShowMode)
    {
        _pCandidateWnd->_InvalidateRect();
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

void CUIPresenter::AddCandidateToTSFTTSUI(_In_ CTSFTTSArray<CCandidateListItem> *pCandidateList, BOOL isAddFindKeyCode)
{
    for (UINT index = 0; index < pCandidateList->Count(); index++)
    {
        _pCandidateWnd->_AddString(pCandidateList->GetAt(index), isAddFindKeyCode);
    }
}

void CUIPresenter::SetPageIndexWithScrollInfo(_In_ CTSFTTSArray<CCandidateListItem> *pCandidateList)
{
    UINT candCntInPage = _pIndexRange->Count();
    UINT bufferSize = pCandidateList->Count() / candCntInPage + 1;
    UINT* puPageIndex = new (std::nothrow) UINT[ bufferSize ];
    if (puPageIndex != nullptr)
    {
        for (UINT i = 0; i < bufferSize; i++)
        {
            puPageIndex[i] = i * candCntInPage;
        }

        _pCandidateWnd->_SetPageIndex(puPageIndex, bufferSize);
        delete [] puPageIndex;
    }
    _pCandidateWnd->_SetScrollInfo(pCandidateList->Count(), candCntInPage);  // nMax:range of max, nPage:number of items in page

}
//+---------------------------------------------------------------------------
//
// _ClearList
//
//----------------------------------------------------------------------------

void CUIPresenter::_ClearCandidateList()
{
	if(_pCandidateWnd)
	{
		_pCandidateWnd->_ClearList();
		_pCandidateWnd->_InvalidateRect();
	}
}

//+---------------------------------------------------------------------------
//
// _SetTextColor
// _SetFillColor
//
//----------------------------------------------------------------------------
void CUIPresenter::_SetNotifyTextColor(COLORREF crColor, COLORREF crBkColor)
{
	if(_pNotifyWnd)
	{
	    _pNotifyWnd->_SetTextColor(crColor, crBkColor);
		_pNotifyWnd->_SetFillColor(crBkColor);
	}
}


void CUIPresenter::_SetCandidateNumberColor(COLORREF crColor, COLORREF crBkColor)
{
    _pCandidateWnd->_SetNumberColor(crColor, crBkColor);
}


void CUIPresenter::_SetCandidateTextColor(COLORREF crColor, COLORREF crBkColor)
{
    _pCandidateWnd->_SetTextColor(crColor, crBkColor);
}

void CUIPresenter::_SetCandidateSelectedTextColor(COLORREF crColor, COLORREF crBkColor)
{
    _pCandidateWnd->_SetSelectedTextColor(crColor, crBkColor);
}


void CUIPresenter::_SetCandidateFillColor(COLORREF fiColor)
{
    _pCandidateWnd->_SetFillColor(fiColor);
}

//+---------------------------------------------------------------------------
//
// _GetSelectedCandidateString
//
//----------------------------------------------------------------------------

DWORD_PTR CUIPresenter::_GetSelectedCandidateString(_Outptr_result_maybenull_ const WCHAR **ppwchCandidateString)
{
    return _pCandidateWnd->_GetSelectedCandidateString(ppwchCandidateString);
}

//+---------------------------------------------------------------------------
//
// _MoveSelection
//
//----------------------------------------------------------------------------

BOOL CUIPresenter::_MoveCandidateSelection(_In_ int offSet)
{
    BOOL ret = _pCandidateWnd->_MoveSelection(offSet, TRUE);
    if (ret)
    {
        if (_isShowMode)
        {
            _pCandidateWnd->_InvalidateRect();
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
// _SetSelection
//
//----------------------------------------------------------------------------

BOOL CUIPresenter::_SetCandidateSelection(_In_ int selectedIndex, _In_opt_ BOOL isNotify)
{
	debugPrint(L"CUIPresenter::_SetCandidateSelection(), selectedIndex = %d, iSnotify = %d", selectedIndex, isNotify);
    BOOL ret = _pCandidateWnd->_SetSelection(selectedIndex, isNotify);
    if (ret)
    {
        if (_isShowMode)
        {
            _pCandidateWnd->_InvalidateRect();
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
// _MovePage
//
//----------------------------------------------------------------------------

BOOL CUIPresenter::_MoveCandidatePage(_In_ int offSet)
{
	debugPrint(L"CUIPresenter::_MoveCandidatePage(), offSet = %d", offSet);
    BOOL ret = _pCandidateWnd->_MovePage(offSet, TRUE);
    if (ret)
    {
        if (_isShowMode)
        {
            _pCandidateWnd->_InvalidateRect();
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
	debugPrint(L"CUIPresenter::_MoveCandidateWindowToTextExt()");
    RECT rc;

    if (FAILED(_GetTextExt(&rc)))
    {
        return;
    }
	if(_pCandidateWnd)
	{
		_pCandidateWnd->_Move(rc.left, rc.bottom);
		_candLocation.x = rc.left;
		_candLocation.y = rc.bottom;
	}
   if(_pNotifyWnd)
   {
	   _notifyLocation.x = rc.left;
	   _notifyLocation.y = rc.bottom;
	   _pNotifyWnd->_Move(_notifyLocation.x  - _pNotifyWnd->_GetWidth() , _notifyLocation.y);
   }
}
//+---------------------------------------------------------------------------
//
// _LayoutChangeNotification
//
//----------------------------------------------------------------------------

VOID CUIPresenter::_LayoutChangeNotification(_In_ RECT *lpRect)
{
	debugPrint(L"CUIPresenter::_LayoutChangeNotification(), top = %d, bottom = %d, right = %d, left = %d", lpRect->top, lpRect->bottom, lpRect->right, lpRect->left);
	if(lpRect == nullptr) return;
    RECT rect = {0, 0, 0, 0};
    POINT pt = {0, 0};
	if(_pCandidateWnd
		&& (lpRect->bottom - lpRect->top >1) && (lpRect->right - lpRect->left >1)  ) // confirm the extent rect is valid.
	{
		_pCandidateWnd->_GetClientRect(&rect);
		_pCandidateWnd->_GetWindowExtent(lpRect, &rect, &pt);
		_pCandidateWnd->_Move(pt.x, pt.y);
		_candLocation.x = pt.x;
		_candLocation.y = pt.y;
	}
	if(_pNotifyWnd
		&& (lpRect->bottom - lpRect->top >1) && (lpRect->right - lpRect->left >1)  ) // confirm the extent rect is valid.
	{
		
		if(_pCandidateWnd && _pCandidateWnd->_IsWindowVisible())
			_pNotifyWnd->_Move(pt.x-_pNotifyWnd->_GetWidth(), pt.y);
		else
		{
			_pNotifyWnd->_InvalidateRect();
			_pNotifyWnd->_GetClientRect(&rect);
			_pNotifyWnd->_GetWindowExtent(lpRect, &rect, &pt);
			_pNotifyWnd->_Move(pt.x, pt.y);
		}
		_notifyLocation.x = pt.x;
		_notifyLocation.y = pt.y;
	}
	_rectCompRange = *lpRect;
}

void CUIPresenter::GetCandLocation(POINT *lpPoint)
{
	lpPoint->x = _candLocation.x;
	lpPoint->y = _candLocation.y;
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
// _NotifyChangeNotifiction
//
//----------------------------------------------------------------------------

HRESULT CUIPresenter::_NotifyChangeNotification()
{
	return S_OK;
}

//+---------------------------------------------------------------------------
//
// _CandidateChangeNotifiction
//
//----------------------------------------------------------------------------

HRESULT CUIPresenter::_CandidateChangeNotification(_In_ enum CANDWND_ACTION action)
{
    HRESULT hr = E_FAIL;

    TfClientId tfClientId = _pTextService->_GetClientId();
    ITfThreadMgr* pThreadMgr = nullptr;
    ITfDocumentMgr* pDocumentMgr = nullptr;
    ITfContext* pContext = nullptr;

    _KEYSTROKE_STATE KeyState;
	KeyState.Category = CATEGORY_CANDIDATE;
    KeyState.Function = FUNCTION_FINALIZE_CANDIDATELIST; // select from the UI. send FUNCTION_FINALIZE_CANDIDATELIST to the keyhandler

    if (CAND_ITEM_SELECT != action)
    {
        goto Exit;
    }

    pThreadMgr = _pTextService->_GetThreadMgr();
    if (nullptr == pThreadMgr)
    {
        goto Exit;
    }

    hr = pThreadMgr->GetFocus(&pDocumentMgr);
    if (FAILED(hr))
    {
        goto Exit;
    }

    hr = pDocumentMgr->GetTop(&pContext);
    if (FAILED(hr))
    {
        pDocumentMgr->Release();
        goto Exit;
    }

    CKeyHandlerEditSession *pEditSession = new (std::nothrow) CKeyHandlerEditSession(_pTextService, pContext, 0, 0, KeyState);
    if (nullptr != pEditSession)
    {
        HRESULT hrSession = S_OK;
        hr = pContext->RequestEditSession(tfClientId, pEditSession, TF_ES_SYNC | TF_ES_READWRITE, &hrSession);
        if (hrSession == TF_E_SYNCHRONOUS || hrSession == TS_E_READONLY)
        {
            hr = pContext->RequestEditSession(tfClientId, pEditSession, TF_ES_ASYNC | TF_ES_READWRITE, &hrSession);
        }
        pEditSession->Release();
    }

    pContext->Release();
    pDocumentMgr->Release();

Exit:
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

    return fakeThis->_CandidateChangeNotification(action);
}

//+---------------------------------------------------------------------------
//
// _NotifyWndCallback
//
//----------------------------------------------------------------------------

// static
HRESULT CUIPresenter::_NotifyWndCallback(_In_ void *pv)
{
    CUIPresenter* fakeThis = (CUIPresenter*)pv;

    return fakeThis->_NotifyChangeNotification();
}

//+---------------------------------------------------------------------------
//
// _UpdateUIElement
//
//----------------------------------------------------------------------------

HRESULT CUIPresenter::_UpdateUIElement()
{
	
    HRESULT hr = S_OK;

    ITfThreadMgr* pThreadMgr = _pTextService->_GetThreadMgr();
    if (nullptr == pThreadMgr)
    {
        return S_OK;
    }

    ITfUIElementMgr* pUIElementMgr = nullptr;

    hr = pThreadMgr->QueryInterface(IID_ITfUIElementMgr, (void **)&pUIElementMgr);
    if (hr == S_OK)
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
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// OnKillThreadFocus
//
//----------------------------------------------------------------------------

HRESULT CUIPresenter::OnKillThreadFocus()
{
	debugPrint(L"CUIPresenter::OnSetThreadFocus()");
    if (_isShowMode)
    {
        Show(FALSE);
    }
	if(_pNotifyWnd) _pNotifyWnd->_Show(FALSE);
    return S_OK;
}

void CUIPresenter::RemoveSpecificCandidateFromList(_In_ LCID Locale, _Inout_ CTSFTTSArray<CCandidateListItem> &candidateList, _In_ CStringRange &candidateString)
{
    for (UINT index = 0; index < candidateList.Count();)
    {
        CCandidateListItem* pLI = candidateList.GetAt(index);

        if (CStringRange::Compare(Locale, &candidateString, &pLI->_ItemString) == CSTR_EQUAL)
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
    case FUNCTION_MOVE_UP:
        {
            if(!isUILessMode())_MoveCandidateSelection(MOVEUP_ONE);
			else if(CConfig::GetArrowKeySWPages())  _MoveCandidatePage(MOVEUP_ONE);
            break;
        }
    case FUNCTION_MOVE_DOWN:
        {
            if(!isUILessMode()) _MoveCandidateSelection(MOVEDOWN_ONE);
			else if(CConfig::GetArrowKeySWPages())  _MoveCandidatePage(MOVEDOWN_ONE);
            break;
        }
	case FUNCTION_MOVE_LEFT:
        {
            if(isUILessMode()) _MoveCandidateSelection(MOVEUP_ONE);
			else if(CConfig::GetArrowKeySWPages())  _MoveCandidatePage(MOVEUP_ONE);
            break;
        }
    case FUNCTION_MOVE_RIGHT:
        {
			if(isUILessMode()) _MoveCandidateSelection(MOVEDOWN_ONE);  //UI less mode is horizontal layout
			else if(CConfig::GetArrowKeySWPages())  _MoveCandidatePage(MOVEDOWN_ONE);
            break;
        }
    case FUNCTION_MOVE_PAGE_UP:
        {
            _MoveCandidatePage(MOVEUP_ONE);
            break;
        }
    case FUNCTION_MOVE_PAGE_DOWN:
        {
            _MoveCandidatePage(MOVEDOWN_ONE);
            break;
        }
    case FUNCTION_MOVE_PAGE_TOP:
        {
            _SetCandidateSelection(MOVETO_TOP);
            break;
        }
    case FUNCTION_MOVE_PAGE_BOTTOM:
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

    ITfThreadMgr* pThreadMgr = _pTextService->_GetThreadMgr();
    if ((nullptr == pThreadMgr) || (-1 == _uiElementId))
    {
        hr = E_FAIL;
        goto Exit;
    }

    ITfUIElementMgr* pUIElementMgr = nullptr;
    hr = pThreadMgr->QueryInterface(IID_ITfUIElementMgr, (void **)&pUIElementMgr);
    if (hr == S_OK)
    {
        pUIElementMgr->EndUIElement(_uiElementId);
        pUIElementMgr->Release();
    }

Exit:
	debugPrint(L"CUIPresenter::EndUIElement(), hresult = %d ", hr);
    return hr;
}

HRESULT CUIPresenter::MakeNotifyWindow(_In_ ITfContext *pContextDocument)
{
	HRESULT hr = S_OK;


	if (nullptr == _pNotifyWnd)
    {
		_pNotifyWnd = new (std::nothrow) CNotifyWindow(_NotifyWndCallback, this);
	}

    if (nullptr == _pNotifyWnd)	return S_FALSE;
  
	HWND parentWndHandle = nullptr;
    ITfContextView* pView = nullptr;
    if (pContextDocument && SUCCEEDED(pContextDocument->GetActiveView(&pView)))
    {
        pView->GetWnd(&parentWndHandle);
    }
	
	if (_pNotifyWnd->_GetUIWnd() == nullptr)
	{
		if( !_pNotifyWnd->_Create(CConfig::GetFontSize(), parentWndHandle))
		{
			hr = E_OUTOFMEMORY;
			return hr;
		}
	}
	
	return hr;
    
}

	
void CUIPresenter::SetNotifyText(_In_ CStringRange *pNotifyText)
{
	debugPrint(L"CUIPresenter::SetNotifyText()");
	if (_pNotifyWnd)
	{
		_pNotifyWnd->_SetString(pNotifyText);
		_pNotifyWnd->_InvalidateRect();
	}
}
void CUIPresenter::ShowNotify(_In_ BOOL showMode, _In_opt_ int timeToHide)
{
	debugPrint(L"CUIPresenter::ShowNotify()");
	if (_pNotifyWnd)
		_pNotifyWnd->_Show(showMode, timeToHide);
}
void CUIPresenter::ClearAll()
{
	debugPrint(L"CUIPresenter::ClearAll()");
	ClearNotify();
	DisposeCandidateWindow();
}

void CUIPresenter::ClearNotify()
{
	debugPrint(L"CUIPresenter::ClearNotify()");
	if (_pNotifyWnd)
	{
		if(_pCandidateWnd == nullptr) _EndLayout();
		_pNotifyWnd->_Clear();
	}
	DisposeNotifyWindow(); //recreate the window so as the ITfContext is always from the latest one.
}
void CUIPresenter::ShowNotifyText(_In_ CStringRange *pNotifyText, _In_ int timeToHide, ITfContext* pContext)
{
	debugPrint(L"CUIPresenter::ShowNotifyText()");
	ITfThreadMgr* pThreadMgr = nullptr;
	ITfDocumentMgr* pDocumentMgr = nullptr;
	BOOL shouldProbeComposition = TRUE;

	if (_pNotifyWnd) 
	{
		ClearNotify();
	}

	if(pContext == nullptr)
	{
		pContext = _GetContextDocument();
		if(pContext == nullptr)
		{
			pThreadMgr = _pTextService->_GetThreadMgr();
			if (nullptr != pThreadMgr)
			{
				if (SUCCEEDED(pThreadMgr->GetFocus(&pDocumentMgr)) && pDocumentMgr != nullptr)
				{
					pDocumentMgr->GetTop(&pContext); 
				}
			}
		}
	}
	if(pContext != nullptr)
	{
		if(MakeNotifyWindow(pContext)== S_OK)
		{

			_SetNotifyTextColor(CConfig::GetItemColor(), CConfig::GetItemBGColor());
			SetNotifyText(pNotifyText);
			

			HWND parentWndHandle = nullptr;
			ITfContextView* pView = nullptr;

			if (SUCCEEDED(pContext->GetActiveView(&pView)))
			{
				pView->GetWnd(&parentWndHandle);
				debugPrint(L" parentWndHandle = %x , FocusHwnd = %x, ActiveHwnd =%x, ForeGroundHWnd = %x", parentWndHandle, GetFocus(), GetActiveWindow(), GetForegroundWindow());
				GUITHREADINFO* guiInfo = new GUITHREADINFO;
				POINT* pt = nullptr;
				guiInfo->cbSize = sizeof(GUITHREADINFO);
				GetGUIThreadInfo(NULL, guiInfo);
				if(guiInfo->hwndCaret)
				{   //for acient non TSF aware apps with a floating composition window.  The caret position we can get is always the caret in the flaoting comosition window.
					pt = new POINT;
					pt->x = guiInfo->rcCaret.left;
					pt->y = guiInfo->rcCaret.bottom;
					ClientToScreen(parentWndHandle, pt);
					debugPrint(L"current caret position from GetGUIThreadInfo, x = %d, y = %d, focus hwd = %x", guiInfo->rcCaret.left, guiInfo->rcCaret.bottom);
					if(_notifyLocation.x < 0) _notifyLocation.x = pt->x;
					if(_notifyLocation.y < 0) _notifyLocation.y = pt->y;
				}

				ShowNotify(TRUE, timeToHide);	//hide after 1.5 secconds

				if(_pCandidateWnd == nullptr && shouldProbeComposition)
					_pTextService->_ProbeComposition(pContext);

				if(_pCandidateWnd && _pCandidateWnd->_IsWindowVisible())
				{
					_pNotifyWnd->_Move(_notifyLocation.x - _pNotifyWnd->_GetWidth(), _notifyLocation.y);
				}
				else
					_pNotifyWnd->_Move(_notifyLocation.x, _notifyLocation.y);

			}
		}
		 

	}


}

BOOL CUIPresenter::IsNotifyShown()
{
	if(_pNotifyWnd && _pNotifyWnd->_IsWindowVisible())
		return TRUE;
	else
		return FALSE;
}
BOOL CUIPresenter::IsCandShown()
{
	if(_pCandidateWnd && _pCandidateWnd->_IsWindowVisible())
		return TRUE;
	else
		return FALSE;
}



HRESULT CUIPresenter::MakeCandidateWindow(_In_ ITfContext *pContextDocument, _In_ UINT wndWidth)
{
    HRESULT hr = S_OK;

    if (nullptr != _pCandidateWnd)
    {
        return hr;
    }

    _pCandidateWnd = new (std::nothrow) CCandidateWindow(_CandWndCallback, this, _pIndexRange, _pTextService->_IsStoreAppMode());
    if (_pCandidateWnd == nullptr)
    {
        hr = E_OUTOFMEMORY;
        goto Exit;
    }

    HWND parentWndHandle = nullptr;
    ITfContextView* pView = nullptr;
    if (SUCCEEDED(pContextDocument->GetActiveView(&pView)))
    {
        pView->GetWnd(&parentWndHandle);
    }

	if (!_pCandidateWnd->_Create(wndWidth, CConfig::GetFontSize(), parentWndHandle))
    {
        hr = E_OUTOFMEMORY;
        goto Exit;
    }
	
Exit:
    return hr;
}

void CUIPresenter::DisposeCandidateWindow()
{
    if (nullptr != _pCandidateWnd)
    {
        _pCandidateWnd->_Destroy();
		 delete _pCandidateWnd;
		 _pCandidateWnd = nullptr;
    }
}
void CUIPresenter::DisposeNotifyWindow()
{
	if (nullptr != _pNotifyWnd)
	{
		_pNotifyWnd->_Destroy();
		delete _pNotifyWnd;
		_pNotifyWnd = nullptr;
	}
}