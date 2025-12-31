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
#include "SearchCandidateProvider.h"
#include <new>
#include "DIME.h"
#include "CompositionProcessorEngine.h"
#include "TipCandidateList.h"
#include "TipCandidateString.h"

/*------------------------------------------------------------------------------

create instance of CSearchCandidateProvider

------------------------------------------------------------------------------*/
HRESULT CSearchCandidateProvider::CreateInstance(_Outptr_ ITfFnSearchCandidateProvider **ppobj, _In_ ITfTextInputProcessorEx *ptip)
{  
    if (ppobj == nullptr)
    {
        return E_INVALIDARG;
    }
    *ppobj = nullptr;

    *ppobj = new (std::nothrow) CSearchCandidateProvider(ptip);
    if (nullptr == *ppobj)
    {
        return E_OUTOFMEMORY;
    }

    return S_OK;
}

/*------------------------------------------------------------------------------

create instance of CSearchCandidateProvider

------------------------------------------------------------------------------*/
HRESULT CSearchCandidateProvider::CreateInstance(REFIID riid, _Outptr_ void **ppvObj, _In_ ITfTextInputProcessorEx *ptip)
{ 
    if (ppvObj == nullptr)
    {
        return E_INVALIDARG;
    }
    *ppvObj = nullptr;

    *ppvObj = new (std::nothrow) CSearchCandidateProvider(ptip);
    if (nullptr == *ppvObj)
    {
        return E_OUTOFMEMORY;
    }

    return ((CSearchCandidateProvider*)(*ppvObj))->QueryInterface(riid, ppvObj);
}

/*------------------------------------------------------------------------------

constructor of CSearchCandidateProvider

------------------------------------------------------------------------------*/
CSearchCandidateProvider::CSearchCandidateProvider(_In_ ITfTextInputProcessorEx *ptip)
{
    assert(ptip != nullptr);

    _pTip = ptip;
    _refCount = 0;
}

/*------------------------------------------------------------------------------

destructor of CSearchCandidateProvider

------------------------------------------------------------------------------*/
CSearchCandidateProvider::~CSearchCandidateProvider(void)
{  
}

/*------------------------------------------------------------------------------

query interface
(IUnknown method)

------------------------------------------------------------------------------*/
STDMETHODIMP CSearchCandidateProvider::QueryInterface(REFIID riid, _Outptr_ void **ppvObj)
{
    if (ppvObj == nullptr)
    {
        return E_POINTER;
    }
    *ppvObj = nullptr;

    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, __uuidof(ITfFnSearchCandidateProvider)))
    {
        *ppvObj = (ITfFnSearchCandidateProvider*)this;
    }
    else if (IsEqualIID(riid, IID_ITfFunction))
    {
        *ppvObj = (ITfFunction*)this;
    }

    if (*ppvObj == nullptr)
    {
        return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
}

/*------------------------------------------------------------------------------

increment reference count
(IUnknown method)

------------------------------------------------------------------------------*/
STDMETHODIMP_(ULONG) CSearchCandidateProvider::AddRef()
{
    return (ULONG)InterlockedIncrement(&_refCount);
}

/*------------------------------------------------------------------------------

decrement reference count and release object
(IUnknown method)

------------------------------------------------------------------------------*/
STDMETHODIMP_(ULONG) CSearchCandidateProvider::Release()
{
    ULONG ref = (ULONG)InterlockedDecrement(&_refCount);
    if (0 < ref)
    {
        return ref;
    }

    delete this;
    return 0;
}

STDMETHODIMP CSearchCandidateProvider::GetDisplayName(_Out_ BSTR *pbstrName)
{
    if (pbstrName == nullptr)
    {
        return E_INVALIDARG;
    }

    *pbstrName = SysAllocString(L"SearchCandidateProvider");
    return  S_OK;
}

STDMETHODIMP CSearchCandidateProvider::GetSearchCandidates(BSTR bstrQuery, BSTR bstrApplicationID, _Outptr_result_maybenull_ ITfCandidateList **pplist)
{
	bstrApplicationID;bstrQuery;
    HRESULT hr = E_FAIL;
    *pplist = nullptr;

    if (nullptr == _pTip)
    {
        return hr;
    }

    CCompositionProcessorEngine* pCompositionProcessorEngine = ((CDIME*)_pTip)->GetCompositionProcessorEngine();
    if (nullptr == pCompositionProcessorEngine)
    {
        return hr;
    }

    CDIMEArray<CCandidateListItem> candidateList;
    pCompositionProcessorEngine->GetCandidateList(&candidateList, TRUE, FALSE);

    int cCand = min(candidateList.Count(), FAKECANDIDATENUMBER);
    if (0 < cCand)
    {
        hr = CTipCandidateList::CreateInstance(pplist, cCand);
		if (FAILED(hr))
		{
			return hr;
		}
        for (int iCand = 0; iCand < cCand; iCand++)
        {
            ITfCandidateString* pCandStr = nullptr;
            CTipCandidateString::CreateInstance(IID_ITfCandidateString, (void**)&pCandStr);
			if(pCandStr)
			{
				((CTipCandidateString*)pCandStr)->SetIndex(iCand);
				((CTipCandidateString*)pCandStr)->SetString(candidateList.GetAt(iCand)->_ItemString.Get(), candidateList.GetAt(iCand)->_ItemString.GetLength());
			}
			if(*pplist)
				((CTipCandidateList*)(*pplist))->SetCandidate(&pCandStr);
			
        }
    }
    hr = S_OK;

    return hr;
}

/*------------------------------------------------------------------------------

set result
(ITfFnSearchCandidateProvider method)

------------------------------------------------------------------------------*/
STDMETHODIMP CSearchCandidateProvider::SetResult(BSTR bstrQuery, BSTR bstrApplicationID, BSTR bstrResult)
{
    bstrQuery;bstrApplicationID;bstrResult;

    return E_NOTIMPL;
}

