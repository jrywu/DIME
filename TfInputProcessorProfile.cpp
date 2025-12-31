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

HRESULT CTfInputProcessorProfile::GetReverseConversionProviders(LANGID langid, CDIMEArray <LanguageProfileInfo> *langProfileInfoList)
{
	debugPrint(L"CTfInputProcessorProfile::GetReverseConversionProviders() langid = %d " , langid);
	HRESULT hr;
	IEnumTfLanguageProfiles* pEnumProf = 0;
	if(_pInputProcessorProfile == nullptr) return E_OUTOFMEMORY;
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
					hr = pITfReverseConversionMgr->GetReverseConversion(langid, langProfile.guidProfile, NULL, &pITfReverseConversion);

					if(SUCCEEDED(hr))
					{
						LanguageProfileInfo *pLangProfileInfo = nullptr;
						PWCH pwchDescription;
						if(langProfileInfoList)
							pLangProfileInfo = langProfileInfoList->Append();;
						pwchDescription = new (std::nothrow) WCHAR[wcslen(bstrDescription)+1];
						if (pwchDescription == nullptr) return S_FALSE;
						StringCchCopy(pwchDescription, wcslen(bstrDescription)+1, bstrDescription);
						if(pLangProfileInfo)
						{
							pLangProfileInfo->clsid = langProfile.clsid;
							pLangProfileInfo->guidProfile = langProfile.guidProfile;
							pLangProfileInfo->description = pwchDescription;
						}

						debugPrint(L"%s supports reverse conversion ", bstrDescription);
						if(pITfReverseConversion)
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
