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

#include "Private.h"
#include "Globals.h"

static const WCHAR RegInfo_Prefix_CLSID[] = L"CLSID\\";
static const WCHAR RegInfo_Key_InProSvr32[] = L"InProcServer32";
static const WCHAR RegInfo_Key_ThreadModel[] = L"ThreadingModel";

static const GUID SupportCategories[] = {
    GUID_TFCAT_TIP_KEYBOARD,
    GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER,
    GUID_TFCAT_TIPCAP_UIELEMENTENABLED, 
    GUID_TFCAT_TIPCAP_SECUREMODE,
    GUID_TFCAT_TIPCAP_COMLESS,
    GUID_TFCAT_TIPCAP_INPUTMODECOMPARTMENT,
    GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT, 
    GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT,
};
//+---------------------------------------------------------------------------
//
//  RegisterProfiles
//
//----------------------------------------------------------------------------

BOOL RegisterProfiles()
{
    HRESULT hr = S_FALSE;

    ITfInputProcessorProfileMgr *pITfInputProcessorProfileMgr = nullptr;
    hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER,
        IID_ITfInputProcessorProfileMgr, (void**)&pITfInputProcessorProfileMgr);
    if (FAILED(hr))
    {
        return FALSE;
    }

    WCHAR achIconFile[MAX_PATH] = {'\0'};
    DWORD cchA = 0;
    cchA = GetModuleFileName(Global::dllInstanceHandle, achIconFile, MAX_PATH);
    cchA = cchA >= MAX_PATH ? (MAX_PATH - 1) : cchA;
    achIconFile[cchA] = '\0';

    size_t lenOfDesc = 0;
	WCHAR serviceDescripion[50]={'\0'};;


	//Dayi profile registration
	LoadString(Global::dllInstanceHandle, IDS_DAYI_DESCRIPTION, serviceDescripion, 50);
    hr = StringCchLength(serviceDescripion, STRSAFE_MAX_CCH, &lenOfDesc);
    if (hr != S_OK)
    {
        goto Exit;
    }
    hr = pITfInputProcessorProfileMgr->RegisterProfile(Global::DIMECLSID,
        TEXTSERVICE_LANGID,
        Global::DIMEDayiGuidProfile,
        serviceDescripion,
        static_cast<ULONG>(lenOfDesc),
        achIconFile,
        cchA,
        (UINT)TEXTSERVICE_DAYI_ICON_INDEX, NULL, 0, FALSE, 0);

    if (FAILED(hr))
    {
        goto Exit;
    }

	//Array profile registration
	*serviceDescripion=L'\0';
	LoadString(Global::dllInstanceHandle, IDS_ARRAY_DESCRIPTION, serviceDescripion, 50);
    hr = StringCchLength(serviceDescripion, STRSAFE_MAX_CCH, &lenOfDesc);
    if (hr != S_OK)
    {
        goto Exit;
    }
	hr = pITfInputProcessorProfileMgr->RegisterProfile(Global::DIMECLSID,
        TEXTSERVICE_LANGID,
		Global::DIMEArrayGuidProfile,
        serviceDescripion,
        static_cast<ULONG>(lenOfDesc),
        achIconFile,
        cchA,
        (UINT)TEXTSERVICE_ARRAY_ICON_INDEX, NULL, 0, FALSE, 0);

    if (FAILED(hr))
    {
        goto Exit;
    }

	//Phonetic profile registration
	*serviceDescripion=L'\0';
	LoadString(Global::dllInstanceHandle, IDS_PHONETIC_DESCRIPTION, serviceDescripion, 50);
    hr = StringCchLength(serviceDescripion, STRSAFE_MAX_CCH, &lenOfDesc);
    if (hr != S_OK)
    {
        goto Exit;
    }
	hr = pITfInputProcessorProfileMgr->RegisterProfile(Global::DIMECLSID,
        TEXTSERVICE_LANGID,
		Global::DIMEPhoneticGuidProfile,
        serviceDescripion,
        static_cast<ULONG>(lenOfDesc),
        achIconFile,
        cchA,
		(UINT)TEXTSERVICE_PHONETIC_ICON_INDEX, NULL, 0, FALSE, 0);

    if (FAILED(hr))
    {
        goto Exit;
    }
	//Generic profile registration
	*serviceDescripion=L'\0';
	LoadString(Global::dllInstanceHandle, IDS_GENERIC_DESCRIPTION, serviceDescripion, 50);
    hr = StringCchLength(serviceDescripion, STRSAFE_MAX_CCH, &lenOfDesc);
    if (hr != S_OK)
    {
        goto Exit;
    }
	hr = pITfInputProcessorProfileMgr->RegisterProfile(Global::DIMECLSID,
        TEXTSERVICE_LANGID,
		Global::DIMEGenericGuidProfile,
        serviceDescripion,
        static_cast<ULONG>(lenOfDesc),
        achIconFile,
        cchA,
		(UINT)TEXTSERVICE_GENERIC_ICON_INDEX, NULL, 0, FALSE, 0);

    if (FAILED(hr))
    {
        goto Exit;
    }

