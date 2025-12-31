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
#include "DisplayAttributeInfo.h"
#include "TfInputProcessorProfile.h"

//+---------------------------------------------------------------------------
//
// The registry key and values
//
//----------------------------------------------------------------------------

// the registry values of the custmized display attributes
const WCHAR CDisplayAttributeInfoInput::_s_szValueName[] = L"DisplayAttributeInput";
const WCHAR CDisplayAttributeInfoConverted::_s_szValueName[] = L"DisplayAttributeConverted";

// The descriptions
const WCHAR CDisplayAttributeInfoInput::_s_szDescription[] = L"DIME Text Service Display Attribute Input";
const WCHAR CDisplayAttributeInfoConverted::_s_szDescription[] = L"DIME Text Service Display Attribute Converted";

//+---------------------------------------------------------------------------
//
// DisplayAttribute
//
//----------------------------------------------------------------------------

const TF_DISPLAYATTRIBUTE CDisplayAttributeInfoInput::_s_DisplayAttribute =
{
	{ TF_CT_NONE, 0 },						// text color
    { TF_CT_NONE, 0 },                      // background color (TF_CT_NONE => app default)
    TF_LS_DOT,								// underline style
    FALSE,                                  // underline boldness
	{ TF_CT_NONE, 0 },						// underline color
    TF_ATTR_INPUT                           // attribute info
};

const TF_DISPLAYATTRIBUTE CDisplayAttributeInfoConverted::_s_DisplayAttribute =
{
    { TF_CT_COLORREF, RGB(255, 255, 255) }, // text color
    { TF_CT_COLORREF, RGB( 0, 103, 206) },  // background color (TF_CT_NONE => app default)
    TF_LS_NONE,                             // underline style
    FALSE,                                  // underline boldness
    { TF_CT_NONE, 0 },                      // underline color
    TF_ATTR_TARGET_CONVERTED                // attribute info
};

//+---------------------------------------------------------------------------
//
// ctor
//
//----------------------------------------------------------------------------

CDisplayAttributeInfo::CDisplayAttributeInfo()
{
    DllAddRef();

    _pguid = nullptr;
    _pDisplayAttribute = nullptr;
    _pValueName = nullptr;

    _refCount = 1;
}

//+---------------------------------------------------------------------------
//
// dtor
//
//----------------------------------------------------------------------------

CDisplayAttributeInfo::~CDisplayAttributeInfo()
{
    DllRelease();
}

//+---------------------------------------------------------------------------
//
// QueryInterface
//
//----------------------------------------------------------------------------

STDAPI CDisplayAttributeInfo::QueryInterface(REFIID riid, _Outptr_ void **ppvObj)
{
    if (ppvObj == nullptr)
        return E_INVALIDARG;

    *ppvObj = nullptr;

    if (IsEqualIID(riid, IID_IUnknown) ||
        IsEqualIID(riid, IID_ITfDisplayAttributeInfo))
    {
        *ppvObj = (ITfDisplayAttributeInfo *)this;
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

ULONG CDisplayAttributeInfo::AddRef(void)
{
    return ++_refCount;
}

//+---------------------------------------------------------------------------
//
// Release
//
//----------------------------------------------------------------------------

ULONG CDisplayAttributeInfo::Release(void)
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
// ITfDisplayAttributeInfo::GetGUID
//
//----------------------------------------------------------------------------

STDAPI CDisplayAttributeInfo::GetGUID(_Out_ GUID *pguid)
{
    if (pguid == nullptr)
        return E_INVALIDARG;

    if (_pguid == nullptr)
        return E_FAIL;

    *pguid = *_pguid;

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfDisplayAttributeInfo::GetDescription
//
//----------------------------------------------------------------------------

STDAPI CDisplayAttributeInfo::GetDescription(_Out_ BSTR *pbstrDesc)
{
    BSTR tempDesc;

    if (pbstrDesc == nullptr)
    {
        return E_INVALIDARG;
    }

    *pbstrDesc = nullptr;

    if ((tempDesc = SysAllocString(_pDescription)) == nullptr)
    {
        return E_OUTOFMEMORY;
    }

    *pbstrDesc = tempDesc;

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfDisplayAttributeInfo::GetAttributeInfo
//
//----------------------------------------------------------------------------

STDAPI CDisplayAttributeInfo::GetAttributeInfo(_Out_ TF_DISPLAYATTRIBUTE *ptfDisplayAttr)
{
    if (ptfDisplayAttr == nullptr)
    {
        return E_INVALIDARG;
    }

    // return the default display attribute.
    *ptfDisplayAttr = *_pDisplayAttribute;

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfDisplayAttributeInfo::SetAttributeInfo
//
//----------------------------------------------------------------------------

STDAPI CDisplayAttributeInfo::SetAttributeInfo(_In_ const TF_DISPLAYATTRIBUTE *ptfDisplayAttr)
{
    ptfDisplayAttr;

    return E_NOTIMPL;
}

//+---------------------------------------------------------------------------
//
// ITfDisplayAttributeInfo::Reset
//
//----------------------------------------------------------------------------

STDAPI CDisplayAttributeInfo::Reset()
{
    return SetAttributeInfo(_pDisplayAttribute);
}
