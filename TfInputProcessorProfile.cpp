//
//
// Derived from Microsoft Sample IME by Jeremy '13,7,17
//
//
//#define DEBUG_PRINT

#include "Private.h"
#include "Globals.h"
#include "TfInputProcessorProfile.h"

CTfInputProcessorProfile::CTfInputProcessorProfile()
{
	debugPrint(L"CTfInputProcessorProfile::CTfInputProcessorProfile()");
    _pInputProcessorProfile = nullptr;
}

CTfInputProcessorProfile::~CTfInputProcessorProfile()
{
	debugPrint(L"CTfInputProcessorProfile::~CTfInputProcessorProfile()");
    if (_pInputProcessorProfile) {
        _pInputProcessorProfile->Release();
        _pInputProcessorProfile = nullptr;
    }
}

HRESULT CTfInputProcessorProfile::CreateInstance()
{
	debugPrint(L"CTfInputProcessorProfile::CreateInstance()");
    HRESULT	hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
        IID_ITfInputProcessorProfiles, (void**)&_pInputProcessorProfile);

    return hr;
}

HRESULT CTfInputProcessorProfile::GetCurrentLanguage(_Out_ LANGID *plangid)
{
	debugPrint(L"CTfInputProcessorProfile::GetCurrentLanguage()");
    if (_pInputProcessorProfile)
    {
        return _pInputProcessorProfile->GetCurrentLanguage(plangid);
    }
    else
    {
        return E_FAIL;
    }
}

HRESULT CTfInputProcessorProfile::GetDefaultLanguageProfile(LANGID langid, REFGUID catid, _Out_ CLSID *pclsid, _Out_ GUID *pguidProfile)
{
	debugPrint(L"CTfInputProcessorProfile::GetDefaultLanguageProfile()");
    if (_pInputProcessorProfile)
    {
        return _pInputProcessorProfile->GetDefaultLanguageProfile(langid, catid, pclsid, pguidProfile);
    }
    else
    {
        return E_FAIL;
    }
}

HRESULT CTfInputProcessorProfile::GetReverseConversionProviders(LANGID langid, CTSFTTSArray <LanguageProfileInfo> *langProfileInfoList)
{
	debugPrint(L"CTfInputProcessorProfile::GetReverseConversionProviders() langid = %d " , langid);
	HRESULT hr;
	IEnumTfLanguageProfiles* pEnumProf = 0;

	hr = _pInputProcessorProfile->EnumLanguageProfiles(langid, &pEnumProf);
	if (SUCCEEDED(hr) && pEnumProf)
	{

		TF_LANGUAGEPROFILE langProfile;
		ULONG fetch = 0;
		while (S_OK == pEnumProf->Next(1, &langProfile, &fetch))
		{
			BSTR bstrDescription;
			BOOL active = false;

			hr = _pInputProcessorProfile->GetLanguageProfileDescription(langProfile.clsid, langid, langProfile.guidProfile, &bstrDescription);
			hr = _pInputProcessorProfile->IsEnabledLanguageProfile(langProfile.clsid, langid, langProfile.guidProfile, &active);
			if (SUCCEEDED(hr))
			{
				debugPrint(L"CTfInputProcessorProfile::GetReverseConversionProviders() Got IM with descrition = %s and enabled = %d", bstrDescription, active);
				ITfReverseConversionMgr * pITfReverseConversionMgr;
				if(SUCCEEDED(CoCreateInstance(langProfile.clsid, nullptr, CLSCTX_INPROC_SERVER, IID_ITfReverseConversionMgr, (void**)&pITfReverseConversionMgr)))
				{
					ITfReverseConversion* pITfReverseConversion;
					HRESULT hr;
					hr = pITfReverseConversionMgr->GetReverseConversion(langid, langProfile.guidProfile, NULL, &pITfReverseConversion);
					if(SUCCEEDED(hr))
					{
						LanguageProfileInfo *pLangProfileInfo = nullptr;
						PWCH pwchDescription;
						pLangProfileInfo = langProfileInfoList->Append();;
						pwchDescription = new (std::nothrow) WCHAR[wcslen(bstrDescription)+1];
						*pwchDescription = L'\0';
						StringCchCopy(pwchDescription, wcslen(bstrDescription)+1, bstrDescription);
						pLangProfileInfo->clsid = langProfile.clsid;
						pLangProfileInfo->guidProfile = langProfile.guidProfile;
						pLangProfileInfo->description = pwchDescription;
			
						debugPrint(L"%s supports reverse conversion ", bstrDescription);
						pITfReverseConversion->Release();
					}	
					pITfReverseConversionMgr->Release();
				}
				
								
			}

			SysFreeString(bstrDescription);

		}

	}
	return S_OK;
}
