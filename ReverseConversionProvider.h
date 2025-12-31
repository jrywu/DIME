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
#include "Globals.h"
#include "CompositionProcessorEngine.h"
#pragma once

#define REVERSE_CONV_RESULT_LENGTH 64
class CReverseConversionList;
class CCompositionProcessorEngine;
class CReverseConversion : public ITfReverseConversion
{
public:
	CReverseConversion(CCompositionProcessorEngine* pCompositionProcessorEngine, IME_MODE imeMode);
	~CReverseConversion();
	// IUnknown methods
	STDMETHODIMP QueryInterface(REFIID riid, _Outptr_ void **ppvObj);
	STDMETHODIMP_(ULONG) AddRef(void);
	STDMETHODIMP_(ULONG) Release(void);
	//ITfReverseConversion 
	STDMETHODIMP DoReverseConversion(_In_ LPCWSTR lpstrToConvert, _Inout_opt_ ITfReverseConversionList **ppList);
private:
	LONG _refCount;
	CReverseConversionList* _pReverseConversionList;
	CCompositionProcessorEngine* _pCompositionProcessorEngine;
	IME_MODE _imeMode;
	_T_RadicalMap* _pRadicalMap;
};

class CReverseConversionList : public ITfReverseConversionList
{
public:
	CReverseConversionList(IME_MODE imeMode, _T_RadicalMap* pRadicalMap);
	~CReverseConversionList();
	// IUnknown methods
	STDMETHODIMP QueryInterface(REFIID riid, _Outptr_ void **ppvObj);
	STDMETHODIMP_(ULONG) AddRef(void);
	STDMETHODIMP_(ULONG) Release(void);
	
	//ITfReverseConversionList 
	STDMETHODIMP GetLength(__RPC__out UINT *puIndex);
	STDMETHODIMP GetString(UINT uIndex, _Inout_ BSTR *pbstr);
	void SetResultList(CDIMEArray<CCandidateListItem>* pCandidateList);
private:
	LONG _refCount;
	BOOL _resultFound;
	WCHAR* _resultString;
	IME_MODE _imeMode;
	_T_RadicalMap* _pRadicalMap;
};