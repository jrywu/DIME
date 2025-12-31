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
#include "DIME.h"
#include "UIPresenter.h"

//+---------------------------------------------------------------------------
//
// ITfTextLayoutSink::OnSetThreadFocus
//
//----------------------------------------------------------------------------

STDAPI CDIME::OnSetThreadFocus()
{
	debugPrint(L"CDIME::OnSetThreadFocus()\n");
	
    if (_pUIPresenter)
    {
        ITfDocumentMgr* pCandidateListDocumentMgr = nullptr;
        ITfContext* pTfContext = _pUIPresenter->_GetContextDocument();

        if ( pTfContext && SUCCEEDED(pTfContext->GetDocumentMgr(&pCandidateListDocumentMgr)) && pCandidateListDocumentMgr)
        {
            if (pCandidateListDocumentMgr == _pDocMgrLastFocused)
            {
                _pUIPresenter->OnSetThreadFocus();
            }

            pCandidateListDocumentMgr->Release();
        }
    }

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfTextLayoutSink::OnKillThreadFocus
//
//----------------------------------------------------------------------------

STDAPI CDIME::OnKillThreadFocus()
{
	debugPrint(L"CDIME::OnSetThreadFocus()\n");
    if (_pUIPresenter)
    {
        ITfDocumentMgr* pCandidateListDocumentMgr = nullptr;
        ITfContext* pTfContext = _pUIPresenter->_GetContextDocument();

        if (pTfContext && SUCCEEDED(pTfContext->GetDocumentMgr(&pCandidateListDocumentMgr)) && pCandidateListDocumentMgr)
        {
            if (_pDocMgrLastFocused)
            {
                _pDocMgrLastFocused->Release();
				_pDocMgrLastFocused = nullptr;
            }
            _pDocMgrLastFocused = pCandidateListDocumentMgr;
            if (_pDocMgrLastFocused)
                _pDocMgrLastFocused->AddRef();
        }
        _pUIPresenter->OnKillThreadFocus();
    }
    return S_OK;
}

BOOL CDIME::_InitThreadFocusSink()
{
    ITfSource* pSource = nullptr;

    if ( (_pThreadMgr && FAILED(_pThreadMgr->QueryInterface(IID_ITfSource, (void **)&pSource))) || pSource == nullptr )
    {
        return FALSE;
    }

    if (FAILED(pSource->AdviseSink(IID_ITfThreadFocusSink, (ITfThreadFocusSink *)this, &_dwThreadFocusSinkCookie)))
    {
        pSource->Release();
        return FALSE;
    }

    pSource->Release();

    return TRUE;
}

void CDIME::_UninitThreadFocusSink()
{
    ITfSource* pSource = nullptr;

    if ( (_pThreadMgr && FAILED(_pThreadMgr->QueryInterface(IID_ITfSource, (void **)&pSource))) || pSource == nullptr)
    {
        return;
    }

    if (FAILED(pSource->UnadviseSink(_dwThreadFocusSinkCookie)))
    {
        pSource->Release();
        return;
    }

    pSource->Release();
}