Exit:
    if (pITfInputProcessorProfileMgr)
    {
        pITfInputProcessorProfileMgr->Release();
    }

    return (hr == S_OK);
}

//+---------------------------------------------------------------------------
//
//  UnregisterProfiles
//
//----------------------------------------------------------------------------

void UnregisterProfiles()
{
    HRESULT hr = S_OK;

    ITfInputProcessorProfileMgr *pITfInputProcessorProfileMgr = nullptr;
    hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER,
        IID_ITfInputProcessorProfileMgr, (void**)&pITfInputProcessorProfileMgr);
    if (FAILED(hr))
    {
        goto Exit;
    }
#ifndef NO_DAYI
	//Dayi profile 
    hr = pITfInputProcessorProfileMgr->UnregisterProfile(Global::DIMECLSID, TEXTSERVICE_LANGID, Global::DIMEDayiGuidProfile, 0);
    if (FAILED(hr))
    {
        goto Exit;
    }
#endif
	//Array profile 
    hr = pITfInputProcessorProfileMgr->UnregisterProfile(Global::DIMECLSID, TEXTSERVICE_LANGID, Global::DIMEArrayGuidProfile, 0);
    if (FAILED(hr))
    {
        goto Exit;
    }
	//Phonetic profile 
	hr = pITfInputProcessorProfileMgr->UnregisterProfile(Global::DIMECLSID, TEXTSERVICE_LANGID, Global::DIMEPhoneticGuidProfile, 0);
    if (FAILED(hr))
    {
        goto Exit;
    }
	//Generic profile 
	hr = pITfInputProcessorProfileMgr->UnregisterProfile(Global::DIMECLSID, TEXTSERVICE_LANGID, Global::DIMEGenericGuidProfile, 0);
    if (FAILED(hr))
    {
        goto Exit;
    }
Exit:
    if (pITfInputProcessorProfileMgr)
    {
        pITfInputProcessorProfileMgr->Release();
    }

    return;
}

//+---------------------------------------------------------------------------
//
//  RegisterCategories
//
//----------------------------------------------------------------------------

BOOL RegisterCategories()
{
    ITfCategoryMgr* pCategoryMgr = nullptr;
    HRESULT hr = S_OK;

    hr = CoCreateInstance(CLSID_TF_CategoryMgr, NULL, CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr, (void**)&pCategoryMgr);
    if (FAILED(hr))
    {
        return FALSE;
    }

    for each(GUID guid in SupportCategories)
    {
        hr = pCategoryMgr->RegisterCategory(Global::DIMECLSID, guid, Global::DIMECLSID);
    }


    pCategoryMgr->Release();

    return (hr == S_OK);
}

//+---------------------------------------------------------------------------
//
//  UnregisterCategories
//
//----------------------------------------------------------------------------

void UnregisterCategories()
{
    ITfCategoryMgr* pCategoryMgr = S_OK;
    HRESULT hr = S_OK;

    hr = CoCreateInstance(CLSID_TF_CategoryMgr, NULL, CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr, (void**)&pCategoryMgr);
    if (FAILED(hr))
    {
        return;
    }

    for each(GUID guid in SupportCategories)
    {
        pCategoryMgr->UnregisterCategory(Global::DIMECLSID, guid, Global::DIMECLSID);
    }

  
    pCategoryMgr->Release();

    return;
}

//+---------------------------------------------------------------------------
//
// RecurseDeleteKey
//
// RecurseDeleteKey is necessary because on NT RegDeleteKey doesn't work if the
// specified key has subkeys
//----------------------------------------------------------------------------

