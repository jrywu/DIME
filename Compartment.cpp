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


#include "Private.h"
#include "Compartment.h"
#include "Globals.h"

//////////////////////////////////////////////////////////////////////
//
// CCompartment
//
//////////////////////////////////////////////////////////////////////+---------------------------------------------------------------------------
// ctor
//----------------------------------------------------------------------------

CCompartment::CCompartment(_In_ IUnknown* punk, TfClientId tfClientId, _In_ REFGUID guidCompartment)
{
    _guidCompartment = guidCompartment;

    _punk = punk;
	if(_punk)
		_punk->AddRef();

    _tfClientId = tfClientId;
}

//+---------------------------------------------------------------------------
// dtor
//----------------------------------------------------------------------------

CCompartment::~CCompartment()
{
    if(_punk)
		_punk->Release();
}

//+---------------------------------------------------------------------------
// _GetCompartment
//----------------------------------------------------------------------------

HRESULT CCompartment::_GetCompartment(_Inout_opt_ ITfCompartment **ppCompartment)
{
    HRESULT hr = S_OK;
    ITfCompartmentMgr* pCompartmentMgr = nullptr;

	if(_punk)
		hr = _punk->QueryInterface(IID_ITfCompartmentMgr, (void **)&pCompartmentMgr);
    if (SUCCEEDED(hr) && pCompartmentMgr)
    {
		if(ppCompartment)
			hr = pCompartmentMgr->GetCompartment(_guidCompartment, ppCompartment);
        pCompartmentMgr->Release();
    }

    return hr;
}

//+---------------------------------------------------------------------------
// _GetCompartmentBOOL
//----------------------------------------------------------------------------

HRESULT CCompartment::_GetCompartmentBOOL(_Out_ BOOL &flag)
{
    HRESULT hr = S_OK;
    ITfCompartment* pCompartment = nullptr;
    flag = FALSE;

    if ((hr = _GetCompartment(&pCompartment)) == S_OK && pCompartment)
    {
        VARIANT var;
        VariantInit(&var); // Initialize the VARIANT structure
        if ((hr = pCompartment->GetValue(&var)) == S_OK)
        {
            if (var.vt == VT_I4) // Even VT_EMPTY, GetValue() can succeed
            {
                flag = (BOOL)var.lVal;
            }
            else
            {
                hr = S_FALSE;
            }
        }
        pCompartment->Release();
    }

    return hr;
}

//+---------------------------------------------------------------------------
// _SetCompartmentBOOL
//----------------------------------------------------------------------------

HRESULT CCompartment::_SetCompartmentBOOL(_In_ BOOL flag)
{
    HRESULT hr = S_OK;
    ITfCompartment* pCompartment = nullptr;

    hr = _GetCompartment(&pCompartment);
    if (SUCCEEDED(hr) && pCompartment)
    {
        VARIANT var;
        VariantInit(&var); // Initialize the VARIANT structure
        var.vt = VT_I4;
        var.lVal = flag;
        hr = pCompartment->SetValue(_tfClientId, &var);
        pCompartment->Release();
    }

    return hr;
}

//+---------------------------------------------------------------------------
// _GetCompartmentDWORD
//----------------------------------------------------------------------------

HRESULT CCompartment::_GetCompartmentDWORD(_Out_ DWORD &dw)
{
    HRESULT hr = S_OK;
    ITfCompartment* pCompartment = nullptr;
    dw = 0;

    hr = _GetCompartment(&pCompartment);
    if (SUCCEEDED(hr) && pCompartment)
    {
        VARIANT var;
        VariantInit(&var); // Initialize the VARIANT structure
        if ((hr = pCompartment->GetValue(&var)) == S_OK)
        {
            if (var.vt == VT_I4) // Even VT_EMPTY, GetValue() can succeed
            {
                dw = (DWORD)var.lVal;
            }
            else
            {
                hr = S_FALSE;
            }
        }
        pCompartment->Release();
    }

    return hr;
}

//+---------------------------------------------------------------------------
// _SetCompartmentDWORD
//----------------------------------------------------------------------------

HRESULT CCompartment::_SetCompartmentDWORD(_In_ DWORD dw)
{
    HRESULT hr = S_OK;
    ITfCompartment* pCompartment = nullptr;

    hr = _GetCompartment(&pCompartment);
    if (SUCCEEDED(hr) && pCompartment)
    {
        VARIANT var;
        VariantInit(&var); // Initialize the VARIANT structure
        var.vt = VT_I4;
        var.lVal = dw;
        hr = pCompartment->SetValue(_tfClientId, &var);
        pCompartment->Release();
    }

    return hr;
}

