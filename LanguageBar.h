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
#ifndef LANGUAGEBAR_H
#define LANGUAGEBAR_H

#pragma once

class CCompartment;
class CCompartmentEventSink;

class CLangBarItemButton : public ITfLangBarItemButton,
    public ITfSource
{
public:
    CLangBarItemButton(REFGUID guidLangBar, LPCWSTR description, LPCWSTR tooltip, DWORD onIconIndex, DWORD offIconIndex, BOOL isSecureMode);
    ~CLangBarItemButton();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, _Outptr_ void **ppvObj);
    STDMETHODIMP_(ULONG) AddRef(void);
    STDMETHODIMP_(ULONG) Release(void);

    // ITfLangBarItem
    STDMETHODIMP GetInfo(_Out_ TF_LANGBARITEMINFO *pInfo);
    STDMETHODIMP GetStatus(_Out_ DWORD *pdwStatus);
    STDMETHODIMP Show(BOOL fShow);
    STDMETHODIMP GetTooltipString(_Out_ BSTR *pbstrToolTip);

    // ITfLangBarItemButton
    STDMETHODIMP OnClick(TfLBIClick click, POINT pt, _In_ const RECT *prcArea);
    STDMETHODIMP InitMenu(_In_ ITfMenu *pMenu);
    STDMETHODIMP OnMenuSelect(UINT wID);
    STDMETHODIMP GetIcon(_Out_ HICON *phIcon);
    STDMETHODIMP GetText(_Out_ BSTR *pbstrText);

    // ITfSource
    STDMETHODIMP AdviseSink(__RPC__in REFIID riid, __RPC__in_opt IUnknown *punk, __RPC__out DWORD *pdwCookie);
    STDMETHODIMP UnadviseSink(DWORD dwCookie);

    // Add/Remove languagebar item
    HRESULT _AddItem(_In_ ITfThreadMgr *pThreadMgr);
    HRESULT _RemoveItem(_In_ ITfThreadMgr *pThreadMgr);

    // Register compartment for button On/Off switch
    BOOL _RegisterCompartment(_In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId, REFGUID guidCompartment);
    BOOL _UnregisterCompartment(_In_ ITfThreadMgr *pThreadMgr);

    void CleanUp();

    void SetStatus(DWORD dwStatus, BOOL fSet);

private:
    ITfLangBarItemSink* _pLangBarItemSink;

    TF_LANGBARITEMINFO _tfLangBarItemInfo;
    LPCWSTR _pTooltipText;
    DWORD _onIconIndex;
    DWORD _offIconIndex;

    BOOL _isAddedToLanguageBar;
    BOOL _isSecureMode;
    DWORD _status;

    CCompartment* _pCompartment;
    CCompartmentEventSink* _pCompartmentEventSink;
    static HRESULT _CompartmentCallback(_In_ void *pv, REFGUID guidCompartment);

    // The cookie for the sink to CLangBarItemButton.
    static const DWORD _cookie = 0;

    LONG _refCount;
};
#endif