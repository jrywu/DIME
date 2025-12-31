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

#include "Private.h"
#include "Globals.h"
#include "DIME.h"
#include "CompositionProcessorEngine.h"
#include "UIPresenter.h"
#include "GetTextExtentEditSession.h"

//+---------------------------------------------------------------------------
//
// ITfCompositionSink::OnCompositionTerminated
//
// Callback for ITfCompositionSink.  The system calls this method whenever
// someone other than this service ends a composition.
//----------------------------------------------------------------------------

STDAPI CDIME::OnCompositionTerminated(TfEditCookie ecWrite, _In_ ITfComposition *pComposition)
{
	debugPrint(L"CDIME::OnCompositionTerminated()");
	HRESULT hr = S_OK;
    ITfContext* pContext = _pContext;
   
	// Clear dummy composition
	_RemoveDummyCompositionForComposing(ecWrite, pComposition);
	
    // Clear display attribute and end composition, _EndComposition will release composition for us
     if (pContext)
    {
        pContext->AddRef();
	}
	

	_EndComposition(pContext);
	_DeleteCandidateList(TRUE, pContext);
	
	if (pContext)
	{
		pContext->Release();
        pContext = nullptr;
    }
    return hr;
}


//+---------------------------------------------------------------------------
//
// _IsComposing
//
//----------------------------------------------------------------------------

BOOL CDIME::_IsComposing()
{
    return _pComposition != nullptr;
}

//+---------------------------------------------------------------------------
//
// _SetComposition
//
//----------------------------------------------------------------------------

void CDIME::_SetComposition(_In_ ITfComposition *pComposition)
{
    _pComposition = pComposition;
	lastReadingString.Set(nullptr, 0); //reset lastReadingString
}

//+---------------------------------------------------------------------------
//
// _AddComposingAndChar
//
//----------------------------------------------------------------------------

HRESULT CDIME::_AddComposingAndChar(TfEditCookie ec, _In_ ITfContext *pContext, _In_ CStringRange *pstrAddString)
{
	debugPrint(L"CDIME::_AddComposingAndChar() reading string = %s", pstrAddString->Get());
    HRESULT hr = S_OK;

    ULONG fetched = 0;
    TF_SELECTION tfSelection;

    if (pContext->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &tfSelection, &fetched) != S_OK || fetched == 0)
        return S_FALSE;

    //
    // make range start to selection
    //
    ITfRange* pAheadSelection = nullptr;
    hr = pContext->GetStart(ec, &pAheadSelection);
    if (SUCCEEDED(hr) && pAheadSelection)
    {
		debugPrint(L"CDIME::_AddComposingAndChar() SUCCEEDED( pContext->GetStart( &pAheadSelection))");
        hr = pAheadSelection->ShiftEndToRange(ec, tfSelection.range, TF_ANCHOR_START);
        if (SUCCEEDED(hr))
        {
			debugPrint(L"CDIME::_AddComposingAndChar() SUCCEEDED( pAheadSelection->ShiftEndToRange())");
            ITfRange* pRange = nullptr;
			BOOL exist_composing = _FindComposingRange(ec, pContext, pAheadSelection, &pRange);

			if (FAILED(_SetInputString(ec, pContext, pRange, pstrAddString, exist_composing)))
				debugPrint(L"CDIME::_AddComposingAndChar() _SetInputString() failed.");
			
			if (pRange)
				pRange->Release();

			lastReadingString = *pstrAddString;
            
        }
    }

    tfSelection.range->Release();

    if (pAheadSelection)
    {
        pAheadSelection->Release();
    }

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _AddCharAndFinalize
//
//----------------------------------------------------------------------------

