//
//
// Derived from Microsoft Sample IME by Jeremy '13,7,17
//
//
//#define DEBUG_PRINT

#include "Private.h"
#include "Globals.h"
#include "TSFTTS.h"
#include "UIPresenter.h"
#include "GetTextExtentEditSession.h"

//+---------------------------------------------------------------------------
//
// ITfThreadMgrEventSink::OnInitDocumentMgr
//
// Sink called by the framework just before the first context is pushed onto
// a document.
//----------------------------------------------------------------------------

STDAPI CTSFTTS::OnInitDocumentMgr(_In_ ITfDocumentMgr *pDocMgr)
{
    pDocMgr;
    return E_NOTIMPL;
}

//+---------------------------------------------------------------------------
//
// ITfThreadMgrEventSink::OnUninitDocumentMgr
//
// Sink called by the framework just after the last context is popped off a
// document.
//----------------------------------------------------------------------------

STDAPI CTSFTTS::OnUninitDocumentMgr(_In_ ITfDocumentMgr *pDocMgr)
{
    pDocMgr;
    return E_NOTIMPL;
}

//+---------------------------------------------------------------------------
//
// ITfThreadMgrEventSink::OnSetFocus
//
// Sink called by the framework when focus changes from one document to
// another.  Either document may be NULL, meaning previously there was no
// focus document, or now no document holds the input focus.
//----------------------------------------------------------------------------

STDAPI CTSFTTS::OnSetFocus(_In_ ITfDocumentMgr *pDocMgrFocus, _In_ ITfDocumentMgr *pDocMgrPrevFocus)
{
	debugPrint(L"CTSFTTS::OnSetFocus()\n");
    pDocMgrPrevFocus;
	ITfContext* pContext = nullptr;

	_InitTextEditSink(pDocMgrFocus);
	
	if(!_UpdateLanguageBarOnSetFocus(pDocMgrFocus))
	{
		pDocMgrFocus->GetTop(&pContext);	
		if(pContext && (CConfig::GetShowNotifyDesktop() || _IsStoreAppMode() ))
		{	
			CStringRange notify;
			//_pUIPresenter->ShowNotifyText(&notify.Set(_isChinese?L"中文":L"英文",2), 500, 3000, NOTIFY_CHN_ENG);
		}

	}


	if(pDocMgrFocus)
	{
#ifdef DEBUG_PRINT //probing the TSF supprting status.
		ITfContext* pContext = nullptr;
		bool isTransitory = false;
		bool isMultiRegion = false;
		bool isMultiSelection = false;
		if (SUCCEEDED(pDocMgrFocus->GetTop(&pContext)))
		{
			//if(_pContext == nullptr)
				//_SaveCompositionContext(pContext);
			TF_STATUS tfStatus;
			if (SUCCEEDED(pContext->GetStatus(&tfStatus)))
			{
				isTransitory = (tfStatus.dwStaticFlags & TS_SS_TRANSITORY) == TS_SS_TRANSITORY;	
				isMultiRegion = (tfStatus.dwStaticFlags & TF_SS_REGIONS) == TF_SS_REGIONS;	
				isMultiSelection = (tfStatus.dwStaticFlags & TF_SS_DISJOINTSEL) == TF_SS_DISJOINTSEL;	
			}

		}
		if(isTransitory) debugPrint(L"TSF in Transitory context\n");
		if(isMultiRegion) debugPrint(L"Support multi region\n");
		if(isMultiSelection) debugPrint(L"Support multi selection\n");
#endif
	}
	
    //
    // We have to hide/unhide candidate list depending on whether they are 
    // associated with pDocMgrFocus.
    //
    if (_pUIPresenter)
    {
        ITfDocumentMgr* pCandidateListDocumentMgr = nullptr;
        ITfContext* pTfContext = _pUIPresenter->_GetContextDocument();
        if ((nullptr != pTfContext) && SUCCEEDED(pTfContext->GetDocumentMgr(&pCandidateListDocumentMgr)))
        {
            if (pCandidateListDocumentMgr != pDocMgrFocus)
            {
                _pUIPresenter->OnKillThreadFocus();
            }
            else 
			{
				_pUIPresenter->OnSetThreadFocus();
				
            }

            pCandidateListDocumentMgr->Release();
        }
    }

    if (_pDocMgrLastFocused)
    {
        _pDocMgrLastFocused->Release();
		_pDocMgrLastFocused = nullptr;
    }

    _pDocMgrLastFocused = pDocMgrFocus;

    if (_pDocMgrLastFocused)
    {
        _pDocMgrLastFocused->AddRef();
    }

	debugPrint(L"leaving CTSFTTS::OnSetFocus()\n");
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfThreadMgrEventSink::OnPushContext
//
// Sink called by the framework when a context is pushed.
//----------------------------------------------------------------------------

STDAPI CTSFTTS::OnPushContext(_In_ ITfContext *pContext)
{
    pContext;

    return E_NOTIMPL;
}

//+---------------------------------------------------------------------------
//
// ITfThreadMgrEventSink::OnPopContext
//
// Sink called by the framework when a context is popped.
//----------------------------------------------------------------------------

STDAPI CTSFTTS::OnPopContext(_In_ ITfContext *pContext)
{
    pContext;

    return E_NOTIMPL;
}

//+---------------------------------------------------------------------------
//
// _InitThreadMgrEventSink
//
// Advise our sink.
//----------------------------------------------------------------------------

BOOL CTSFTTS::_InitThreadMgrEventSink()
{
	debugPrint(L"CTSFTTS::_InitThreadMgrEventSink()");
    ITfSource* pSource = nullptr;
    BOOL ret = FALSE;

    if (FAILED(_pThreadMgr->QueryInterface(IID_ITfSource, (void **)&pSource)))
    {
        return ret;
    }

    if (FAILED(pSource->AdviseSink(IID_ITfThreadMgrEventSink, (ITfThreadMgrEventSink *)this, &_threadMgrEventSinkCookie)))
    {
        _threadMgrEventSinkCookie = TF_INVALID_COOKIE;
        goto Exit;
    }

    ret = TRUE;

Exit:
    pSource->Release();
    return ret;
}

//+---------------------------------------------------------------------------
//
// _UninitThreadMgrEventSink
//
// Unadvise our sink.
//----------------------------------------------------------------------------

void CTSFTTS::_UninitThreadMgrEventSink()
{
	debugPrint(L"CTSFTTS::_UninitThreadMgrEventSink()");
    ITfSource* pSource = nullptr;

    if (_threadMgrEventSinkCookie == TF_INVALID_COOKIE)
    {
        return; 
    }

    if (SUCCEEDED(_pThreadMgr->QueryInterface(IID_ITfSource, (void **)&pSource)))
    {
        pSource->UnadviseSink(_threadMgrEventSinkCookie);
        pSource->Release();
    }

    _threadMgrEventSinkCookie = TF_INVALID_COOKIE;
}

