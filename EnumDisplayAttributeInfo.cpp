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
#include "globals.h"
#include "DIME.h"
#include "DisplayAttributeInfo.h"
#include "EnumDisplayAttributeInfo.h"

//+---------------------------------------------------------------------------
//
// ctor
//
//----------------------------------------------------------------------------

CEnumDisplayAttributeInfo::CEnumDisplayAttributeInfo()
{
    DllAddRef();

    _index = 0;
    _refCount = 1;
}

//+---------------------------------------------------------------------------
//
// dtor
//
//----------------------------------------------------------------------------

CEnumDisplayAttributeInfo::~CEnumDisplayAttributeInfo()
{
    DllRelease();
}

//+---------------------------------------------------------------------------
//
// QueryInterface
//
//----------------------------------------------------------------------------

STDAPI CEnumDisplayAttributeInfo::QueryInterface(REFIID riid, _Outptr_ void **ppvObj)
{
    if (ppvObj == nullptr)
        return E_INVALIDARG;

    *ppvObj = nullptr;

    if (IsEqualIID(riid, IID_IUnknown) ||
        IsEqualIID(riid, IID_IEnumTfDisplayAttributeInfo))
    {
        *ppvObj = (IEnumTfDisplayAttributeInfo *)this;
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

STDAPI_(ULONG) CEnumDisplayAttributeInfo::AddRef()
{
    return ++_refCount;
}

//+---------------------------------------------------------------------------
//
// Release
//
//----------------------------------------------------------------------------

STDAPI_(ULONG) CEnumDisplayAttributeInfo::Release()
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
// IEnumTfDisplayAttributeInfo::Clone
//
// Returns a copy of the object.
//----------------------------------------------------------------------------

STDAPI CEnumDisplayAttributeInfo::Clone(_Out_ IEnumTfDisplayAttributeInfo **ppEnum)
{
    CEnumDisplayAttributeInfo* pClone = nullptr;

    if (ppEnum == nullptr)
    {
        return E_INVALIDARG;
    }

    *ppEnum = nullptr;

    pClone = new (std::nothrow) CEnumDisplayAttributeInfo();
    if ((pClone) == nullptr)
    {
        return E_OUTOFMEMORY;
    }

    // the clone should match this object's state
    pClone->_index = _index;

    *ppEnum = pClone;

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// IEnumTfDisplayAttributeInfo::Next
//
// Returns an array of display attribute info objects supported by this service.
//----------------------------------------------------------------------------

const int MAX_DISPLAY_ATTRIBUTE_INFO = 2;

STDAPI CEnumDisplayAttributeInfo::Next(ULONG ulCount, __RPC__out_ecount_part(ulCount, *pcFetched) ITfDisplayAttributeInfo **rgInfo, _Inout_opt_ ULONG *pcFetched)
{
    ULONG fetched = 0;

    
    if (ulCount == 0)
    {
        return S_OK;
    }
    if (rgInfo == nullptr)
    {
        return E_INVALIDARG;
    }
    *rgInfo = nullptr;

    while (fetched < ulCount)
    {
        ITfDisplayAttributeInfo* pDisplayAttributeInfo = nullptr;

        if (_index == 0)
        {   
            pDisplayAttributeInfo = new (std::nothrow) CDisplayAttributeInfoInput();
            if ((pDisplayAttributeInfo) == nullptr)
            {
                return E_OUTOFMEMORY;
            }
        }
        else if (_index == 1)
        {
            pDisplayAttributeInfo = new (std::nothrow) CDisplayAttributeInfoConverted();
            if ((pDisplayAttributeInfo) == nullptr)
            {
                return E_OUTOFMEMORY;
            }

        }
        else
        {
            break;
        }

        *rgInfo = pDisplayAttributeInfo;
        rgInfo++;
        fetched++;
        _index++;
    }

    if (pcFetched != nullptr)
    {
        // technically this is only legal if ulCount == 1, but we won't check
        *pcFetched = fetched;
    }

    return (fetched == ulCount) ? S_OK : S_FALSE;
}

//+---------------------------------------------------------------------------
//
// IEnumTfDisplayAttributeInfo::Reset
//
// Resets the enumeration.
//----------------------------------------------------------------------------

STDAPI CEnumDisplayAttributeInfo::Reset()
{
    _index = 0;
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// IEnumTfDisplayAttributeInfo::Skip
//
// Skips past objects in the enumeration.
//----------------------------------------------------------------------------

STDAPI CEnumDisplayAttributeInfo::Skip(ULONG ulCount)
{
    if ((ulCount + _index) > MAX_DISPLAY_ATTRIBUTE_INFO || (ulCount + _index) < ulCount)
    {
        _index = MAX_DISPLAY_ATTRIBUTE_INFO;
        return S_FALSE;
    }
    _index += ulCount;
    return S_OK;
}