HRESULT CDIME::_AddCharAndFinalize(TfEditCookie ec, _In_ ITfContext *pContext, _In_ CStringRange *pstrAddString)
{
	debugPrint(L"CDIME::_AddCharAndFinalize()");
    HRESULT hr = E_FAIL;

    ULONG fetched = 0;
    TF_SELECTION tfSelection;

	if ((hr = pContext->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &tfSelection, &fetched)) != S_OK || fetched != 1 || tfSelection.range==nullptr || pstrAddString ==nullptr)
        return hr;

    // we use SetText here instead of InsertTextAtSelection because we've already started a composition
    // we don't want the app to adjust the insertion point inside our composition
    hr = tfSelection.range->SetText(ec, 0, pstrAddString->Get(), (LONG)pstrAddString->GetLength());
    if (hr == S_OK)
    {
        // update the selection, we'll make it an insertion point just past
        // the inserted text.
        tfSelection.range->Collapse(ec, TF_ANCHOR_END);
		if(pContext)
			pContext->SetSelection(ec, 1, &tfSelection);
    }

    tfSelection.range->Release();

    return hr;
}

//+---------------------------------------------------------------------------
//
// _FindComposingRange
//
//----------------------------------------------------------------------------

BOOL CDIME::_FindComposingRange(TfEditCookie ec, _In_ ITfContext *pContext, _In_ ITfRange *pSelection, _Outptr_result_maybenull_ ITfRange **ppRange)
{
	debugPrint(L"CDIME::_FindComposingRange()");

    if (pContext == nullptr || ppRange == nullptr)
    {
		debugPrint(L"CDIME::_FindComposingRange() failed with null pContext or pRange");
        return FALSE;
    }

    *ppRange = nullptr;

    // find GUID_PROP_COMPOSING
    ITfProperty* pPropComp = nullptr;
    IEnumTfRanges* enumComp = nullptr;

    HRESULT hr = pContext->GetProperty(GUID_PROP_COMPOSING, &pPropComp);
    if (FAILED(hr) || pPropComp == nullptr)
    {
		if (FAILED(hr))
			debugPrint(L"CDIME::_FindComposingRange()  pContext->GetProperty() failed");
		else
			debugPrint(L"CDIME::_FindComposingRange() failed with null pPropComp");
        return FALSE;
    }

    hr = pPropComp->EnumRanges(ec, &enumComp, pSelection);
    if (FAILED(hr) || enumComp == nullptr)
    {
		if (FAILED(hr))
			debugPrint(L"CDIME::_FindComposingRange()  pPropComp->EnumRanges failed");
		else
			debugPrint(L"CDIME::_FindComposingRange() failed with null enumComp");
        pPropComp->Release();
        return FALSE;
    }

    BOOL isCompExist = FALSE;
    VARIANT var;
    ULONG  fetched = 0;

    while (enumComp->Next(1, ppRange, &fetched) == S_OK && fetched == 1)
    {
		debugPrint(L"CDIME::_FindComposingRange() enumComp->Next() ");
        hr = pPropComp->GetValue(ec, *ppRange, &var);
        if (hr == S_OK)
        {
            if (var.vt == VT_I4 && var.lVal != 0)
            {
				WCHAR rangeText[4096];
				rangeText[0] = NULL;
				fetched = 0;
				hr = (*ppRange)->GetText(ec, 0, rangeText, 4096, &fetched);
				if (SUCCEEDED(hr))
				{
					if (lastReadingString.GetLength() > 0) // The last reading string is null when composing is just started and the range should be empty.
					{
						debugPrint(L"CDIME::_FindComposingRange() text in range = %s with %d character, last reading string = %s with %d characters",
							rangeText, fetched, lastReadingString.Get(), lastReadingString.GetLength());
						UINT cmpCount = (int)lastReadingString.GetLength();
						BOOL rangeMatchReadingString = FALSE;
						if (fetched > 0 && fetched > cmpCount)
						{
							LONG shifted = 0;
							// range contains other characters than last reading string, and these characters should be skipped with shifting the start anchor
							(*ppRange)->ShiftStart(ec, fetched - cmpCount, &shifted, NULL);
							debugPrint(L"CDIME::_FindComposingRange() shift start anchor with %d characters", shifted);
							rangeText[0] = NULL;
							hr = (*ppRange)->GetText(ec, 0, rangeText, 4096, &fetched);
							if (SUCCEEDED(hr))
								debugPrint(L"CDIME::_FindComposingRange() text in range with shited anchor = %s with %d character", rangeText, fetched);
						}
						// The range we are composing should always matched with the last reading string.
						rangeMatchReadingString = CompareString(GetLocale(), 0, rangeText, cmpCount, lastReadingString.Get(), cmpCount) == CSTR_EQUAL;
				
						if (rangeMatchReadingString)
						{
							isCompExist = TRUE;
							debugPrint(L"CDIME::_FindComposingRange() text in this range mathced with last reading string. isCompExist = TRUE");
							break;
						}
					}
					
				}
				else
				{
					debugPrint(L"CDIME::_FindComposingRange() cannot getText from range isCompExist = TRUE");
				}
            }
        }
		debugPrint(L"CDIME::_FindComposingRange() release *ppRange");
        if(*ppRange)
			(*ppRange)->Release();
        *ppRange = nullptr;
    }

    pPropComp->Release();
    enumComp->Release();

    return isCompExist;
}

