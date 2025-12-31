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
#include "EnumTfCandidates.h"

HRESULT CEnumTfCandidates::CreateInstance(_Out_ CEnumTfCandidates **ppobj, _In_ const CDIMEArray<ITfCandidateString*> &rgelm, UINT currentNum)
{
    if (ppobj == nullptr)
    {
        return E_INVALIDARG;
    }
    *ppobj = nullptr;

    *ppobj = new (std::nothrow) CEnumTfCandidates(rgelm, currentNum);
    if (*ppobj == nullptr) 
    {
        return E_OUTOFMEMORY;
    }

    return S_OK;
}

HRESULT CEnumTfCandidates::CreateInstance(REFIID riid, _Out_ void **ppvObj, _In_ const CDIMEArray<ITfCandidateString*> &rgelm, UINT currentNum)
{
    if (ppvObj == nullptr)
    {
        return E_POINTER;
    }
    *ppvObj = nullptr;

    *ppvObj = new (std::nothrow) CEnumTfCandidates(rgelm, currentNum);
    if (*ppvObj == nullptr) 
    {
        return E_OUTOFMEMORY;
    }

    return ((CEnumTfCandidates*)(*ppvObj))->QueryInterface(riid, ppvObj);
}

CEnumTfCandidates::CEnumTfCandidates(_In_ const CDIMEArray<ITfCandidateString*> &rgelm, UINT currentNum)
{
    _refCount = 0;
    _rgelm = rgelm;
    _currentCandidateStrIndex = currentNum;
}

CEnumTfCandidates::~CEnumTfCandidates()
{
}

//
// IUnknown methods
//
STDMETHODIMP CEnumTfCandidates::QueryInterface(REFIID riid, _Outptr_ void **ppvObj)
{
    if (ppvObj == nullptr)
    {
        return E_POINTER;
    }
    *ppvObj = nullptr;

    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, __uuidof(IEnumTfCandidates)))
    {
        *ppvObj = (IEnumTfCandidates*)this;
    }

    if (*ppvObj == nullptr)
    {
        return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) CEnumTfCandidates::AddRef()
{
    return (ULONG)InterlockedIncrement(&_refCount);
}

STDMETHODIMP_(ULONG) CEnumTfCandidates::Release()
{
    ULONG cRef = (ULONG)InterlockedDecrement(&_refCount);
    if (0 < cRef)
    {
        return cRef;
    }

    delete this;
    return 0;
}


//
// IEnumTfCandidates methods
//
STDMETHODIMP CEnumTfCandidates::Next(ULONG ulCount, _Out_ ITfCandidateString **ppObj, _Out_ ULONG *pcFetched)
{
    ULONG fetched = 0;
    if (ppObj == nullptr)
    {
        return E_INVALIDARG;
    }
    *ppObj = nullptr;

    while ((fetched < ulCount) && (_currentCandidateStrIndex < _rgelm.Count()))
    {
        *ppObj = *_rgelm.GetAt(_currentCandidateStrIndex);
        _currentCandidateStrIndex++;
        fetched++;
    }

    if (pcFetched)
    {
        *pcFetched = fetched;
    }

    return (fetched == ulCount) ? S_OK : S_FALSE;
}

STDMETHODIMP CEnumTfCandidates::Skip(ULONG ulCount)
{
    while ((0 < ulCount) && (_currentCandidateStrIndex < _rgelm.Count()))
    {
        _currentCandidateStrIndex++;
        ulCount--;
    }

    return (0 < ulCount) ? S_FALSE : S_OK;
}

STDMETHODIMP CEnumTfCandidates::Reset()
{
    _currentCandidateStrIndex = 0;
    return S_OK;
}

STDMETHODIMP CEnumTfCandidates::Clone(_Out_ IEnumTfCandidates **ppEnum)
{
    return CreateInstance(__uuidof(IEnumTfCandidates), (void**)ppEnum, _rgelm, _currentCandidateStrIndex);
}