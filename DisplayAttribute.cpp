//
//
// Derived from Microsoft Sample IME by Jeremy '13,7,17
//
//
//#define DEBUG_PRINT

#include "Private.h"
#include "globals.h"
#include "DIME.h"

//+---------------------------------------------------------------------------
//
// _ClearCompositionDisplayAttributes
//
//----------------------------------------------------------------------------

void CDIME::_ClearCompositionDisplayAttributes(TfEditCookie ec, _In_ ITfContext *pContext)
{
    ITfRange* pRangeComposition = nullptr;
    ITfProperty* pDisplayAttributeProperty = nullptr;

    // get the compositon range.
    if (_pComposition == nullptr || FAILED(_pComposition->GetRange(&pRangeComposition)))
    {
        return;
    }

    // get our the display attribute property
    if (pContext && SUCCEEDED(pContext->GetProperty(GUID_PROP_ATTRIBUTE, &pDisplayAttributeProperty)) && pDisplayAttributeProperty)
    {
        // clear the value over the range
        pDisplayAttributeProperty->Clear(ec, pRangeComposition);

        pDisplayAttributeProperty->Release();
    }

	if(pRangeComposition)
		pRangeComposition->Release();
}

//+---------------------------------------------------------------------------
//
// _SetCompositionDisplayAttributes
//
//----------------------------------------------------------------------------

BOOL CDIME::_SetCompositionDisplayAttributes(TfEditCookie ec, _In_ ITfContext *pContext, TfGuidAtom gaDisplayAttribute)
{
	debugPrint(L"CDIME::_SetCompositionDisplayAttributes()");
    ITfRange* pRangeComposition = nullptr;
    ITfProperty* pDisplayAttributeProperty = nullptr;
    HRESULT hr = S_OK;

    // we need a range and the context it lives in
    if(_pComposition) 
		hr = _pComposition->GetRange(&pRangeComposition);
    if (FAILED(hr) || pRangeComposition==nullptr )
    {
		if (FAILED(hr))
			debugPrint(L"CDIME::_SetCompositionDisplayAttributes()  _pComposition->GetRange() failed hr = %x", hr);
		else
			debugPrint(L"CDIME::_SetCompositionDisplayAttributes()  null pRangeComposition");

        return FALSE;
    }

    hr = E_FAIL;

    // get our the display attribute property
    if (pContext && SUCCEEDED(pContext->GetProperty(GUID_PROP_ATTRIBUTE, &pDisplayAttributeProperty)) && pDisplayAttributeProperty)
    {
		debugPrint(L"CDIME::_SetCompositionDisplayAttributes()  SUCCEEDED(pContext->GetProperty()");
        VARIANT var;
        // set the value over the range
        // the application will use this guid atom to lookup the acutal rendering information
        var.vt = VT_I4; // we're going to set a TfGuidAtom
        var.lVal = gaDisplayAttribute; 

        hr = pDisplayAttributeProperty->SetValue(ec, pRangeComposition, &var);
		if (FAILED(hr))
			debugPrint(L"CDIME::_SetCompositionDisplayAttributes()  pDisplayAttributeProperty->SetValue() failed hr = %x", hr);

        pDisplayAttributeProperty->Release();
    }

	if(pRangeComposition)
		pRangeComposition->Release();
    return (hr == S_OK);
}

//+---------------------------------------------------------------------------
//
// _InitDisplayAttributeGuidAtom
//
// Because it's expensive to map our display attribute GUID to a TSF
// TfGuidAtom, we do it once when Activate is called.
//----------------------------------------------------------------------------

BOOL CDIME::_InitDisplayAttributeGuidAtom()
{
    ITfCategoryMgr* pCategoryMgr = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr, (void**)&pCategoryMgr);

    if (FAILED(hr) || pCategoryMgr == nullptr)
    {
        return FALSE;
    }

    // register the display attribute for input text.
    hr = pCategoryMgr->RegisterGUID(Global::DIMEGuidDisplayAttributeInput, &_gaDisplayAttributeInput);
	if (FAILED(hr))
    {
        goto Exit;
    }
    // register the display attribute for the converted text.
    hr = pCategoryMgr->RegisterGUID(Global::DIMEGuidDisplayAttributeConverted, &_gaDisplayAttributeConverted);
	if (FAILED(hr))
    {
        goto Exit;
    }

Exit:
	if(pCategoryMgr)
		pCategoryMgr->Release();

    return (hr == S_OK);
}
