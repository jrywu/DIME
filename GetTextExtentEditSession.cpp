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
	debugPrint(L"CGetTextExtentEditSession::DoEditSession()\n");
    RECT rc = {0, 0, 0, 0};
    BOOL isClipped = TRUE;
	

    if (SUCCEEDED(_pContextView->GetTextExt(ec, _pRangeComposition, &rc, &isClipped)))
    {
		_pTfTextLayoutSink->_LayoutChangeNotification(&rc);
		//_pTextService->_HandlTextLayoutChange(ec, _pContext, _pRangeComposition);
        
    }

    return S_OK;
}
