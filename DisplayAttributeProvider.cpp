//
//
// Derived from Microsoft Sample IME by Jeremy '13,7,17
//
//


#include "Private.h"
#include "globals.h"
#include "TSFTTS.h"
#include "DisplayAttributeInfo.h"
#include "EnumDisplayAttributeInfo.h"

//+---------------------------------------------------------------------------
//
// ITfDisplayAttributeProvider::EnumDisplayAttributeInfo
//
//----------------------------------------------------------------------------

STDAPI CTSFTTS::EnumDisplayAttributeInfo(__RPC__deref_out_opt IEnumTfDisplayAttributeInfo **ppEnum)
{
    CEnumDisplayAttributeInfo* pAttributeEnum = nullptr;

    if (ppEnum == nullptr)
    {
        return E_INVALIDARG;
    }

    *ppEnum = nullptr;

    pAttributeEnum = new (std::nothrow) CEnumDisplayAttributeInfo();
    if (pAttributeEnum == nullptr)
    {
        return E_OUTOFMEMORY;
    }

    *ppEnum = pAttributeEnum;

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfDisplayAttributeProvider::GetDisplayAttributeInfo
//
//----------------------------------------------------------------------------

STDAPI CTSFTTS::GetDisplayAttributeInfo(__RPC__in REFGUID guidInfo, __RPC__deref_out_opt ITfDisplayAttributeInfo **ppInfo)
{
    if (ppInfo == nullptr)
    {
        return E_INVALIDARG;
    }

    *ppInfo = nullptr;

    // Which display attribute GUID?
    if (IsEqualGUID(guidInfo, Global::TSFTTSGuidDisplayAttributeInput))
    {
        *ppInfo = new (std::nothrow) CDisplayAttributeInfoInput();
        if ((*ppInfo) == nullptr)
        {
            return E_OUTOFMEMORY;
        }
    }
    else if (IsEqualGUID(guidInfo, Global::TSFTTSGuidDisplayAttributeConverted))
    {
        *ppInfo = new (std::nothrow) CDisplayAttributeInfoConverted();
        if ((*ppInfo) == nullptr)
        {
            return E_OUTOFMEMORY;
        }
    }
    else
    {
        return E_INVALIDARG;
    }


    return S_OK;
}