//+---------------------------------------------------------------------------
//
// _ClearCompartment
//
//----------------------------------------------------------------------------

HRESULT CCompartment::_ClearCompartment()
{
    if (IsEqualGUID(_guidCompartment, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE))
    {
        return S_FALSE;
    }

    HRESULT hr = S_OK;
    ITfCompartmentMgr* pCompartmentMgr = nullptr;

    if ((hr = _punk->QueryInterface(IID_ITfCompartmentMgr, (void **)&pCompartmentMgr)) == S_OK && pCompartmentMgr)
    {
        hr = pCompartmentMgr->ClearCompartment(_tfClientId, _guidCompartment);
        pCompartmentMgr->Release();
    }

    return hr;
}

//////////////////////////////////////////////////////////////////////
//
// CCompartmentEventSink
//
//////////////////////////////////////////////////////////////////////+---------------------------------------------------------------------------
// ctor
//----------------------------------------------------------------------------

CCompartmentEventSink::CCompartmentEventSink(_In_ CESCALLBACK pfnCallback, _In_ void *pv)
    : _pCompartment(nullptr), _dwCookie(0), _pfnCallback(pfnCallback), _pv(pv), _refCount(1)
{
}

//+---------------------------------------------------------------------------
// dtor
//----------------------------------------------------------------------------

CCompartmentEventSink::~CCompartmentEventSink()
{
}

//+---------------------------------------------------------------------------
//
// QueryInterface
//
//----------------------------------------------------------------------------

STDAPI CCompartmentEventSink::QueryInterface(REFIID riid, _Outptr_ void **ppvObj)
{
    if (ppvObj == nullptr)
        return E_INVALIDARG;

    *ppvObj = nullptr;

    if (IsEqualIID(riid, IID_IUnknown) ||
        IsEqualIID(riid, IID_ITfCompartmentEventSink))
    {
        *ppvObj = (CCompartmentEventSink *)this;
    }

    if (*ppvObj)
    {
        AddRef();
        return S_OK;
    }

    return E_NOINTERFACE;
}


//+---------------------------------------------------------------------------
//
// AddRef
//
//----------------------------------------------------------------------------

STDAPI_(ULONG) CCompartmentEventSink::AddRef()
{
    return ++_refCount;
}

//+---------------------------------------------------------------------------
//
// Release
//
//----------------------------------------------------------------------------

STDAPI_(ULONG) CCompartmentEventSink::Release()
{
    LONG cr = --_refCount;

    assert(_refCount >= 0);

    if (_refCount == 0)
    {
        delete this;
    }

    return cr;
}

//+---------------------------------------------------------------------------
//
// OnChange
//
//----------------------------------------------------------------------------

STDAPI CCompartmentEventSink::OnChange(_In_ REFGUID guidCompartment)
{
    return _pfnCallback(_pv, guidCompartment);
}

//+---------------------------------------------------------------------------
//
// _Advise
//
//----------------------------------------------------------------------------

HRESULT CCompartmentEventSink::_Advise(_In_ IUnknown *punk, _In_ REFGUID guidCompartment)
{
    HRESULT hr = S_OK;
    ITfCompartmentMgr* pCompartmentMgr = nullptr;
    ITfSource* pSource = nullptr;

    hr = punk->QueryInterface(IID_ITfCompartmentMgr, (void **)&pCompartmentMgr);
    if (FAILED(hr) || pCompartmentMgr==nullptr)
    {
        return hr;
    }

    hr = pCompartmentMgr->GetCompartment(guidCompartment, &_pCompartment);
    if (SUCCEEDED(hr) && _pCompartment)
    {
        hr = _pCompartment->QueryInterface(IID_ITfSource, (void **)&pSource);
        if (SUCCEEDED(hr) && pSource)
        {
            hr = pSource->AdviseSink(IID_ITfCompartmentEventSink, this, &_dwCookie);
            pSource->Release();
        }
    }

    pCompartmentMgr->Release();

    return hr;
}

//+---------------------------------------------------------------------------
//
// _Unadvise
//
//----------------------------------------------------------------------------

HRESULT CCompartmentEventSink::_Unadvise()
{
    HRESULT hr = S_OK;
    ITfSource* pSource = nullptr;

	if (_pCompartment==nullptr)
    {
        return hr;
    }

    hr = _pCompartment->QueryInterface(IID_ITfSource, (void **)&pSource);
    if (SUCCEEDED(hr) && pSource)
    {
        hr = pSource->UnadviseSink(_dwCookie);
        pSource->Release();
    }

    _pCompartment->Release();
    _pCompartment = nullptr;
    _dwCookie = 0;

    return hr;
}