//+---------------------------------------------------------------------------
//
// _SetInputString
//
//----------------------------------------------------------------------------

HRESULT CDIME::_SetInputString(TfEditCookie ec, _In_ ITfContext *pContext, _Out_opt_ ITfRange *pRange, _In_ CStringRange *pstrAddString, BOOL exist_composing)
{
	debugPrint(L"CDIME::_SetInputString() exist_composing = %x", exist_composing);
    ITfRange* pRangeInsert = nullptr;
	HRESULT hr = S_OK;
    if (!exist_composing)
    {
        _InsertAtSelection(ec, pContext, pstrAddString, &pRangeInsert);
        if (pRangeInsert == nullptr)
        {
            return hr;
        }
        pRange = pRangeInsert;
    }
    if (pRange != nullptr)
    {
        hr = pRange->SetText(ec, 0, pstrAddString->Get(), (LONG)pstrAddString->GetLength());
		if (FAILED(hr))
		{
			debugPrint(L"CDIME::_SetInputString() pRange->SetText() failed with hr = %x", hr);			
			goto exit;
		}

    }
	
	if (!_SetCompositionLanguage(ec, pContext))
	{
		debugPrint(L"CDIME::_SetInputString() _SetCompositionLanguage failed");
		hr = E_FAIL;
		goto exit;
	}

	if (!_SetCompositionDisplayAttributes(ec, pContext, _gaDisplayAttributeInput))
	{
		debugPrint(L"CDIME::_SetInputString() _SetCompositionDisplayAttributes() failed");
		hr = E_FAIL;
		goto exit;
	}

    // update the selection, we'll make it an insertion point just past
    // the inserted text.
    ITfRange* pSelection = nullptr;
    TF_SELECTION sel;

    if ((pRange != nullptr) && (pRange->Clone(&pSelection) == S_OK))
    {
        hr = pSelection->Collapse(ec, TF_ANCHOR_END);
		if (FAILED(hr))
		{
			debugPrint(L"CDIME::_SetInputString() pSelection->Collapse() failed, hr = %x", hr);
			goto exit;
		}

        sel.range = pSelection;
        sel.style.ase = TF_AE_NONE;
        sel.style.fInterimChar = FALSE;
        pContext->SetSelection(ec, 1, &sel);
        pSelection->Release();
    }

exit:
    if (pRangeInsert)
        pRangeInsert->Release();

    return hr;
}

//+---------------------------------------------------------------------------
//
// _InsertAtSelection
//
//----------------------------------------------------------------------------

