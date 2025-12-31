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


#include "private.h"
#include "TipCandidateString.h"

HRESULT CTipCandidateString::CreateInstance(_Outptr_ CTipCandidateString **ppobj)
{  
    if (ppobj == nullptr)
    {
        return E_INVALIDARG;
    }
    *ppobj = nullptr;

    *ppobj = new (std::nothrow) CTipCandidateString();
    if (*ppobj == nullptr) 
    {
        return E_OUTOFMEMORY;
    }

    return S_OK;
}

HRESULT CTipCandidateString::CreateInstance(REFIID riid, _Outptr_ void **ppvObj)
{ 
    if (ppvObj == nullptr)
    {
        return E_INVALIDARG;
    }
    *ppvObj = nullptr;

    *ppvObj = new (std::nothrow) CTipCandidateString();
    if (*ppvObj == nullptr) 
    {
        return E_OUTOFMEMORY;
    }

    return ((CTipCandidateString*)(*ppvObj))->QueryInterface(riid, ppvObj);
}

CTipCandidateString::CTipCandidateString(void)
{
    _refCount = 0;
    _index = 0;
}

CTipCandidateString::~CTipCandidateString()
{
}

// IUnknown methods
STDMETHODIMP CTipCandidateString::QueryInterface(REFIID riid, _Outptr_ void **ppvObj)
{
    if (ppvObj == nullptr)
    {
        return E_POINTER;
    }
    *ppvObj = nullptr;

    if (IsEqualIID(riid, IID_IUnknown))
    {
        *ppvObj = (CTipCandidateString*)this;
    }
    else if (IsEqualIID(riid, IID_ITfCandidateString))
    {
        *ppvObj = (CTipCandidateString*)this;
    }

    if (*ppvObj == nullptr)
    {
        return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) CTipCandidateString::AddRef(void)
{
    return (ULONG)InterlockedIncrement((LONG*)&_refCount);
}

STDMETHODIMP_(ULONG) CTipCandidateString::Release(void)
{
    ULONG refT = (ULONG)InterlockedDecrement((LONG*)&_refCount);
    if (0 < refT)
    {
        return refT;
    }

    delete this;

    return 0;
}

// ITfCandidateString methods
STDMETHODIMP CTipCandidateString::GetString(BSTR *pbstr)
{
    *pbstr = SysAllocString(_candidateStr.c_str());
    return S_OK;
}

STDMETHODIMP CTipCandidateString::GetIndex(_Out_ ULONG *pnIndex)
{
    if (pnIndex == nullptr)
    {
        return E_POINTER;
    }

    *pnIndex = _index;
    return S_OK;
}

STDMETHODIMP CTipCandidateString::SetIndex(ULONG uIndex)
{
    _index = uIndex;
    return S_OK;
}

STDMETHODIMP CTipCandidateString::SetString(_In_ const WCHAR *pch, DWORD_PTR length)
{
    _candidateStr.assign(pch, 0, length);
    return S_OK;
}
