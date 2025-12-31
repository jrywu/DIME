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
#include "CandidateWindow.h"
#include "UIPresenter.h"
#include "CompositionProcessorEngine.h"
#include "BaseStructure.h"
#include <memory> // Include for smart pointers

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
    : CTfTextLayoutSink(pTextService), _pCandidateWnd(nullptr), _pNotifyWnd(nullptr), _Category(KEYSTROKE_CATEGORY::CATEGORY_NONE), _pIndexRange(nullptr)
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

    // Initialize locations off-screen
    _candLocation = { -32768, -32768 };
    _notifyLocation = { -32768, -32768 };
    _rectCompRange = { -32768, -32768, -32768, -32768 };
    _inFocus = FALSE;
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
    if(_pCandidateWnd) _pCandidateWnd->_Show(TRUE);
	if(_pNotifyWnd && _pNotifyWnd->GetNotifyType() == NOTIFY_TYPE::NOTIFY_OTHERS)
	{
		_pNotifyWnd->_Show(TRUE);
	}


    return S_OK;
}

HRESULT CUIPresenter::ToHideUIWindows()
{
	debugPrint(L"CUIPresenter::ToHideUIWindows()");
	if (_pCandidateWnd)	_pCandidateWnd->_Show(FALSE);	
	if(_pNotifyWnd)
	{
		ClearNotify();
	}

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
	if(_pCandidateWnd)
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
		INT selection = _pCandidateWnd->_GetSelection();
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
    if (_pCandidateWnd == nullptr || (uIndex > _pCandidateWnd->_GetCount()))
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
    if (_pCandidateWnd == nullptr)
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
    if (_pCandidateWnd == nullptr)
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
    if (_pCandidateWnd == nullptr)
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

HRESULT CUIPresenter::_StartCandidateList(TfClientId tfClientId, _In_ ITfDocumentMgr *pDocumentMgr, _In_ ITfContext *pContextDocument, TfEditCookie ec, _In_ ITfRange *pRangeComposition, UINT wndWidth)
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

	
    hr = MakeCandidateWindow(pContextDocument, wndWidth);
    if (FAILED(hr))
    {
        goto Exit;
    }

	Show(_isShowMode);


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

void CUIPresenter::_SetCandidateText(_In_ CDIMEArray<CCandidateListItem> *pCandidateList,_In_ CCandidateRange* pIndexRange, BOOL isAddFindKeyCode, UINT candWidth)
{
	debugPrint(L"CUIPresenter::_SetCandidateText() candWidth = %d", candWidth);

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

	if(_pCandidateWnd)
		_pCandidateWnd->_SetCandStringLength(candWidth);

	Show(_isShowMode);
    if (_isShowMode && _pCandidateWnd)
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

void CUIPresenter::AddCandidateToUI(_In_ CDIMEArray<CCandidateListItem> *pCandidateList, BOOL isAddFindKeyCode)
{

	debugPrint(L"UIPresenter:AddCandidateToUI()");
	if(pCandidateList == nullptr || _pCandidateWnd == nullptr) return;
    for (UINT index = 0; index < pCandidateList->Count(); index++)
    {
		_pCandidateWnd->_AddString(pCandidateList->GetAt(index), isAddFindKeyCode);
    }

}

void CUIPresenter::SetPageIndexWithScrollInfo(_In_ CDIMEArray<CCandidateListItem> *pCandidateList)
{
    if (!_pIndexRange || !pCandidateList || !_pCandidateWnd)
        return;

    _pCandidateWnd->_SetCandIndexRange(_pIndexRange);

    UINT candCntInPage = _pIndexRange->Count();
    UINT bufferSize = pCandidateList->Count() / candCntInPage + 1;
    std::vector<UINT> pageIndex(bufferSize);

    for (UINT i = 0; i < bufferSize; i++)
    {
        pageIndex[i] = i * candCntInPage;
    }

    _pCandidateWnd->_SetPageIndex(pageIndex.data(), bufferSize);
    _pCandidateWnd->_SetScrollInfo(pCandidateList->Count(), candCntInPage);
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
		_pNotifyWnd->_InvalidateRect();
	}
}


void CUIPresenter::_SetCandidateNumberColor(COLORREF crColor, COLORREF crBkColor)
{
	if(_pCandidateWnd)
		_pCandidateWnd->_SetNumberColor(crColor, crBkColor);
}


void CUIPresenter::_SetCandidateTextColor(COLORREF crColor, COLORREF crBkColor)
{
	if(_pCandidateWnd)
		_pCandidateWnd->_SetTextColor(crColor, crBkColor);
}

void CUIPresenter::_SetCandidateSelectedTextColor(COLORREF crColor, COLORREF crBkColor)
{
	if(_pCandidateWnd)
		_pCandidateWnd->_SetSelectedTextColor(crColor, crBkColor);
}


void CUIPresenter::_SetCandidateFillColor(COLORREF fiColor)
{
	if(_pCandidateWnd)
		_pCandidateWnd->_SetFillColor(fiColor);
}


// +-------------------------------------------------------------------------- -
//
// _GetSelectedCandidateKeyCode
//
//----------------------------------------------------------------------------

DWORD_PTR CUIPresenter::_GetSelectedCandidateKeyCode(_Inout_ const WCHAR **ppwchCandidateString)
{
	if (_pCandidateWnd)
		return _pCandidateWnd->_GetSelectedCandidateKeyCode(ppwchCandidateString);
	else
		return 0;
}


//+---------------------------------------------------------------------------
//
// _GetSelectedCandidateString
//
//----------------------------------------------------------------------------

DWORD_PTR CUIPresenter::_GetSelectedCandidateString(_Inout_ const WCHAR **ppwchCandidateString)
{
	if(_pCandidateWnd)
		return _pCandidateWnd->_GetSelectedCandidateString(ppwchCandidateString);
	else
		return 0;
}

//+---------------------------------------------------------------------------
//
// _MoveSelection
//
//----------------------------------------------------------------------------

BOOL CUIPresenter::_MoveCandidateSelection(_In_ int offSet)
{
	if(_pCandidateWnd == nullptr) return FALSE;
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
// _SetCandidateSelection
//
//----------------------------------------------------------------------------

BOOL CUIPresenter::_SetCandidateSelection(_In_ int selectedIndex, _In_opt_ BOOL isNotify)
{
	debugPrint(L"CUIPresenter::_SetCandidateSelection(), selectedIndex = %d, isNotify = %d", selectedIndex, isNotify);
	if(_pCandidateWnd == nullptr) return FALSE;
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
// _GetCandidateSelection
//
//----------------------------------------------------------------------------
INT CUIPresenter::_GetCandidateSelection()
{	
	if (_pCandidateWnd)
		return _pCandidateWnd->_GetSelection();
	else
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
	if(_pCandidateWnd == nullptr) return FALSE;
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
	debugPrint(L"CUIPresenter::_MoveUIWindowToTextExt()");
	RECT compRect = { 0,0,0,0 };

	if (SUCCEEDED(_GetTextExt(&compRect)))
	{
		debugPrint(L"CUIPresenter::_MoveUIWindowToTextExt(), top = %d, bottom = %d, right = %d, left = %d", compRect.top, compRect.bottom, compRect.right, compRect.left);
		_LayoutChangeNotification(&compRect, true);
	}
	else
		_LayoutChangeNotification(&_rectCompRange, true);

	
	
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
	debugPrint(L"CUIPresenter::_LayoutChangeNotification(), top = %d, bottom = %d, right = %d, left = %d", lpRect->top, lpRect->bottom, lpRect->right, lpRect->left);
	if(lpRect == nullptr) return;

	RECT compRect = * lpRect;
    RECT candRect = {0, 0, 0, 0};
	RECT notifyRect = {0, 0, 0, 0};
    POINT candPt = {0, 0};
	POINT notifyPt = {0, 0};

	ITfContext *pContext =  _GetContextDocument();
	ITfContextView * pView = nullptr;
	HWND parentWndHandle;
	POINT caretPt = { 0, 0 };
	
	if (pContext && SUCCEEDED(pContext->GetActiveView(&pView)))
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
		GetCaretPos(&caretPt);
		ClientToScreen(parentWndHandle, &caretPt);
		debugPrint(L"current caret position from GetCaretPos, x = %d, y = %d", caretPt.x, caretPt.y);
		// Recreate font for fitting current monitor resolution and font size
		if (Global::isWindows8) CConfig::SetDefaultTextFont(parentWndHandle);

	}
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
		else if (firstCall && parentWndHandle)
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
	if (_pNotifyWnd && lpRect)
	{
		// Recreate font for fitting current monitor resolution and font size
		//if (Global::isWindows8) CConfig::SetDefaultTextFont(_pNotifyWnd->_GetWnd());
		if (lpRect->bottom - lpRect->top > 1 || lpRect->right - lpRect->left > 1)   // confirm the extent rect is valid.
		{

			if (_pCandidateWnd && _pCandidateWnd->_IsWindowVisible())
			{
				/*
				debugPrint(L"notify width = %d, candwidth = %d", _pNotifyWnd->_GetWidth(), _pCandidateWnd->_GetWidth());
				if (candPt.x < (int)_pNotifyWnd->_GetWidth())
				{
					_pNotifyWnd->_Move(candPt.x + _pCandidateWnd->_GetWidth(), candPt.y);
					debugPrint(L"move notify to x = %d, y = %d", candPt.x + _pCandidateWnd->_GetWidth(), candPt.y);
				}
				else
				{
					_pNotifyWnd->_Move(candPt.x - _pNotifyWnd->_GetWidth(), candPt.y);
					debugPrint(L"move notify to x = %d, y = %d", candPt.x - _pNotifyWnd->_GetWidth(), candPt.y);
				}
				_notifyLocation.x = candPt.x;
				_notifyLocation.y = candPt.y;
				*/
				debugPrint(L"notify width = %d, candwidth = %d", _pNotifyWnd->_GetWidth(), _pCandidateWnd->_GetWidth());
				// cand window is on the top of caret
				if (candPt.y < lpRect->top) 
					_notifyLocation.y = candPt.y + (candRect.bottom - candRect.top) - _pNotifyWnd->_GetHeight();
				else 
					_notifyLocation.y = candPt.y;

				if (candPt.x < (int)_pNotifyWnd->_GetWidth())
					_notifyLocation.x = candPt.x + _pCandidateWnd->_GetWidth();
				else
					_notifyLocation.x = candPt.x - _pNotifyWnd->_GetWidth();
					
				_pNotifyWnd->_Move(_notifyLocation.x, _notifyLocation.y);
				debugPrint(L"move notify to x = %d, y = %d", _notifyLocation.x, _notifyLocation.y);
				_notifyLocation.x = candPt.x;
			}
			else
			{
				_pNotifyWnd->_GetClientRect(&notifyRect);
				if (((compRect.right - compRect.left) > (notifyRect.right - notifyRect.left)) && caretPt.x < compRect.right && caretPt.x >= compRect.left)
					compRect.left = caretPt.x;

				_pNotifyWnd->_GetWindowExtent(&compRect, &notifyRect, &notifyPt);
				_pNotifyWnd->_Move(notifyPt.x, notifyPt.y);
				_notifyLocation.x = notifyPt.x;
				_notifyLocation.y = notifyPt.y;
				debugPrint(L"move notify to x = %d, y = %d", notifyPt.x, notifyPt.y);
			}

		}
	}
	_rectCompRange = compRect;
}

void CUIPresenter::GetCandLocation(_Out_ POINT *lpPoint) const
{
	if(lpPoint)
	{
		lpPoint->x = _candLocation.x;
		lpPoint->y = _candLocation.y;
	}
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
			if((NOTIFY_TYPE)lParam == NOTIFY_TYPE::NOTIFY_CHN_ENG && !_pTextService->_IsComposing())
			{
				if(_pTextService && _GetContextDocument() == nullptr)  //layout is not started. we need to do probecomposition to start layout
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
								ShowNotify(TRUE, 0, (UINT) wParam);
								_pTextService->_ProbeComposition(pContext);
							}

						}
					}
				}
			}
			else if((NOTIFY_TYPE)lParam == NOTIFY_TYPE::NOTIFY_OTHERS || (_pCandidateWnd && !_pCandidateWnd->_IsWindowVisible()))
				ShowNotify(TRUE, 0, (UINT) wParam);
			break;
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

    // Initialize KeyState to avoid uninitialized variable usage
    _KEYSTROKE_STATE KeyState = { KEYSTROKE_CATEGORY::CATEGORY_NONE, KEYSTROKE_FUNCTION::FUNCTION_NONE };

    KeyState.Category = KEYSTROKE_CATEGORY::CATEGORY_CANDIDATE;
    // Select from the UI. Send FUNCTION_FINALIZE_CANDIDATELIST or FUNCTION_CANCEL to the key handler
    KeyState.Function = (CANDWND_ACTION::CAND_CANCEL == action) ? KEYSTROKE_FUNCTION::FUNCTION_CANCEL : KEYSTROKE_FUNCTION::FUNCTION_FINALIZE_CANDIDATELIST;

    if (!(CANDWND_ACTION::CAND_ITEM_SELECT == action || CANDWND_ACTION::CAND_CANCEL == action))
    {
        goto Exit;
    }

    if (pContext == nullptr)
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
            pDocumentMgr->Release();
            goto Exit;
        }
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

    if (pContext)
        pContext->Release();
    if (pDocumentMgr)
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
	if (_pCandidateWnd && _pCandidateWnd->_IsWindowVisible())
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
	HRESULT hr = S_OK;


	if (nullptr == _pNotifyWnd)
    {
		_pNotifyWnd = std::make_unique<CNotifyWindow>(_NotifyWndCallback, this, notifyType);
	}

    if (nullptr == _pNotifyWnd)	return S_FALSE;
  
	HWND parentWndHandle = nullptr;
    ITfContextView* pView = nullptr;
    if (pContextDocument && SUCCEEDED(pContextDocument->GetActiveView(&pView)) && pView)
    {
        pView->GetWnd(&parentWndHandle);
    }
	
	if (_pNotifyWnd && _pNotifyWnd->_GetUIWnd() == nullptr)
	{
		if( !_pNotifyWnd->_Create(CConfig::GetFontSize(), parentWndHandle, notifyText))
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

	}
}
void CUIPresenter::ShowNotify(_In_ BOOL showMode,  _In_opt_ UINT delayShow, _In_opt_ UINT timeToHide)
{
	debugPrint(L"CUIPresenter::ShowNotify()");
	if (_pNotifyWnd)
		_pNotifyWnd->_Show(showMode, delayShow, timeToHide);
}
void CUIPresenter::ClearAll()
{
	debugPrint(L"CUIPresenter::ClearAll()");
	if(_pNotifyWnd)
		ClearNotify();
	if(_pCandidateWnd)
		_EndCandidateList();
}

