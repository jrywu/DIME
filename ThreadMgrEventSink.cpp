//
//
// Derived from Microsoft Sample IME by Jeremy '13,7,17
//
//
//#define DEBUG_PRINT

#include "Private.h"
#include "Globals.h"
#include "DIME.h"
#include "UIPresenter.h"
#include "GetTextExtentEditSession.h"

//+---------------------------------------------------------------------------
//
// ITfThreadMgrEventSink::OnInitDocumentMgr
//
// Sink called by the framework just before the first context is pushed onto
// a document.
//----------------------------------------------------------------------------

STDAPI CDIME::OnInitDocumentMgr(_In_ ITfDocumentMgr *pDocMgr)
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

STDAPI CDIME::OnUninitDocumentMgr(_In_ ITfDocumentMgr *pDocMgr)
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

STDAPI CDIME::OnSetFocus(_In_ ITfDocumentMgr *pDocMgrFocus, _In_ ITfDocumentMgr *pDocMgrPrevFocus)
{
	debugPrint(L"CDIME::OnSetFocus() _isChinese = %d, _lastKeyboardMode = %d\n", _isChinese, _lastKeyboardMode);
    pDocMgrPrevFocus;
	ITfContext* pContext = nullptr;

	_InitTextEditSink(pDocMgrFocus);
	
	if(pDocMgrFocus && !_UpdateLanguageBarOnSetFocus(pDocMgrFocus))
	{
		pDocMgrFocus->GetTop(&pContext);	

		//Update double single byte mode
		if (CConfig::GetDoubleSingleByteMode() != DOUBLE_SINGLE_BYTE_SHIFT_SPACE)
		{
			CCompartment CompartmentDoubleSingleByte(_pThreadMgr, _tfClientId, Global::DIMEGuidCompartmentDoubleSingleByte);
			CompartmentDoubleSingleByte._SetCompartmentBOOL(CConfig::GetDoubleSingleByteMode() == DOUBLE_SINGLE_BYTE_ALWAYS_DOUBLE);
		}

		//Update keyboard mode
		if (pContext && !(_newlyActivated && !Global::isWindows8 && isWin7IEProcess()))
		{	

			debugPrint(L"CDIME::OnSetFocus() Set isChinese = lastkeyboardMode = %d,", _isChinese);
			_lastKeyboardMode = _isChinese;
			debugPrint(L"CDIME::OnSetFocus() Set keyboard mode to last state = %d", _lastKeyboardMode);
			ConversionModeCompartmentUpdated(_pThreadMgr, &_lastKeyboardMode);
			debugPrint(L"CDIME::OnSetFocus() show chi/eng notify _isChinese = %d", _isChinese);
			if (!isBlackListedProcessForProbeComposition()) 	
				showChnEngNotify(_isChinese, 500);  //only show chn/eng notify for processes no black listed (may cause strange behaviour like firefox).
			
		}
		else if (_newlyActivated)
			_newlyActivated = FALSE;
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
        if (pTfContext && 
			SUCCEEDED(pTfContext->GetDocumentMgr(&pCandidateListDocumentMgr) && pCandidateListDocumentMgr ))
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

	debugPrint(L"leaving CDIME::OnSetFocus()\n");
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfThreadMgrEventSink::OnPushContext
//
// Sink called by the framework when a context is pushed.
//----------------------------------------------------------------------------

STDAPI CDIME::OnPushContext(_In_ ITfContext *pContext)
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

STDAPI CDIME::OnPopContext(_In_ ITfContext *pContext)
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

BOOL CDIME::_InitThreadMgrEventSink()
{
	debugPrint(L"CDIME::_InitThreadMgrEventSink()");
    ITfSource* pSource = nullptr;
    BOOL ret = FALSE;

    if ( (_pThreadMgr && FAILED(_pThreadMgr->QueryInterface(IID_ITfSource, (void **)&pSource))) || pSource==nullptr)
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

void CDIME::_UninitThreadMgrEventSink()
{
	debugPrint(L"CDIME::_UninitThreadMgrEventSink()");
    ITfSource* pSource = nullptr;

    if (_threadMgrEventSinkCookie == TF_INVALID_COOKIE)
    {
        return; 
    }

    if ( _pThreadMgr && SUCCEEDED(_pThreadMgr->QueryInterface(IID_ITfSource, (void **)&pSource)) && pSource)
    {
        pSource->UnadviseSink(_threadMgrEventSinkCookie);
        pSource->Release();
    }

    _threadMgrEventSinkCookie = TF_INVALID_COOKIE;
}

