//
//
// Derived from Microsoft Sample IME by Jeremy '13,7,17
//
//


#pragma once

#include "EditSession.h"

class CTSFDayi;
class CTfTextLayoutSink;

//////////////////////////////////////////////////////////////////////
//
//    ITfEditSession
//        CEditSessionBase
// CGetTextExtentEditSession class
//
//////////////////////////////////////////////////////////////////////+---------------------------------------------------------------------------
//
// CGetTextExtentEditSession
//
//----------------------------------------------------------------------------

class CGetTextExtentEditSession : public CEditSessionBase
{
public:
    CGetTextExtentEditSession(_In_ CTSFDayi *pTextService, _In_ ITfContext *pContext, _In_ ITfContextView *pContextView, _In_ ITfRange *pRangeComposition, _In_ CTfTextLayoutSink *pTextLayoutSink);

    // ITfEditSession
    STDMETHODIMP DoEditSession(TfEditCookie ec);

private:
    ITfContextView* _pContextView;
    ITfRange* _pRangeComposition;
    CTfTextLayoutSink* _pTfTextLayoutSink;
};
