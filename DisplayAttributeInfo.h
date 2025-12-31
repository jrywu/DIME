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

//+---------------------------------------------------------------------------
//
// CDisplayAttributeInfo class
//
//----------------------------------------------------------------------------

class CDisplayAttributeInfo : public ITfDisplayAttributeInfo
{
public:
    CDisplayAttributeInfo();
    ~CDisplayAttributeInfo();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, _Outptr_ void **ppvObj);
    STDMETHODIMP_(ULONG) AddRef(void);
    STDMETHODIMP_(ULONG) Release(void);

    // ITfDisplayAttributeInfo
    STDMETHODIMP GetGUID(_Out_ GUID *pguid);
    STDMETHODIMP GetDescription(_Out_ BSTR *pbstrDesc);
    STDMETHODIMP GetAttributeInfo(_Out_ TF_DISPLAYATTRIBUTE *pTSFDisplayAttr);
    STDMETHODIMP SetAttributeInfo(_In_ const TF_DISPLAYATTRIBUTE *ptfDisplayAttr);
    STDMETHODIMP Reset();

protected:
    const GUID* _pguid;
    const TF_DISPLAYATTRIBUTE* _pDisplayAttribute;
    const WCHAR* _pDescription;
    const WCHAR* _pValueName;

private:
    LONG _refCount; // COM ref count
};

//+---------------------------------------------------------------------------
//
// CDisplayAttributeInfoInput class
//
//----------------------------------------------------------------------------

class CDisplayAttributeInfoInput : public CDisplayAttributeInfo
{
public:
    CDisplayAttributeInfoInput()
    {
        _pguid = &Global::DIMEGuidDisplayAttributeInput;
        _pDisplayAttribute = &_s_DisplayAttribute;
        _pDescription = _s_szDescription;
        _pValueName = _s_szValueName;
    }

    static const TF_DISPLAYATTRIBUTE _s_DisplayAttribute;
    static const WCHAR _s_szDescription[];
    static const WCHAR _s_szValueName[];
};

//+---------------------------------------------------------------------------
//
// CDisplayAttributeInfoConverted class
//
//----------------------------------------------------------------------------

class CDisplayAttributeInfoConverted : public CDisplayAttributeInfo
{
public:
    CDisplayAttributeInfoConverted()
    {
        _pguid = &Global::DIMEGuidDisplayAttributeConverted;
        _pDisplayAttribute = &_s_DisplayAttribute;
        _pDescription = _s_szDescription;
        _pValueName = _s_szValueName;
    }

    static const TF_DISPLAYATTRIBUTE _s_DisplayAttribute;
    static const WCHAR _s_szDescription[];
    static const WCHAR _s_szValueName[];
};
