//
//
// Derived from Microsoft Sample IME by Jeremy '13,7,17
//
//


#include "Private.h"
#include "TSFDayi.h"
#include "CandidateWindow.h"
#include "TSFDayiUIPresenter.h"
#include "CompositionProcessorEngine.h"
#include "TSFDayiBaseStructure.h"

//////////////////////////////////////////////////////////////////////
//
// CTSFDayi candidate key handler methods
//
//////////////////////////////////////////////////////////////////////

const int MOVEUP_ONE = -1;
const int MOVEDOWN_ONE = 1;
const int MOVETO_TOP = 0;
const int MOVETO_BOTTOM = -1;
//+---------------------------------------------------------------------------
//
// _HandleCandidateFinalize
//
//----------------------------------------------------------------------------

HRESULT CTSFDayi::_HandleCandidateFinalize(TfEditCookie ec, _In_ ITfContext *pContext)
{
	return _HandleCandidateWorker(ec, pContext);
}

//+---------------------------------------------------------------------------
//
// _HandleCandidateConvert
//
//----------------------------------------------------------------------------

HRESULT CTSFDayi::_HandleCandidateConvert(TfEditCookie ec, _In_ ITfContext *pContext)
{
    return _HandleCandidateWorker(ec, pContext);
	
}

//+---------------------------------------------------------------------------
//
// _HandleCandidateWorker
//
//----------------------------------------------------------------------------