HRESULT CDIME::_InsertAtSelection(TfEditCookie ec, _In_ ITfContext *pContext, _In_ CStringRange *pstrAddString, _Outptr_ ITfRange **ppCompRange)
{
	debugPrint(L"CDIME::_InsertAtSelection()");
    ITfRange* rangeInsert = nullptr;
    ITfInsertAtSelection* pias = nullptr;
    HRESULT hr = S_OK;

    if (ppCompRange == nullptr || pContext == nullptr || pstrAddString == nullptr)
    {
		debugPrint(L"CDIME::_InsertAtSelection() failed with null ppCompRange or pContext or pstrAddstring");
        hr = E_INVALIDARG;
        goto Exit;
    }

    *ppCompRange = nullptr;

    hr = pContext->QueryInterface(IID_ITfInsertAtSelection, (void **)&pias);
    if (FAILED(hr) || pias == nullptr)
    {
		debugPrint(L"CDIME::_InsertAtSelection() failed with null pias or QueryInterface failed");
        goto Exit;
    }

    hr = pias->InsertTextAtSelection(ec, TF_IAS_QUERYONLY, pstrAddString->Get(), (LONG)pstrAddString->GetLength(), &rangeInsert);

    if ( FAILED(hr) || rangeInsert == nullptr)
    {
		debugPrint(L"CDIME::_InsertAtSelection() InsertTextAtSelection failed");
        rangeInsert = nullptr;
        pias->Release();
        goto Exit;
    }
	WCHAR rangeText[256];
	rangeText[0] = NULL;
	ULONG fetched = 0;
	hr = rangeInsert->GetText(ec, 0, rangeText, 256, &fetched);
	if (SUCCEEDED(hr) && rangeText)
		debugPrint(L"CDIME::_InsertAtSelection() text in range = %s", rangeText);
    *ppCompRange = rangeInsert;
    pias->Release();
    hr = S_OK;

Exit:
    return hr;
}

//+---------------------------------------------------------------------------
//
// _RemoveDummyCompositionForComposing
//
//----------------------------------------------------------------------------

HRESULT CDIME::_RemoveDummyCompositionForComposing
	(TfEditCookie ec, _In_ ITfComposition *pComposition)
{
	debugPrint(L"CDIME::_RemoveDummyCompositionForComposing()\n");
    HRESULT hr = S_OK;

    ITfRange* pRange = nullptr;
    
    if (pComposition)
    {
        hr = pComposition->GetRange(&pRange);
        if (SUCCEEDED(hr) && pRange)
        {
            pRange->SetText(ec, 0, nullptr, 0);
            pRange->Release();
        }
    }

    return hr;
}

//+---------------------------------------------------------------------------
//
// _SetCompositionLanguage
//
//----------------------------------------------------------------------------

BOOL CDIME::_SetCompositionLanguage(TfEditCookie ec, _In_ ITfContext *pContext)
{
	debugPrint(L"CDIME::_SetCompositionLanguage()");
    HRESULT hr = S_OK;
    BOOL ret = TRUE;

	if (pContext == nullptr || _pComposition == nullptr)
    {
		debugPrint(L"CDIME::_SetCompositionLanguage() failed with null pContext or _pComposition");
		ret = FALSE;
        goto Exit;
    }

    ITfRange* pRangeComposition = nullptr;
    ITfProperty* pLanguageProperty = nullptr;

    // we need a range and the context it lives in
    hr = _pComposition->GetRange(&pRangeComposition);
    if (FAILED(hr) || pRangeComposition == nullptr)
    {
		if (FAILED(hr))
			debugPrint(L"CDIME::_SetCompositionLanguage() _pComposition->GetRange() failed");
		else
			debugPrint(L"CDIME::_SetCompositionLanguage() failed with null pRangeComposition");

        ret = FALSE;
        goto Exit;
    }

    // get our the language property
    hr = pContext->GetProperty(GUID_PROP_LANGID, &pLanguageProperty);
    if (FAILED(hr) || pLanguageProperty == nullptr)
    {
		if (FAILED(hr))
			debugPrint(L"CDIME::_SetCompositionLanguage() pContext->GetProperty() failed hr = %x", hr);
		else
			debugPrint(L"CDIME::_SetCompositionLanguage() failed with null pLanguageProperty");

        ret = FALSE;
        goto Exit;
    }

    VARIANT var;
    var.vt = VT_I4;   // we're going to set DWORD
    var.lVal = _langid; 

    hr = pLanguageProperty->SetValue(ec, pRangeComposition, &var);
    if (FAILED(hr) || pRangeComposition == nullptr)
    {
		if (FAILED(hr))
			debugPrint(L"CDIME::_SetCompositionLanguage() pLanguageProperty->SetValue() failed hr = %x", hr);
		else
			debugPrint(L"CDIME::_SetCompositionLanguage() failed with null pRangeComposition ");

        ret = FALSE;
        goto Exit;
    }

    pLanguageProperty->Release();
    pRangeComposition->Release();

Exit:
    return ret;
}


