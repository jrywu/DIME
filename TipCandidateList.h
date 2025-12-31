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

#include "BaseStructure.h"

//
// CTipCandidateList
//

class CTipCandidateList : public ITfCandidateList
{
protected:
    CTipCandidateList(size_t candStrReserveSize);
    virtual ~CTipCandidateList();

public:
    static HRESULT CreateInstance(_Outptr_ ITfCandidateList **ppobj, size_t candStrReserveSize = 0);
    static HRESULT CreateInstance(REFIID riid, _Outptr_ void **ppvObj, size_t candStrReserveSize = 0);

    // IUnknown methods
    virtual STDMETHODIMP QueryInterface(REFIID riid, _Outptr_ void **ppvObj);
    virtual STDMETHODIMP_(ULONG) AddRef();
    virtual STDMETHODIMP_(ULONG) Release();

    // ITfCandidateList methods
    virtual STDMETHODIMP EnumCandidates(_Outptr_ IEnumTfCandidates **ppEnum);
    virtual STDMETHODIMP GetCandidate(ULONG nIndex, _Outptr_result_maybenull_ ITfCandidateString **ppCandStr);
    virtual STDMETHODIMP GetCandidateNum(_Out_ ULONG *pnCnt);
    virtual STDMETHODIMP SetResult(ULONG nIndex, TfCandidateResult imcr);

    virtual STDMETHODIMP SetCandidate(_In_ ITfCandidateString **ppCandStr);

protected:
    long _refCount;
    CDIMEArray<ITfCandidateString*> _tfCandStrList;
};