void CUIPresenter::ClearNotify()
{
	debugPrint(L"CUIPresenter::ClearNotify()");
	if (_pNotifyWnd)
	{
		if(_GetContextDocument() && _pCandidateWnd == nullptr) //cand is not here. the layoutsink was start by notify, and we need to end here.
			_EndLayout();

		DisposeNotifyWindow(); //recreate the window so as the ITfContext is always from the latest one.
	}
	
}
void CUIPresenter::ShowNotifyText(_In_ CStringRange* pNotifyText, _In_opt_ UINT delayShow, _In_opt_ UINT timeToHide, _In_opt_ NOTIFY_TYPE notifyType)
{
	//if(pNotifyText) debugPrint(L"CUIPresenter::ShowNotifyText(): text = %s, delayShow = %d, timeTimeHide = %d, notifyType= %d, _inFocus = %d, ", pNotifyText->Get(), delayShow, timeToHide, notifyType, _inFocus);
	ITfContext* pContext = _GetContextDocument();
	ITfThreadMgr* pThreadMgr = nullptr;
	ITfDocumentMgr* pDocumentMgr = nullptr;


	if (_pNotifyWnd) 
	{
		if (notifyType == NOTIFY_TYPE::NOTIFY_CHN_ENG && _pNotifyWnd->GetNotifyType() == NOTIFY_TYPE::NOTIFY_OTHERS)
			return;
		else
			ClearNotify();
	}



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
		
			_SetNotifyTextColor(CConfig::GetItemColor(), CConfig::GetItemBGColor());	

			HWND parentWndHandle = nullptr;
			ITfContextView* pView = nullptr;

			if (SUCCEEDED(pContext->GetActiveView(&pView)) && pView)
			{
				pView->GetWnd(&parentWndHandle);
				debugPrint(L" parentWndHandle = %x , FocusHwnd = %x, ActiveHwnd =%x, ForeGroundHWnd = %x", parentWndHandle, GetFocus(), GetActiveWindow(), GetForegroundWindow());
				
				// Use stack allocation instead of heap allocation to avoid memory leaks
				GUITHREADINFO guiInfo = {0};
				guiInfo.cbSize = sizeof(GUITHREADINFO);
				
				if(GetGUIThreadInfo(NULL, &guiInfo) && parentWndHandle) 
				{   //for ancient non TSF aware apps with a floating composition window.  The caret position we can get is always the caret in the floating composition window.
					POINT pt;
					pt.x = guiInfo.rcCaret.left;
					pt.y = guiInfo.rcCaret.bottom;
					ClientToScreen(parentWndHandle, &pt);
					debugPrint(L"current caret position from GetGUIThreadInfo, x = %d, y = %d", pt.x, pt.y);
					if(_notifyLocation.x < 0) _notifyLocation.x = pt.x;
					if(_notifyLocation.y < 0) _notifyLocation.y = pt.y;
				}
				else if(parentWndHandle)
				{
					POINT caretPt;
					GetCaretPos(&caretPt);
					ClientToScreen(parentWndHandle, &caretPt);
					debugPrint(L"current caret position from GetCaretPos, x = %d, y = %d", caretPt.x, caretPt.y);
					if (_notifyLocation.x < 0) _notifyLocation.x = caretPt.x;
					if (_notifyLocation.y < 0) _notifyLocation.y = caretPt.y;

				}
				

				ShowNotify(TRUE, delayShow, timeToHide);	

				if(delayShow == 0)
				{
					if(_pCandidateWnd && _pNotifyWnd && _pCandidateWnd->_IsWindowVisible())
					{
						if( _notifyLocation.x  < (int) _pNotifyWnd->_GetWidth() )
							_pNotifyWnd->_Move(_notifyLocation.x  + _pCandidateWnd->_GetWidth() , _notifyLocation.y);
						else		
							_pNotifyWnd->_Move(_notifyLocation.x  - _pNotifyWnd->_GetWidth() , _notifyLocation.y);
					}
					else
						_pNotifyWnd->_Move(_notifyLocation.x, _notifyLocation.y);
				}
				//means TextLayoutSink is not working. We need to ProbeComposition to start layout
				if(_pTextService && delayShow == 0 && _GetContextDocument() == nullptr && notifyType == NOTIFY_TYPE::NOTIFY_CHN_ENG )
					_pTextService->_ProbeComposition(pContext);

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

    if (nullptr != _pCandidateWnd || _pTextService == nullptr)
    {
        hr = E_FAIL;
		goto Exit;
    }

	_pCandidateWnd = std::make_unique <CCandidateWindow>(_CandWndCallback, this, _pIndexRange, _pTextService->_IsStoreAppMode());
    if (_pCandidateWnd == nullptr)
    {
        hr = E_OUTOFMEMORY;
        goto Exit;
    }

    HWND parentWndHandle = nullptr;
    ITfContextView* pView = nullptr;
    if (SUCCEEDED(pContextDocument->GetActiveView(&pView)) && pView)
    {
        pView->GetWnd(&parentWndHandle);
    }

	if (_pCandidateWnd && !_pCandidateWnd->_Create(wndWidth, CConfig::GetFontSize(), parentWndHandle))
    {
        hr = E_OUTOFMEMORY;
        goto Exit;
    }
	
Exit:
    return hr;
}

void CUIPresenter::DisposeCandidateWindow()
{
    if (_pCandidateWnd)
    {
        _pCandidateWnd->_Destroy();
        _pCandidateWnd.reset(); // Automatically deletes the object
    }
}

void CUIPresenter::DisposeNotifyWindow()
{
    if (_pNotifyWnd)
    {
        _pNotifyWnd->_Destroy();
        _pNotifyWnd.reset(); // Automatically deletes the object
    }
}