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


#pragma once

class CCompartment
{
public:
    CCompartment(_In_ IUnknown *punk, TfClientId tfClientId, _In_ REFGUID guidCompartment);
    ~CCompartment();

    HRESULT _GetCompartment(_Inout_opt_ ITfCompartment **ppCompartment);
    HRESULT _GetCompartmentBOOL(_Out_ BOOL &flag);
    HRESULT _SetCompartmentBOOL(_In_ BOOL flag);
    HRESULT _GetCompartmentDWORD(_Out_ DWORD &dw);
    HRESULT _SetCompartmentDWORD(_In_ DWORD dw);
    HRESULT _ClearCompartment();

    VOID _GetGUID(GUID *pguid)
    {
        *pguid = _guidCompartment;
    }

private:
    GUID _guidCompartment;
    IUnknown* _punk;
    TfClientId _tfClientId;
};

typedef HRESULT (*CESCALLBACK)(void *pv, REFGUID guidCompartment);

class CCompartmentEventSink : public ITfCompartmentEventSink
{
public:
    CCompartmentEventSink(_In_ CESCALLBACK pfnCallback, _In_ void *pv);
    ~CCompartmentEventSink();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, _Outptr_ void **ppvObj);
    STDMETHODIMP_(ULONG) AddRef(void);
    STDMETHODIMP_(ULONG) Release(void);

    // ITfCompartmentEventSink
    STDMETHODIMP OnChange(_In_ REFGUID guid);

    // function
    HRESULT _Advise(_In_ IUnknown *punk, _In_ REFGUID guidCompartment);
    HRESULT _Unadvise();

private:
    ITfCompartment *_pCompartment;
    DWORD _dwCookie;
    CESCALLBACK _pfnCallback;
    void *_pv;

    LONG _refCount;
};
