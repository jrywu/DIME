//
//
// Derived from Microsoft Sample IME by Jeremy '13,7,17
//
//


#pragma once

class CTfInputProcessorProfile
{
public:
    CTfInputProcessorProfile();
    ~CTfInputProcessorProfile();

    HRESULT CreateInstance();
    HRESULT GetCurrentLanguage(_Out_ LANGID *plangid);
    HRESULT GetDefaultLanguageProfile(LANGID langid, REFGUID catid, _Out_ CLSID *pclsid, _Out_ GUID *pguidProfile);

private:
    ITfInputProcessorProfiles* _pInputProcessorProfile;
};
