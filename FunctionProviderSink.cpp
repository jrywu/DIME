//
//
// Derived from Microsoft Sample IME by Jeremy '13,7,17
//
//
//#define DEBUG_PRINT

#include "Private.h"
#include "DIME.h"
#include "SearchCandidateProvider.h"

//+---------------------------------------------------------------------------
//
// _InitFunctionProviderSink
//
//----------------------------------------------------------------------------

BOOL CDIME::_InitFunctionProviderSink()
{
	debugPrint(L"CDIME::_InitFunctionProviderSink()");
    ITfSourceSingle* pSourceSingle = nullptr;
    BOOL ret = FALSE;
    if (_pThreadMgr && SUCCEEDED(_pThreadMgr->QueryInterface(IID_ITfSourceSingle, (void **)&pSourceSingle)) && pSourceSingle)
    {
        IUnknown* punk = nullptr;
        if (SUCCEEDED(QueryInterface(IID_IUnknown, (void **)&punk)) && punk)
        {
            if (SUCCEEDED(pSourceSingle->AdviseSingleSink(_tfClientId, IID_ITfFunctionProvider, punk)))
            {
                if (SUCCEEDED(CSearchCandidateProvider::CreateInstance(&_pITfFnSearchCandidateProvider, (ITfTextInputProcessorEx*)this)))
                {
                    ret = TRUE;
                }
            }
            punk->Release();
        }
        pSourceSingle->Release();
    }
    return ret;
}

//+---------------------------------------------------------------------------
//
// _UninitFunctionProviderSink
//
//----------------------------------------------------------------------------

void CDIME::_UninitFunctionProviderSink()
{
    ITfSourceSingle* pSourceSingle = nullptr;
    if (_pThreadMgr && SUCCEEDED(_pThreadMgr->QueryInterface(IID_ITfSourceSingle, (void **)&pSourceSingle)) && pSourceSingle )
    {
        pSourceSingle->UnadviseSingleSink(_tfClientId, IID_ITfFunctionProvider);
        pSourceSingle->Release();
    }
}
