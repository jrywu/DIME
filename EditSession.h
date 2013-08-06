//
//
// Derived from Microsoft Sample IME by Jeremy '13,7,17
//
//
#ifndef EDITSESSION_H
#define EDITSESSION_H
#include "BaseStructure.h"
#include "Private.h"

#pragma once

class CTSFTTS;

class CEditSessionBase : public ITfEditSession
{
public:
    CEditSessionBase(_In_ CTSFTTS *pTextService, _In_ ITfContext *pContext);
    virtual ~CEditSessionBase();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, _Outptr_ void **ppvObj);
    STDMETHODIMP_(ULONG) AddRef(void);
    STDMETHODIMP_(ULONG) Release(void);

    // ITfEditSession
    virtual STDMETHODIMP DoEditSession(TfEditCookie ec) = 0;

protected:
    ITfContext *_pContext;
    CTSFTTS *_pTextService;

private:
    LONG _refCount;     // COM ref count
};
#endif