HRESULT CTSFDayi::_HandleCandidateWorker(TfEditCookie ec, _In_ ITfContext *pContext)
{
	OutputDebugString(L"CTSFDayi::_HandleCandidateWorker() \n");
    HRESULT hr = S_OK;
	CStringRange commitString;
	CTSFDayiArray<CCandidateListItem> candidatePhraseList;	
	CStringRange candidateString;
	
    if (nullptr == _pTSFDayiUIPresenter)
    {
        goto Exit; //should not happen
    }
	
	const WCHAR* pCandidateString = nullptr;
	DWORD_PTR candidateLen = 0;    

	if (!_IsComposing())
		_StartComposition(pContext);

	candidateLen = _pTSFDayiUIPresenter->_GetSelectedCandidateString(&pCandidateString);
	if (candidateLen == 0)
    {
		if(_candidateMode == CANDIDATE_WITH_NEXT_COMPOSITION || _candidateMode == CANDIDATE_PHRASE)
		{
			_HandleCancel(ec, pContext);
			goto Exit;
		}
		else
		{
			hr = S_FALSE;
			MessageBeep(MB_ICONASTERISK); //beep for no valid mapping found
			goto Exit;
		}
    }
	
	
	commitString.Set(pCandidateString , candidateLen );
	
	PWCHAR pwch = new (std::nothrow) WCHAR[2];  // pCandidateString will be destroyed after _detelteCanddiateList was called.
	pwch[1] = L'0';
	if(candidateLen > 1)
	{	
		StringCchCopyN(pwch, 2, pCandidateString + candidateLen -1, 1); 
		//candidateString.Set(pwch + candidateLen -1 , 1 );
	}else // cnadidateLen ==1
	{
		StringCchCopyN(pwch, 2, pCandidateString, 1); 	
	}
	candidateString.Set(pwch, 1 );

	hr = _AddComposingAndChar(ec, pContext, &commitString);
	if (FAILED(hr))	return hr;
	
	_HandleComplete(ec, pContext);
	
	

	BOOL fMakePhraseFromText = _pCompositionProcessorEngine->IsMakePhraseFromText();
	if (fMakePhraseFromText)
	{
		_pCompositionProcessorEngine->GetCandidateStringInConverted(candidateString, &candidatePhraseList);
		LCID locale = _pCompositionProcessorEngine->GetLocale();

		_pTSFDayiUIPresenter->RemoveSpecificCandidateFromList(locale, candidatePhraseList, candidateString);
	}
	
	// We have a candidate list if candidatePhraseList.Cnt is not 0
	// If we are showing reverse conversion, use CTSFDayiUIPresenter
	CANDIDATE_MODE tempCandMode = CANDIDATE_NONE;
	CTSFDayiUIPresenter* _pPhraseTSFDayiUIPresenter = nullptr;
	
	if (candidatePhraseList.Count())
	{
		tempCandMode =  CANDIDATE_PHRASE; // CANDIDATE_WITH_NEXT_COMPOSITION;

		_pPhraseTSFDayiUIPresenter = new (std::nothrow) CTSFDayiUIPresenter(this, Global::AtomCandidateWindow,
			CATEGORY_CANDIDATE,
			_pCompositionProcessorEngine->GetCandidateListIndexRange(),
			FALSE);
		if (nullptr == _pPhraseTSFDayiUIPresenter)
		{
			hr = E_OUTOFMEMORY;
			goto Exit;
		}
	}
	else
		goto Exit;
	
	// call _Start*Line for CTSFDayiUIPresenter or CReadingLine
	// we don't cache the document manager object so get it from pContext.
	ITfDocumentMgr* pDocumentMgr = nullptr;
	HRESULT hrStartCandidateList = E_FAIL;
	if (pContext->GetDocumentMgr(&pDocumentMgr) == S_OK)
	{
		ITfRange* pRange = nullptr;
		CStringRange emptyComposition;
		if (!_IsComposing())
			_StartComposition(pContext);  //StartCandidateList require a valid selection from a valid pComposition to determine the location to show the candidate window
		
		// add a space character to empty composition buffer so as the phrase cand can showed in right position in non TSF award program.
		_AddComposingAndChar(ec, pContext, &emptyComposition.Set(L" ",1)); 
		if (_pComposition->GetRange(&pRange) == S_OK)
		{
			if (_pPhraseTSFDayiUIPresenter)
			{
				hrStartCandidateList = _pPhraseTSFDayiUIPresenter->_StartCandidateList(_tfClientId, pDocumentMgr, pContext, ec, pRange, _pCompositionProcessorEngine->GetCandidateWindowWidth());
			} 

			pRange->Release();
			
		}
		//_TerminateComposition(ec, pContext);
		pDocumentMgr->Release();
	}
	
	
	

	// set up candidate list if it is being shown
	if (SUCCEEDED(hrStartCandidateList))
	{
		_pPhraseTSFDayiUIPresenter->_SetTextColor(RGB(0, 0x80, 0), GetSysColor(COLOR_WINDOW));    // Text color is green
		_pPhraseTSFDayiUIPresenter->_SetFillColor((HBRUSH)(COLOR_WINDOW+1));    // Background color is window
		_pPhraseTSFDayiUIPresenter->_SetText(&candidatePhraseList, FALSE);

		
		// close candidate list
		if (_pTSFDayiUIPresenter)
		{
			_pTSFDayiUIPresenter->_EndCandidateList();
			delete _pTSFDayiUIPresenter;
			_pTSFDayiUIPresenter = nullptr;

			_candidateMode = CANDIDATE_NONE;
			_isCandidateWithWildcard = FALSE;
		}

		if (hr == S_OK)
		{
			// copy temp candidate
			_pTSFDayiUIPresenter = _pPhraseTSFDayiUIPresenter;
			_pTSFDayiUIPresenter->_SetSelection(-1); // set selected index to -1 if showing phrase candidates

			_phraseCandShowing = TRUE;
			OutputDebugString(L"CTSFDayi::_HandleCandidateWorker(); _phraseCandShowing = TRUE. phrase cand is showing\n");

			_candidateMode = tempCandMode;
			_isCandidateWithWildcard = FALSE;
		}
		
	}
	_pPhraseTSFDayiUIPresenter = nullptr;

	// no next phrase list, exit without doing anything.
	
Exit:
    return hr;
}

//+---------------------------------------------------------------------------
//
// _HandleCandidateArrowKey
//
//----------------------------------------------------------------------------