LONG RecurseDeleteKey(_In_ HKEY hParentKey, _In_ LPCTSTR lpszKey)
{
    HKEY regKeyHandle = nullptr;
    LONG res = 0;
    FILETIME time;
    WCHAR stringBuffer[256] = {'\0'};
    DWORD size = ARRAYSIZE(stringBuffer);

    if (RegOpenKey(hParentKey, lpszKey, &regKeyHandle) != ERROR_SUCCESS)
    {
        return ERROR_SUCCESS;
    }

    res = ERROR_SUCCESS;
    while (RegEnumKeyEx(regKeyHandle, 0, stringBuffer, &size, NULL, NULL, NULL, &time) == ERROR_SUCCESS)
    {
        stringBuffer[ARRAYSIZE(stringBuffer)-1] = '\0';
        res = RecurseDeleteKey(regKeyHandle, stringBuffer);
        if (res != ERROR_SUCCESS)
        {
            break;
        }
        size = ARRAYSIZE(stringBuffer);
    }
    RegCloseKey(regKeyHandle);

    return res == ERROR_SUCCESS ? RegDeleteKey(hParentKey, lpszKey) : res;
}

//+---------------------------------------------------------------------------
//
//  RegisterServer
//
//----------------------------------------------------------------------------

BOOL RegisterServer()
{
    DWORD copiedStringLen = 0;
    HKEY regKeyHandle = nullptr;
    HKEY regSubkeyHandle = nullptr;
    BOOL ret = FALSE;
    WCHAR achIMEKey[ARRAYSIZE(RegInfo_Prefix_CLSID) + CLSID_STRLEN] = {'\0'};
    WCHAR achFileName[MAX_PATH] = {'\0'};

    if (!CLSIDToString(Global::DIMECLSID, achIMEKey + ARRAYSIZE(RegInfo_Prefix_CLSID) - 1))
    {
        return FALSE;
    }

    memcpy(achIMEKey, RegInfo_Prefix_CLSID, sizeof(RegInfo_Prefix_CLSID) - sizeof(WCHAR));
	WCHAR serviceDescripion[50]={'\0'};
	LoadString(Global::dllInstanceHandle, IDIS_DIME, serviceDescripion, 50);

    if (RegCreateKeyEx(HKEY_CLASSES_ROOT, achIMEKey, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &regKeyHandle, &copiedStringLen) == ERROR_SUCCESS)
    {
        if (RegSetValueEx(regKeyHandle, NULL, 0, REG_SZ, (const BYTE *)serviceDescripion, (_countof(serviceDescripion))*sizeof(WCHAR)) != ERROR_SUCCESS)
        {
            goto Exit;
        }

        if (RegCreateKeyEx(regKeyHandle, RegInfo_Key_InProSvr32, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &regSubkeyHandle, &copiedStringLen) == ERROR_SUCCESS)
        {
            copiedStringLen = GetModuleFileNameW(Global::dllInstanceHandle, achFileName, ARRAYSIZE(achFileName));
            copiedStringLen = (copiedStringLen >= (MAX_PATH - 1)) ? MAX_PATH : (++copiedStringLen);
            if (RegSetValueEx(regSubkeyHandle, NULL, 0, REG_SZ, (const BYTE *)achFileName, (copiedStringLen)*sizeof(WCHAR)) != ERROR_SUCCESS)
            {
                goto Exit;
            }
            if (RegSetValueEx(regSubkeyHandle, RegInfo_Key_ThreadModel, 0, REG_SZ, (const BYTE *)TEXTSERVICE_MODEL, (_countof(TEXTSERVICE_MODEL)) * sizeof(WCHAR)) != ERROR_SUCCESS)
            {
                goto Exit;
            }

            ret = TRUE;
        }
    }
	
Exit:
    if (regSubkeyHandle)
    {
        RegCloseKey(regSubkeyHandle);
        regSubkeyHandle = nullptr;
    }
    if (regKeyHandle)
    {
        RegCloseKey(regKeyHandle);
        regKeyHandle = nullptr;
    }

    return ret;
}

//+---------------------------------------------------------------------------
//
//  UnregisterServer
//
//----------------------------------------------------------------------------

void UnregisterServer()
{
    WCHAR achIMEKey[ARRAYSIZE(RegInfo_Prefix_CLSID) + CLSID_STRLEN] = {'\0'};
	//Dayi
    if (!CLSIDToString(Global::DIMECLSID, achIMEKey + ARRAYSIZE(RegInfo_Prefix_CLSID) - 1))
    {
        return;
    }

    memcpy(achIMEKey, RegInfo_Prefix_CLSID, sizeof(RegInfo_Prefix_CLSID) - sizeof(WCHAR));

    RecurseDeleteKey(HKEY_CLASSES_ROOT, achIMEKey);

}
