//
//
// Derived from Microsoft Sample IME by Jeremy '13,7,17
//
//


#include "Private.h"
#include "globals.h"
#include "TSFTTS.h"

//+---------------------------------------------------------------------------
//
// _ClearCompositionDisplayAttributes
//
//----------------------------------------------------------------------------

void CTSFTTS::_ClearCompositionDisplayAttributes(TfEditCookie ec, _In_ ITfContext *pContext)
{
    ITfRange* pRangeComposition = nullptr;
    ITfProperty* pDisplayAttributeProperty = nullptr;

    // get the compositon range.
    if (_pComposition == nullptr || FAILED(_pComposition->GetRange(&pRangeComposition)))
    {
        return;
    }

    // get our the display attribute property
    if (SUCCEEDED(pContext->GetProperty(GUID_PROP_ATTRIBUTE, &pDisplayAttributeProperty)) && pDisplayAttributeProperty)
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

BOOL CTSFTTS::_SetCompositionDisplayAttributes(TfEditCookie ec, _In_ ITfContext *pContext, TfGuidAtom gaDisplayAttribute)
{
    ITfRange* pRangeComposition = nullptr;
    ITfProperty* pDisplayAttributeProperty = nullptr;
    HRESULT hr = S_OK;

    // we need a range and the context it lives in
    if(_pComposition) 
		hr = _pComposition->GetRange(&pRangeComposition);
    if (FAILED(hr) || pRangeComposition==nullptr )
    {
        return FALSE;
    }

    hr = E_FAIL;

    // get our the display attribute property
    if (SUCCEEDED(pContext->GetProperty(GUID_PROP_ATTRIBUTE, &pDisplayAttributeProperty)) && pDisplayAttributeProperty)
    {
        VARIANT var;
        // set the value over the range
        // the application will use this guid atom to lookup the acutal rendering information
        var.vt = VT_I4; // we're going to set a TfGuidAtom
        var.lVal = gaDisplayAttribute; 

        hr = pDisplayAttributeProperty->SetValue(ec, pRangeComposition, &var);

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

BOOL CTSFTTS::_InitDisplayAttributeGuidAtom()
{
    ITfCategoryMgr* pCategoryMgr = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr, (void**)&pCategoryMgr);

    if (FAILED(hr) || pCategoryMgr == nullptr)
    {
        return FALSE;
    }

    // register the display attribute for input text.
    hr = pCategoryMgr->RegisterGUID(Global::TSFTTSGuidDisplayAttributeInput, &_gaDisplayAttributeInput);
	if (FAILED(hr))
    {
        goto Exit;
    }
    // register the display attribute for the converted text.
    hr = pCategoryMgr->RegisterGUID(Global::TSFTTSGuidDisplayAttributeConverted, &_gaDisplayAttributeConverted);
	if (FAILED(hr))
    {
        goto Exit;
    }

Exit:
	if(pCategoryMgr)
		pCategoryMgr->Release();

    return (hr == S_OK);
}