HRESULT CTSFDayi::_HandleCandidateArrowKey(TfEditCookie ec, _In_ ITfContext *pContext, _In_ KEYSTROKE_FUNCTION keyFunction)
{
    ec;
    pContext;

    _pTSFDayiUIPresenter->AdviseUIChangedByArrowKey(keyFunction);

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _HandleCandidateSelectByNumber
//
//----------------------------------------------------------------------------

HRESULT CTSFDayi::_HandleCandidateSelectByNumber(TfEditCookie ec, _In_ ITfContext *pContext, _In_ UINT uCode)
{
	int iSelectAsNumber = _pCompositionProcessorEngine->GetCandidateListIndexRange()->GetIndex(uCode, _candidateMode);
    if (iSelectAsNumber == -1)
    {
        return S_FALSE;
    }

    if (_pTSFDayiUIPresenter)
    {
        if (_pTSFDayiUIPresenter->_SetSelectionInPage(iSelectAsNumber))
        {
            return _HandleCandidateConvert(ec, pContext);
        }
    }

    return S_FALSE;
}

//+---------------------------------------------------------------------------
//
// _HandlePhraseFinalize
//
//----------------------------------------------------------------------------

HRESULT CTSFDayi::_HandlePhraseFinalize(TfEditCookie ec, _In_ ITfContext *pContext)
{
    HRESULT hr = S_OK;

    DWORD phraseLen = 0;
    const WCHAR* pPhraseString = nullptr;

    phraseLen = (DWORD)_pTSFDayiUIPresenter->_GetSelectedCandidateString(&pPhraseString);

    CStringRange phraseString;
    phraseString.Set(pPhraseString, phraseLen);

    if (phraseLen)
    {
        if ((hr = _AddCharAndFinalize(ec, pContext, &phraseString)) != S_OK)
        {
            return hr;
        }
    }

    _HandleComplete(ec, pContext);

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _HandlePhraseArrowKey
//
//----------------------------------------------------------------------------

HRESULT CTSFDayi::_HandlePhraseArrowKey(TfEditCookie ec, _In_ ITfContext *pContext, _In_ KEYSTROKE_FUNCTION keyFunction)
{
    ec;
    pContext;

    _pTSFDayiUIPresenter->AdviseUIChangedByArrowKey(keyFunction);

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _HandlePhraseSelectByNumber
//
//----------------------------------------------------------------------------

HRESULT CTSFDayi::_HandlePhraseSelectByNumber(TfEditCookie ec, _In_ ITfContext *pContext, _In_ UINT uCode)
{
	int iSelectAsNumber = _pCompositionProcessorEngine->GetCandidateListIndexRange()->GetIndex(uCode, _candidateMode);
    if (iSelectAsNumber == -1)
    {
        return S_FALSE;
    }

    if (_pTSFDayiUIPresenter)
    {
        if (_pTSFDayiUIPresenter->_SetSelectionInPage(iSelectAsNumber))
        {
            return _HandlePhraseFinalize(ec, pContext);
        }
    }

    return S_FALSE;
}

//////////////////////////////////////////////////////////////////////
//
// CTSFDayiUIPresenter class
//
//////////////////////////////////////////////////////////////////////+---------------------------------------------------------------------------
//
// ctor
//
//----------------------------------------------------------------------------

CTSFDayiUIPresenter::CTSFDayiUIPresenter(_In_ CTSFDayi *pTextService, ATOM atom, KEYSTROKE_CATEGORY Category, _In_ CCandidateRange *pIndexRange, BOOL hideWindow) : CTfTextLayoutSink(pTextService)
{
    _atom = atom;

    _pIndexRange = pIndexRange;

    _parentWndHandle = nullptr;
    _pCandidateWnd = nullptr;

    _Category = Category;

    _updatedFlags = 0;

    _uiElementId = (DWORD)-1;
    _isShowMode = TRUE;   // store return value from BeginUIElement
    _hideWindow = hideWindow;     // Hide window flag from [Configuration] CandidateList.Phrase.HideWindow

    _pTextService = pTextService;
    _pTextService->AddRef();

    _refCount = 1;
}

//+---------------------------------------------------------------------------
//
// dtor
//
//----------------------------------------------------------------------------

CTSFDayiUIPresenter::~CTSFDayiUIPresenter()
{
    _EndCandidateList();
    _pTextService->Release();
}

//+---------------------------------------------------------------------------
//
// ITfTSFDayiUIElement::IUnknown::QueryInterface
//
//----------------------------------------------------------------------------

STDAPI CTSFDayiUIPresenter::QueryInterface(REFIID riid, _Outptr_ void **ppvObj)
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
// ITfTSFDayiUIElement::IUnknown::AddRef
//
//----------------------------------------------------------------------------

STDAPI_(ULONG) CTSFDayiUIPresenter::AddRef()
{
    CTfTextLayoutSink::AddRef();
    return ++_refCount;
}

//+---------------------------------------------------------------------------
//
// ITfTSFDayiUIElement::IUnknown::Release
//
//----------------------------------------------------------------------------

STDAPI_(ULONG) CTSFDayiUIPresenter::Release()
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
// ITfTSFDayiUIElement::ITfUIElement::GetDescription
//
//----------------------------------------------------------------------------

STDAPI CTSFDayiUIPresenter::GetDescription(BSTR *pbstr)
{
    if (pbstr)
    {
        *pbstr = SysAllocString(L"Cand");
    }
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfTSFDayiUIElement::ITfUIElement::GetGUID
//
//----------------------------------------------------------------------------

STDAPI CTSFDayiUIPresenter::GetGUID(GUID *pguid)
{
    *pguid = Global::TSFDayiGuidCandUIElement;
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfTSFDayiUIElement::ITfUIElement::Show
//
//----------------------------------------------------------------------------

STDAPI CTSFDayiUIPresenter::Show(BOOL showCandidateWindow)
{
    if (showCandidateWindow)
    {
        ToShowCandidateWindow();
    }
    else
    {
        ToHideCandidateWindow();
    }
    return S_OK;
}

HRESULT CTSFDayiUIPresenter::ToShowCandidateWindow()
{
    if (_hideWindow)
    {
        _pCandidateWnd->_Show(FALSE);
    }
    else
    {
        _MoveWindowToTextExt();

        _pCandidateWnd->_Show(TRUE);
    }

    return S_OK;
}

HRESULT CTSFDayiUIPresenter::ToHideCandidateWindow()
{
	if (_pCandidateWnd)
	{
		_pCandidateWnd->_Show(FALSE);
	}

    _updatedFlags = TF_CLUIE_SELECTION | TF_CLUIE_CURRENTPAGE;
    _UpdateUIElement();

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfTSFDayiUIElement::ITfUIElement::IsShown
//
//----------------------------------------------------------------------------

STDAPI CTSFDayiUIPresenter::IsShown(BOOL *pIsShow)
{
    *pIsShow = _pCandidateWnd->_IsWindowVisible();
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfTSFDayiUIElement::GetUpdatedFlags
//
//----------------------------------------------------------------------------

STDAPI CTSFDayiUIPresenter::GetUpdatedFlags(DWORD *pdwFlags)
{
    *pdwFlags = _updatedFlags;
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfTSFDayiUIElement::GetDocumentMgr
//
//----------------------------------------------------------------------------

STDAPI CTSFDayiUIPresenter::GetDocumentMgr(ITfDocumentMgr **ppdim)
{
    *ppdim = nullptr;

    return E_NOTIMPL;
}

//+---------------------------------------------------------------------------
//
// ITfTSFDayiUIElement::GetCount
//
//----------------------------------------------------------------------------

STDAPI CTSFDayiUIPresenter::GetCount(UINT *pCandidateCount)
{
    if (_pCandidateWnd)
    {
        *pCandidateCount = _pCandidateWnd->_GetCount();
    }
    else
    {
        *pCandidateCount = 0;
    }
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfTSFDayiUIElement::GetSelection
//
//----------------------------------------------------------------------------

STDAPI CTSFDayiUIPresenter::GetSelection(UINT *pSelectedCandidateIndex)
{
    if (_pCandidateWnd)
    {
        *pSelectedCandidateIndex = _pCandidateWnd->_GetSelection();
    }
    else
    {
        *pSelectedCandidateIndex = 0;
    }
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfTSFDayiUIElement::GetString
//
//----------------------------------------------------------------------------

STDAPI CTSFDayiUIPresenter::GetString(UINT uIndex, BSTR *pbstr)
{
    if (!_pCandidateWnd || (uIndex > _pCandidateWnd->_GetCount()))
    {
        return E_FAIL;
    }

    DWORD candidateLen = 0;
    const WCHAR* pCandidateString = nullptr;

    candidateLen = _pCandidateWnd->_GetCandidateString(uIndex, &pCandidateString);

    *pbstr = (candidateLen == 0) ? nullptr : SysAllocStringLen(pCandidateString, candidateLen);

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfTSFDayiUIElement::GetPageIndex
//
//----------------------------------------------------------------------------

STDAPI CTSFDayiUIPresenter::GetPageIndex(UINT *pIndex, UINT uSize, UINT *puPageCnt)
{
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
// ITfTSFDayiUIElement::SetPageIndex
//
//----------------------------------------------------------------------------

STDAPI CTSFDayiUIPresenter::SetPageIndex(UINT *pIndex, UINT uPageCnt)
{
    if (!_pCandidateWnd)
    {
        return E_FAIL;
    }
    return _pCandidateWnd->_SetPageIndex(pIndex, uPageCnt);
}

//+---------------------------------------------------------------------------
//
// ITfTSFDayiUIElement::GetCurrentPage
//
//----------------------------------------------------------------------------

STDAPI CTSFDayiUIPresenter::GetCurrentPage(UINT *puPage)
{
    if (!_pCandidateWnd)
    {
        *puPage = 0;
        return S_OK;
    }
    return _pCandidateWnd->_GetCurrentPage(puPage);
}

//+---------------------------------------------------------------------------
//
// ITfTSFDayiUIElementBehavior::SetSelection
// It is related of the mouse clicking behavior upon the suggestion window
//----------------------------------------------------------------------------

STDAPI CTSFDayiUIPresenter::SetSelection(UINT nIndex)
{
    if (_pCandidateWnd)
    {
        _pCandidateWnd->_SetSelection(nIndex);
    }

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfTSFDayiUIElementBehavior::Finalize
// It is related of the mouse clicking behavior upon the suggestion window
//----------------------------------------------------------------------------

STDAPI CTSFDayiUIPresenter::Finalize(void)
{
    _CandidateChangeNotification(CAND_ITEM_SELECT);
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfTSFDayiUIElementBehavior::Abort
//
//----------------------------------------------------------------------------

STDAPI CTSFDayiUIPresenter::Abort(void)
{
    return E_NOTIMPL;
}

//+---------------------------------------------------------------------------
//
// ITfIntegratableTSFDayiUIElement::SetIntegrationStyle
// To show candidateNumbers on the suggestion window
//----------------------------------------------------------------------------

STDAPI CTSFDayiUIPresenter::SetIntegrationStyle(GUID guidIntegrationStyle)
{
    return (guidIntegrationStyle == GUID_INTEGRATIONSTYLE_SEARCHBOX) ? S_OK : E_NOTIMPL;
}

//+---------------------------------------------------------------------------
//
// ITfIntegratableTSFDayiUIElement::GetSelectionStyle
//
//----------------------------------------------------------------------------

STDAPI CTSFDayiUIPresenter::GetSelectionStyle(_Out_ TfIntegratableCandidateListSelectionStyle *ptfSelectionStyle)
{
    *ptfSelectionStyle = STYLE_ACTIVE_SELECTION;
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfIntegratableTSFDayiUIElement::OnKeyDown
//
//----------------------------------------------------------------------------

STDAPI CTSFDayiUIPresenter::OnKeyDown(_In_ WPARAM wParam, _In_ LPARAM lParam, _Out_ BOOL *pIsEaten)
{
    wParam;
    lParam;

    *pIsEaten = TRUE;
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfIntegratableTSFDayiUIElement::ShowCandidateNumbers
//
//----------------------------------------------------------------------------

STDAPI CTSFDayiUIPresenter::ShowCandidateNumbers(_Out_ BOOL *pIsShow)
{
    *pIsShow = TRUE;
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfIntegratableTSFDayiUIElement::FinalizeExactCompositionString
//
//----------------------------------------------------------------------------

STDAPI CTSFDayiUIPresenter::FinalizeExactCompositionString()
{
    return E_NOTIMPL;
}


//+---------------------------------------------------------------------------
//
// _StartCandidateList
//
//----------------------------------------------------------------------------

HRESULT CTSFDayiUIPresenter::_StartCandidateList(TfClientId tfClientId, _In_ ITfDocumentMgr *pDocumentMgr, _In_ ITfContext *pContextDocument, TfEditCookie ec, _In_ ITfRange *pRangeComposition, UINT wndWidth)
{
	OutputDebugString(L"CTSFDayiUIPresenter::_StartCandidateList()\n");
	pDocumentMgr;tfClientId;

    HRESULT hr = E_FAIL;

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
    return hr;
}

//+---------------------------------------------------------------------------
//
// _EndCandidateList
//
//----------------------------------------------------------------------------

void CTSFDayiUIPresenter::_EndCandidateList()
{
    EndUIElement();

    DisposeCandidateWindow();

    _EndLayout();
}

//+---------------------------------------------------------------------------
//
// _SetText
//
//----------------------------------------------------------------------------

void CTSFDayiUIPresenter::_SetText(_In_ CTSFDayiArray<CCandidateListItem> *pCandidateList, BOOL isAddFindKeyCode)
{
    AddCandidateToTSFDayiUI(pCandidateList, isAddFindKeyCode);

    SetPageIndexWithScrollInfo(pCandidateList);

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

void CTSFDayiUIPresenter::AddCandidateToTSFDayiUI(_In_ CTSFDayiArray<CCandidateListItem> *pCandidateList, BOOL isAddFindKeyCode)
{
    for (UINT index = 0; index < pCandidateList->Count(); index++)
    {
        _pCandidateWnd->_AddString(pCandidateList->GetAt(index), isAddFindKeyCode);
    }
}

void CTSFDayiUIPresenter::SetPageIndexWithScrollInfo(_In_ CTSFDayiArray<CCandidateListItem> *pCandidateList)
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

void CTSFDayiUIPresenter::_ClearList()
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

void CTSFDayiUIPresenter::_SetTextColor(COLORREF crColor, COLORREF crBkColor)
{
    _pCandidateWnd->_SetTextColor(crColor, crBkColor);
}

void CTSFDayiUIPresenter::_SetFillColor(HBRUSH hBrush)
{
    _pCandidateWnd->_SetFillColor(hBrush);
}

//+---------------------------------------------------------------------------
//
// _GetSelectedCandidateString
//
//----------------------------------------------------------------------------

DWORD_PTR CTSFDayiUIPresenter::_GetSelectedCandidateString(_Outptr_result_maybenull_ const WCHAR **ppwchCandidateString)
{
    return _pCandidateWnd->_GetSelectedCandidateString(ppwchCandidateString);
}

//+---------------------------------------------------------------------------
//
// _MoveSelection
//
//----------------------------------------------------------------------------

BOOL CTSFDayiUIPresenter::_MoveSelection(_In_ int offSet)
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

BOOL CTSFDayiUIPresenter::_SetSelection(_In_ int selectedIndex)
{
    BOOL ret = _pCandidateWnd->_SetSelection(selectedIndex, TRUE);
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

BOOL CTSFDayiUIPresenter::_MovePage(_In_ int offSet)
{
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

void CTSFDayiUIPresenter::_MoveWindowToTextExt()
{
    RECT rc;

    if (FAILED(_GetTextExt(&rc)))
    {
        return;
    }

    _pCandidateWnd->_Move(rc.left, rc.bottom);
}
//+---------------------------------------------------------------------------
//
// _LayoutChangeNotification
//
//----------------------------------------------------------------------------

VOID CTSFDayiUIPresenter::_LayoutChangeNotification(_In_ RECT *lpRect)
{
	
	
    RECT rectCandidate = {0, 0, 0, 0};
    POINT ptCandidate = {0, 0};

    _pCandidateWnd->_GetClientRect(&rectCandidate);
    _pCandidateWnd->_GetWindowExtent(lpRect, &rectCandidate, &ptCandidate);
    _pCandidateWnd->_Move(ptCandidate.x, ptCandidate.y);
	_candLocation.x = ptCandidate.x;
	_candLocation.y = ptCandidate.y;
}

void CTSFDayiUIPresenter::GetCandLocation(POINT *lpPoint)
{
	lpPoint->x = _candLocation.x;
	lpPoint->y = _candLocation.y;
}

//+---------------------------------------------------------------------------
//
// _LayoutDestroyNotification
//
//----------------------------------------------------------------------------

VOID CTSFDayiUIPresenter::_LayoutDestroyNotification()
{
    _EndCandidateList();
}

//+---------------------------------------------------------------------------
//
// _CandidateChangeNotifiction
//
//----------------------------------------------------------------------------

HRESULT CTSFDayiUIPresenter::_CandidateChangeNotification(_In_ enum CANDWND_ACTION action)
{
    HRESULT hr = E_FAIL;

    TfClientId tfClientId = _pTextService->_GetClientId();
    ITfThreadMgr* pThreadMgr = nullptr;
    ITfDocumentMgr* pDocumentMgr = nullptr;
    ITfContext* pContext = nullptr;

    _KEYSTROKE_STATE KeyState;
    KeyState.Category = _Category;
    KeyState.Function = FUNCTION_FINALIZE_CANDIDATELIST;

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
HRESULT CTSFDayiUIPresenter::_CandWndCallback(_In_ void *pv, _In_ enum CANDWND_ACTION action)
{
    CTSFDayiUIPresenter* fakeThis = (CTSFDayiUIPresenter*)pv;

    return fakeThis->_CandidateChangeNotification(action);
}

//+---------------------------------------------------------------------------
//
// _UpdateUIElement
//
//----------------------------------------------------------------------------

HRESULT CTSFDayiUIPresenter::_UpdateUIElement()
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

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// OnSetThreadFocus
//
//----------------------------------------------------------------------------

HRESULT CTSFDayiUIPresenter::OnSetThreadFocus()
{
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

HRESULT CTSFDayiUIPresenter::OnKillThreadFocus()
{
    if (_isShowMode)
    {
        Show(FALSE);
    }
    return S_OK;
}

void CTSFDayiUIPresenter::RemoveSpecificCandidateFromList(_In_ LCID Locale, _Inout_ CTSFDayiArray<CCandidateListItem> &candidateList, _In_ CStringRange &candidateString)
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

void CTSFDayiUIPresenter::AdviseUIChangedByArrowKey(_In_ KEYSTROKE_FUNCTION arrowKey)
{
    switch (arrowKey)
    {
    case FUNCTION_MOVE_UP:
        {
            _MoveSelection(MOVEUP_ONE);
            break;
        }
    case FUNCTION_MOVE_DOWN:
        {
            _MoveSelection(MOVEDOWN_ONE);
            break;
        }
    case FUNCTION_MOVE_PAGE_UP:
        {
            _MovePage(MOVEUP_ONE);
            break;
        }
    case FUNCTION_MOVE_PAGE_DOWN:
        {
            _MovePage(MOVEDOWN_ONE);
            break;
        }
    case FUNCTION_MOVE_PAGE_TOP:
        {
            _SetSelection(MOVETO_TOP);
            break;
        }
    case FUNCTION_MOVE_PAGE_BOTTOM:
        {
            _SetSelection(MOVETO_BOTTOM);
            break;
        }
    default:
        break;
    }
}

HRESULT CTSFDayiUIPresenter::BeginUIElement()
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
    return hr;
}

HRESULT CTSFDayiUIPresenter::EndUIElement()
{
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
    return hr;
}

HRESULT CTSFDayiUIPresenter::MakeCandidateWindow(_In_ ITfContext *pContextDocument, _In_ UINT wndWidth)
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

    if (!_pCandidateWnd->_Create(_atom, wndWidth, parentWndHandle))
    {
        hr = E_OUTOFMEMORY;
        goto Exit;
    }

Exit:
    return hr;
}

void CTSFDayiUIPresenter::DisposeCandidateWindow()
{
    if (nullptr == _pCandidateWnd)
    {
        return;
    }

    _pCandidateWnd->_Destroy();

    delete _pCandidateWnd;
    _pCandidateWnd = nullptr;
}