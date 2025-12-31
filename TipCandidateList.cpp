/* DIME IME for Windows 7/8/10/11

BSD 3-Clause License

Copyright (c) 2022, Jeremy Wu
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, this
   list of conditions and the following disclaimer in the documentation
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
#include "TipCandidateList.h"
#include "EnumTfCandidates.h"
#include "TipCandidateString.h"

HRESULT CTipCandidateList::CreateInstance(_Outptr_ ITfCandidateList **ppobj, size_t candStrReserveSize)
{  
    if (ppobj == nullptr)
    {
        return E_INVALIDARG;
    }
    *ppobj = nullptr;

    *ppobj = new (std::nothrow) CTipCandidateList(candStrReserveSize);
    if (*ppobj == nullptr) 
    {
        return E_OUTOFMEMORY;
    }

    return S_OK;
}

HRESULT CTipCandidateList::CreateInstance(REFIID riid, _Outptr_ void **ppvObj, size_t candStrReserveSize)
{  
    if (ppvObj == nullptr)
    {
        return E_INVALIDARG;
    }
    *ppvObj = nullptr;

    *ppvObj = new (std::nothrow) CTipCandidateList(candStrReserveSize);
    if (*ppvObj == nullptr) 
    {
        return E_OUTOFMEMORY;
    }

    return ((CTipCandidateList*)(*ppvObj))->QueryInterface(riid, ppvObj);
}

CTipCandidateList::CTipCandidateList(size_t candStrReserveSize)
{
    _refCount = 0;

    if (0 < candStrReserveSize)
    {
        _tfCandStrList.reserve(candStrReserveSize);
    }
}

CTipCandidateList::~CTipCandidateList()
{
    for (UINT i = 0; i < _tfCandStrList.Count(); i++)
    {
        ITfCandidateString** ppCandStr = _tfCandStrList.GetAt(i);
        if (ppCandStr && *ppCandStr)
        {
            (*ppCandStr)->Release();
        }
    }
    _tfCandStrList.Clear();
}

STDMETHODIMP CTipCandidateList::QueryInterface(REFIID riid, _Outptr_ void **ppvObj)
{
    if (ppvObj == nullptr)
    {
        return E_POINTER;
    }
    *ppvObj = nullptr;

    if (IsEqualIID(riid, IID_IUnknown))
    {
        *ppvObj = (ITfCandidateList*)this;
    }
    else if (IsEqualIID(riid, IID_ITfCandidateList))
    {
        *ppvObj = (ITfCandidateList*)this;
    }

    if (*ppvObj == nullptr)
    {
        return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) CTipCandidateList::AddRef()
{
    return (ULONG)InterlockedIncrement((LONG*)&_refCount);
}

STDMETHODIMP_(ULONG) CTipCandidateList::Release()
{
    ULONG  cRefT  = (ULONG)InterlockedDecrement((LONG*)&_refCount);
    if (0 < cRefT)
    {
        return cRefT;
    }

    delete this;

    return 0;
}

STDMETHODIMP CTipCandidateList::EnumCandidates(_Outptr_ IEnumTfCandidates **ppEnum)
{
    return CEnumTfCandidates::CreateInstance(IID_IEnumTfCandidates, (void**)ppEnum, _tfCandStrList);
}

STDMETHODIMP CTipCandidateList::GetCandidate(ULONG nIndex, _Outptr_result_maybenull_ ITfCandidateString **ppCandStr)
{
    if (ppCandStr == nullptr)
    {
        return E_POINTER;
    }
    *ppCandStr = nullptr;

    ULONG sizeCandStr = (ULONG)_tfCandStrList.Count();
    if (sizeCandStr <= nIndex)
    {
        return E_FAIL;
    }

    for (UINT i = 0; i < _tfCandStrList.Count(); i++)
    {
        ITfCandidateString** ppCandStrCur = _tfCandStrList.GetAt(i);
        ULONG indexCur = 0;
        if ((nullptr != ppCandStrCur) && (SUCCEEDED((*ppCandStrCur)->GetIndex(&indexCur))))
        {
            if (nIndex == indexCur)
            {
                BSTR bstr=L"";
                CTipCandidateString* pTipCandidateStrCur = (CTipCandidateString*)(*ppCandStrCur);
                if(pTipCandidateStrCur) 
					pTipCandidateStrCur->GetString(&bstr);

                CTipCandidateString::CreateInstance(IID_ITfCandidateString, (void**)ppCandStr);

                if (nullptr != (*ppCandStr))
                {
                    CTipCandidateString* pTipCandidateStr = (CTipCandidateString*)(*ppCandStr);
                    if (pTipCandidateStr)
                    {
                        pTipCandidateStr->SetString((LPCWSTR)bstr, SysStringLen(bstr));
                    }
                }

                SysFreeString(bstr); // Free BSTR after use
                break;
            }
        }
    }
    return S_OK;
}

STDMETHODIMP CTipCandidateList::GetCandidateNum(_Out_ ULONG *pnCnt)
{
    if (pnCnt == nullptr)
    {
        return E_POINTER;
    }

    *pnCnt = (ULONG)(_tfCandStrList.Count());
    return S_OK;
}

STDMETHODIMP CTipCandidateList::SetResult(ULONG nIndex, TfCandidateResult imcr)
{
    nIndex;imcr;

    return E_NOTIMPL;
}

STDMETHODIMP CTipCandidateList::SetCandidate(_In_ ITfCandidateString **ppCandStr)
{
    if (ppCandStr == nullptr)
    {
        return E_POINTER;
    }

    // Use smart pointer to manage the candidate string
    std::unique_ptr<ITfCandidateString*> candidate(new (std::nothrow) ITfCandidateString*(*ppCandStr));
    if (!candidate)
    {
        return E_OUTOFMEMORY;
    }

    ITfCandidateString** ppCandLast = _tfCandStrList.Append();
    if (ppCandLast)
    {
        *ppCandLast = *candidate.release(); // Transfer ownership
    }
    else
    {
        return E_OUTOFMEMORY;
    }

    return S_OK;
}
