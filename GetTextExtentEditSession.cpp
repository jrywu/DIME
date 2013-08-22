//
//
// Derived from Microsoft Sample IME by Jeremy '13,7,17
//
//


#include "Private.h"
#include "EditSession.h"
#include "GetTextExtentEditSession.h"
#include "TfTextLayoutSink.h"
#include "TSFTTS.h"

//+---------------------------------------------------------------------------
//
// ctor
//
//----------------------------------------------------------------------------

CGetTextExtentEditSession::CGetTextExtentEditSession(_In_ CTSFTTS *pTextService, _In_ ITfContext *pContext, _In_ ITfContextView *pContextView, _In_ ITfRange *pRangeComposition, _In_ CTfTextLayoutSink *pTfTextLayoutSink) : CEditSessionBase(pTextService, pContext)
{
	debugPrint(L"CGetTextExtentEditSession::CGetTextExtentEditSession(): pContext = %x, pTfTextLayoutSink = %x \n",pContext, pTfTextLayoutSink);
	_pTextService = pTextService;
    _pContextView = pContextView;
    _pRangeComposition = pRangeComposition;
    _pTfTextLayoutSink = pTfTextLayoutSink;
}

//+---------------------------------------------------------------------------
//
// ITfEditSession::DoEditSession
//
//----------------------------------------------------------------------------

STDAPI CGetTextExtentEditSession::DoEditSession(TfEditCookie ec)
{
	debugPrint(L"CGetTextExtentEditSession::DoEditSession() ec = %x \n", ec);
    RECT rc = {0, 0, 0, 0};
    BOOL isClipped = TRUE;
	

    if (SUCCEEDED(_pContextView->GetTextExt(ec, _pRangeComposition, &rc, &isClipped)))
    {
		if(_pTfTextLayoutSink)
			_pTfTextLayoutSink->_LayoutChangeNotification(&rc);
		//if(_pTextService)
			//_pTextService->_LayoutChangeNotification(ec, _pContext, &rc);
        
    }

    return S_OK;
}