//+---------------------------------------------------------------------------
//
// CProbeComposistionEditSession
//
//----------------------------------------------------------------------------

class CProbeComposistionEditSession : public CEditSessionBase
{
public:
    CProbeComposistionEditSession(_In_ CDIME *pTextService, _In_ ITfContext *pContext) : CEditSessionBase(pTextService, pContext)
    {
    }

    // ITfEditSession
    STDMETHODIMP DoEditSession(TfEditCookie ec);
};

//+---------------------------------------------------------------------------
//
// ITfEditSession::DoEditSession
//
//----------------------------------------------------------------------------

STDAPI CProbeComposistionEditSession::DoEditSession(TfEditCookie ec)
{
	debugPrint(L"CProbeComposistionEditSession::DoEditSession()\n");
	if(_pTextService) 
		_pTextService->_ProbeCompositionRangeNotification(ec, _pContext);
	
    return S_OK;
}

//////////////////////////////////////////////////////////////////////
//
// CDIME class
//
//////////////////////////////////////////////////////////////////////+---------------------------------------------------------------------------
//
// _ProbeComposition
//
// starts a new (std::nothrow) pProbeComposistionEditSession at the selection of the current 
// focus context to get correct caret position.
//----------------------------------------------------------------------------

void CDIME::_ProbeComposition(_In_ ITfContext *pContext)
{
	debugPrint(L"CDIME::_ProbeComposition() pContext = %x\n", pContext);


	CProbeComposistionEditSession* pProbeComposistionEditSession = new (std::nothrow) CProbeComposistionEditSession(this, pContext);

	if (pProbeComposistionEditSession && pContext)
	{
		
		HRESULT hrES = S_OK, hr = S_OK;
		hr = pContext->RequestEditSession(_tfClientId, pProbeComposistionEditSession, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &hrES);
		
		debugPrint(L"CDIME::_ProbeComposition() RequestEdisession HRESULT = %x, return HRESULT  = %x\n", hrES, hr );

		pProbeComposistionEditSession->Release();
	}
	
   
}

HRESULT CDIME::_ProbeCompositionRangeNotification(_In_ TfEditCookie ec, _In_ ITfContext *pContext)
{
	debugPrint(L"CDIME::_ProbeCompositionRangeNotification(), \n");


	HRESULT hr = S_OK;
	if(!_IsComposing())
		_StartComposition(pContext);
	
	hr = E_FAIL;
    ULONG fetched = 0;
    TF_SELECTION tfSelection;

    if ( pContext == nullptr || (hr = pContext->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &tfSelection, &fetched)) != S_OK || fetched != 1 ||tfSelection.range==nullptr)
	{
		_TerminateComposition(ec,pContext);
        return hr;
	}
	tfSelection.range->Release();
   
	
	ITfRange *pRange;
	ITfContextView* pContextView;
	ITfDocumentMgr* pDocumgr;
	if (_pComposition&&SUCCEEDED(_pComposition->GetRange(&pRange)) && pRange)
	{
		if(pContext && SUCCEEDED(pContext->GetActiveView(&pContextView)) && pContextView)
		{
			if(pContext && SUCCEEDED( pContext->GetDocumentMgr(&pDocumgr)) && pDocumgr && _pThreadMgr &&_pUIPresenter)
			{
				ITfDocumentMgr* pFocusDocuMgr;
				_pThreadMgr->GetFocus(&pFocusDocuMgr);
				if(pFocusDocuMgr == pDocumgr)
				{
					_pUIPresenter->_StartLayout(pContext, ec, pRange);
					_pUIPresenter->_MoveUIWindowsToTextExt();
				}
				else 
					_pUIPresenter->ClearNotify();

				pDocumgr->Release();
			}
		pContextView->Release();
		}
		pRange->Release();
	}


    
	_TerminateComposition(ec,pContext);
	return hr;
}
