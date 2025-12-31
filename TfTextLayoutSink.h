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

class CDIME;

class CTfTextLayoutSink : public ITfTextLayoutSink
{
public:
    CTfTextLayoutSink(_In_ CDIME *pTextService);
    virtual ~CTfTextLayoutSink();

    // IUnknown methods
    STDMETHODIMP QueryInterface(REFIID riid, _Outptr_ void **ppvObj);
    STDMETHODIMP_(ULONG) AddRef(void);
    STDMETHODIMP_(ULONG) Release(void);

    // ITfTextLayoutSink
    STDMETHODIMP OnLayoutChange(_In_ ITfContext *pContext, TfLayoutCode lcode, _In_ ITfContextView *pContextView);

    HRESULT _StartLayout(_In_ ITfContext *pContextDocument, TfEditCookie ec, _In_ ITfRange *pRangeComposition);
    VOID _EndLayout();

    HRESULT _GetTextExt(_Inout_ RECT *lpRect);
    ITfContext* _GetContextDocument() { return _pContextDocument; };

	virtual VOID _LayoutChangeNotification(_In_ RECT *lpRect) = 0;
	
    virtual VOID _LayoutDestroyNotification() = 0;

private:
    HRESULT _AdviseTextLayoutSink();
    HRESULT _UnadviseTextLayoutSink();

private:
    ITfRange* _pRangeComposition;
    ITfContext* _pContextDocument;
    TfEditCookie _tfEditCookie;
    CDIME* _pTextService;
    DWORD _dwCookieTextLayoutSink;
    LONG _refCount;
};
