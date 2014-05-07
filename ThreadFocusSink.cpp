//
//
// Derived from Microsoft Sample IME by Jeremy '13,7,17
//
//
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