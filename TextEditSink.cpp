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
#include "globals.h"
#include "DIME.h"

//+---------------------------------------------------------------------------
//
// ITfTextEditSink::OnEndEdit
//
// Called by the system whenever anyone releases a write-access document lock.
//----------------------------------------------------------------------------

STDAPI CDIME::OnEndEdit(__RPC__in_opt ITfContext *pContext, TfEditCookie ecReadOnly, __RPC__in_opt ITfEditRecord *pEditRecord)
{
	debugPrint(L"CDIME::OnEndEdit()\n");
    BOOL isSelectionChanged;

    //
    // did the selection change?
    // The selection change includes the movement of caret as well. 
    // The caret position is represent as the empty selection range when
    // there is no selection.
    //
    if (pEditRecord == nullptr)
    {
        return E_INVALIDARG;
    }
    if (SUCCEEDED(pEditRecord->GetSelectionStatus(&isSelectionChanged)) &&
        isSelectionChanged)
    {
        // If the selection is moved to out side of the current composition,
        // we terminate the composition. This TextService supports only one
        // composition in one context object.
        if (_IsComposing())
        {
            TF_SELECTION tfSelection;
            ULONG fetched = 0;

            if (pContext == nullptr)
            {
                return E_INVALIDARG;
            }
            if (FAILED(pContext->GetSelection(ecReadOnly, TF_DEFAULT_SELECTION, 1, &tfSelection, &fetched)) || fetched != 1 || tfSelection.range == nullptr)
            {
                return S_FALSE;
            }

            ITfRange* pRangeComposition = nullptr;
            if (SUCCEEDED(_pComposition->GetRange(&pRangeComposition)) && pRangeComposition)
            {
                if (!_IsRangeCovered(ecReadOnly, tfSelection.range, pRangeComposition))
                {
                    _EndComposition(pContext);
                }

                pRangeComposition->Release();
            }

            tfSelection.range->Release();
        }
    }

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _InitTextEditSink
//
// Init a text edit sink on the topmost context of the document.
// Always release any previous sink.
//----------------------------------------------------------------------------

BOOL CDIME::_InitTextEditSink(_In_ ITfDocumentMgr *pDocMgr)
{
	debugPrint(L"CDIME::_InitTextEditSink()\n");
    ITfSource* pSource = nullptr;
    BOOL ret = TRUE;

    // clear out any previous sink first
    if (_textEditSinkCookie != TF_INVALID_COOKIE)
    {
		debugPrint(L"CDIME::_InitTextEditSink() release old textEditSink first.");
        if (_pTextEditSinkContext && SUCCEEDED(_pTextEditSinkContext->QueryInterface(IID_ITfSource, (void **)&pSource)) && pSource)
        {
            pSource->UnadviseSink(_textEditSinkCookie);
            pSource->Release();
        }

        _pTextEditSinkContext->Release();
        _pTextEditSinkContext = nullptr;
        _textEditSinkCookie = TF_INVALID_COOKIE;
    }

    if (pDocMgr == nullptr)
    {
        return TRUE; // caller just wanted to clear the previous sink
    }

    if (FAILED(pDocMgr->GetTop(&_pTextEditSinkContext)))
    {
        return FALSE;
    }

    if (_pTextEditSinkContext == nullptr)
    {
        return TRUE; // empty document, no sink possible
    }

    ret = FALSE;
    if (SUCCEEDED(_pTextEditSinkContext->QueryInterface(IID_ITfSource, (void **)&pSource)) && pSource)
    {
		debugPrint(L"CDIME::_InitTextEditSink() advis new textEditSink.");
        if (SUCCEEDED(pSource->AdviseSink(IID_ITfTextEditSink, (ITfTextEditSink *)this, &_textEditSinkCookie)))
        {
            ret = TRUE;
        }
        else
        {
            _textEditSinkCookie = TF_INVALID_COOKIE;
        }
        pSource->Release();
    }

    if (ret == FALSE)
    {
        _pTextEditSinkContext->Release();
        _pTextEditSinkContext = nullptr;
    }

    return ret;
}


void CDIME::_UnInitTextEditSink()
{
	debugPrint(L"CDIME::_UnInitTextEditSink()\n");
    ITfSource* pSource = nullptr;


	 if (_textEditSinkCookie != TF_INVALID_COOKIE && _pTextEditSinkContext)
    {
        if (SUCCEEDED(_pTextEditSinkContext->QueryInterface(IID_ITfSource, (void **)&pSource)) && pSource)
        {
            pSource->UnadviseSink(_textEditSinkCookie);
            pSource->Release();
        }

        _pTextEditSinkContext->Release();
        _pTextEditSinkContext = nullptr;
        _textEditSinkCookie = TF_INVALID_COOKIE;
    }

}
