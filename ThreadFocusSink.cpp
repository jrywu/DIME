//
//
// Derived from Microsoft Sample IME by Jeremy '13,7,17
//
//


#include "Private.h"
#include "TSFTTS.h"
#include "UIPresenter.h"

//+---------------------------------------------------------------------------
//
// ITfTextLayoutSink::OnSetThreadFocus
//
//----------------------------------------------------------------------------

STDAPI CTSFTTS::OnSetThreadFocus()
{
	debugPrint(L"CTSFTTS::OnSetThreadFocus()\n");
    if (_pTSFTTSUIPresenter)
    {
        ITfDocumentMgr* pCandidateListDocumentMgr = nullptr;
        ITfContext* pTfContext = _pTSFTTSUIPresenter->_GetContextDocument();

        if ((nullptr != pTfContext) && SUCCEEDED(pTfContext->GetDocumentMgr(&pCandidateListDocumentMgr)))
        {
            if (pCandidateListDocumentMgr == _pDocMgrLastFocused)
            {
                _pTSFTTSUIPresenter->OnSetThreadFocus();
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

STDAPI CTSFTTS::OnKillThreadFocus()
{
	debugPrint(L"CTSFTTS::OnSetThreadFocus()\n");
    if (_pTSFTTSUIPresenter)
    {
        ITfDocumentMgr* pCandidateListDocumentMgr = nullptr;
        ITfContext* pTfContext = _pTSFTTSUIPresenter->_GetContextDocument();

        if ((nullptr != pTfContext) && SUCCEEDED(pTfContext->GetDocumentMgr(&pCandidateListDocumentMgr)))
        {
            if (_pDocMgrLastFocused)
            {
                _pDocMgrLastFocused->Release();
				_pDocMgrLastFocused = nullptr;
            }
            _pDocMgrLastFocused = pCandidateListDocumentMgr;
            if (_pDocMgrLastFocused)
            {
                _pDocMgrLastFocused->AddRef();
            }
        }
        _pTSFTTSUIPresenter->OnKillThreadFocus();
    }
    return S_OK;
}

BOOL CTSFTTS::_InitThreadFocusSink()
{
    ITfSource* pSource = nullptr;

    if (FAILED(_pThreadMgr->QueryInterface(IID_ITfSource, (void **)&pSource)))
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

void CTSFTTS::_UninitThreadFocusSink()
{
    ITfSource* pSource = nullptr;

    if (FAILED(_pThreadMgr->QueryInterface(IID_ITfSource, (void **)&pSource)))
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