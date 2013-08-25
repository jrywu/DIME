
#include "Globals.h"
#include "CompositionProcessorEngine.h"
#pragma once

#define REVERSE_CONV_RESULT_LENGTH 64
class CReverseConversionList;
class CCompositionProcessorEngine;
class CReverseConversion : public ITfReverseConversion
{
public:
	CReverseConversion(CCompositionProcessorEngine* pCompositionProcessorEngine, REFGUID guidLanguageProfile);
	~CReverseConversion();
	// IUnknown methods
	STDMETHODIMP QueryInterface(REFIID riid, _Outptr_ void **ppvObj);
	STDMETHODIMP_(ULONG) AddRef(void);
	STDMETHODIMP_(ULONG) Release(void);
	//ITfReverseConversion 
	STDMETHODIMP DoReverseConversion(_In_ LPCWSTR lpstrToConvert, _Out_ ITfReverseConversionList **ppList);
private:
	LONG _refCount;
	CReverseConversionList* _pReverseConversionList;
	CCompositionProcessorEngine* _pCompositionProcessorEngine;
	GUID _guidLanguageProfile;
	IME_MODE _imeMode;
};

class CReverseConversionList : public ITfReverseConversionList
{
public:
	CReverseConversionList(IME_MODE imeMode);
	~CReverseConversionList();
	// IUnknown methods
	STDMETHODIMP QueryInterface(REFIID riid, _Outptr_ void **ppvObj);
	STDMETHODIMP_(ULONG) AddRef(void);
	STDMETHODIMP_(ULONG) Release(void);
	
	//ITfReverseConversionList 
	STDMETHODIMP GetLength(__RPC__out UINT *puIndex);
	STDMETHODIMP GetString(UINT uIndex, __RPC__deref_out_opt BSTR *pbstr);
	void SetResultList(CTSFTTSArray<CCandidateListItem>* pCandidateList);
private:
	LONG _refCount;
	BOOL _resultFound;
	WCHAR* _resultString;
	IME_MODE _imeMode;
};