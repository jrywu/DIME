// DIME Settings.cpp : Defines the entry point for the application.
//

#include "framework.h"
#include <CommCtrl.h>
#include <ntddkbd.h>
#include <appmodel.h>  // For GetCurrentPackageFullName
#include <shellapi.h>  // For ShellExecuteEx (elevation)
#include <ShlObj.h>    // For SHGetFolderPath
#include <msctf.h>     // For ITfInputProcessorProfileMgr, ITfCategoryMgr
#include "DIMESettings.h"
#include "..\Globals.h"
#include "..\Config.h"
#include "..\BuildInfo.h"
#include "..\TfInputProcessorProfile.h"

#pragma comment(lib, "ComCtl32.lib")  
constexpr wchar_t DIME_SETTINGS_INSTANCE_MUTEX_NAME[] = L"{B11F1FB2-3ECC-409E-A036-4162ADCEF1A3}";

// TSF registration constants (same as in Register.cpp)
#define TEXTSERVICE_LANGID    MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL)
// Icon indices are defined in Define.h as negative resource IDs:
// TEXTSERVICE_DAYI_ICON_INDEX = -IDI_DAYI = -11
// TEXTSERVICE_ARRAY_ICON_INDEX = -IDI_ARRAY = -12
// TEXTSERVICE_PHONETIC_ICON_INDEX = -IDI_PHONETIC = -13
// TEXTSERVICE_GENERIC_ICON_INDEX = -IDI_GENERIC = -14
// (Define.h is included via Globals.h)

// TSF Category GUIDs to register
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

// Global Variables:
HINSTANCE hInst;                                // current instance

// Forward declarations
static void GetPackageDllPath(LPCWSTR dllSubPath, LPWSTR outPath, size_t outPathSize);
static void GetDeployedDllPath(BOOL isX86, LPWSTR outPath, size_t outPathSize);
static BOOL DeployDllToAccessibleLocation(LPCWSTR sourcePath, LPCWSTR destPath);
static void RemoveDeployedDlls();
static BOOL RegisterCOMServerToRegistry(LPCWSTR dllPath, BOOL isWow64Dll);
static BOOL UnregisterCOMServerFromRegistry(BOOL isWow64Dll);
static BOOL RegisterTSFProfiles(LPCWSTR dllPath);
static BOOL RegisterTSFCategories();
static void UnregisterTSFProfiles();
static void UnregisterTSFCategories();
static void UnregisterIMEForMSIX();
static void RegisterIMEForMSIX(BOOL isElevated, BOOL testMode = FALSE);  // Note: TSF requires HKLM, per-user not possible

// Check if process is running with admin privileges
static BOOL IsRunningAsAdmin()
{
    BOOL isAdmin = FALSE;
    PSID adminGroup = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    
    if (AllocateAndInitializeSid(&ntAuthority, 2,
        SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
        0, 0, 0, 0, 0, 0, &adminGroup))
    {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin;
}

// Re-launch this process with elevation (UAC prompt)
static BOOL RelaunchAsAdmin(LPCWSTR args)
{
    WCHAR exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpVerb = L"runas";
    sei.lpFile = exePath;
    sei.lpParameters = args;
    sei.hwnd = NULL;
    sei.nShow = SW_NORMAL;
    
    return ShellExecuteExW(&sei);
}

// Pre-stage DLLs from WindowsApps to TEMP folder before elevation
// This is necessary because the elevated process loses package identity
// and cannot read from WindowsApps folder
// Returns TRUE if at least one DLL was staged
static BOOL PreStageDllsToTemp(LPWSTR tempX64Path, size_t tempX64Size, 
                                LPWSTR tempX86Path, size_t tempX86Size)
{
    // Get temp folder
    WCHAR tempDir[MAX_PATH];
    DWORD len = GetTempPathW(MAX_PATH, tempDir);
    if (len == 0 || len >= MAX_PATH)
    {
        return FALSE;
    }
    
    // Create DIME subfolder in temp
    WCHAR tempDimeDir[MAX_PATH];
    StringCchPrintfW(tempDimeDir, MAX_PATH, L"%sDIME_Stage", tempDir);
    CreateDirectoryW(tempDimeDir, NULL);
    
    WCHAR srcPath[MAX_PATH];
    BOOL anySuccess = FALSE;
    
    // Copy x64 DLL
    GetPackageDllPath(L"DIME\\DIME.dll", srcPath, MAX_PATH);
    if (GetFileAttributesW(srcPath) != INVALID_FILE_ATTRIBUTES)
    {
        StringCchPrintfW(tempX64Path, tempX64Size, L"%s\\DIME_x64.dll", tempDimeDir);
        if (CopyFileW(srcPath, tempX64Path, FALSE))
        {
            anySuccess = TRUE;
        }
        else
        {
            tempX64Path[0] = L'\0';
            debugPrint(L"PreStageDlls: Failed to copy x64 DLL, error=%d", GetLastError());
        }
    }
    else
    {
        tempX64Path[0] = L'\0';
    }
    
    // Copy x86 DLL
    GetPackageDllPath(L"DIME.x86\\DIME.dll", srcPath, MAX_PATH);
    if (GetFileAttributesW(srcPath) != INVALID_FILE_ATTRIBUTES)
    {
        StringCchPrintfW(tempX86Path, tempX86Size, L"%s\\DIME_x86.dll", tempDimeDir);
        if (CopyFileW(srcPath, tempX86Path, FALSE))
        {
            anySuccess = TRUE;
        }
        else
        {
            tempX86Path[0] = L'\0';
            debugPrint(L"PreStageDlls: Failed to copy x86 DLL, error=%d", GetLastError());
        }
    }
    else
    {
        tempX86Path[0] = L'\0';
    }
    
    return anySuccess;
}

// Clean up staged DLL files from temp
static void CleanupStagedDlls()
{
    WCHAR tempDir[MAX_PATH];
    DWORD len = GetTempPathW(MAX_PATH, tempDir);
    if (len == 0 || len >= MAX_PATH) return;
    
    WCHAR path[MAX_PATH];
    StringCchPrintfW(path, MAX_PATH, L"%sDIME_Stage\\DIME_x64.dll", tempDir);
    DeleteFileW(path);
    StringCchPrintfW(path, MAX_PATH, L"%sDIME_Stage\\DIME_x86.dll", tempDir);
    DeleteFileW(path);
    StringCchPrintfW(path, MAX_PATH, L"%sDIME_Stage", tempDir);
    RemoveDirectoryW(path);
}

// Check if DIMESettings.exe is running as MSIX package
// Unlike DIME.dll, we CAN use GetCurrentPackageFullName() here because
// DIMESettings.exe is the packaged app's entry point
static BOOL IsRunningAsMSIX()
{
    UINT32 length = 0;
    LONG result = GetCurrentPackageFullName(&length, NULL);
    return (result == ERROR_INSUFFICIENT_BUFFER);
}

// Check if IME is already registered (COM server exists in HKLM registry)
// Note: TSF requires HKLM registration - per-user (HKCU) is not supported
static BOOL IsIMERegistered()
{
    // DIME CLSID: {1DE68A87-FF3B-46A0-8F80-46730B2491B1}
    HKEY hKey = NULL;
    BOOL registered = FALSE;
    
    // Check HKLM (system-wide registration)
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, 
        L"SOFTWARE\\Classes\\CLSID\\{1DE68A87-FF3B-46A0-8F80-46730B2491B1}\\InprocServer32",
        0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        RegCloseKey(hKey);
        registered = TRUE;
    }
    
    return registered;
}


// Helper function to invoke regsvr32.exe for a specific DLL
// useWow64: TRUE = use 32-bit regsvr32 (for x86 DLLs), FALSE = use native 64-bit regsvr32 (for x64/ARM64 DLLs)
// Note: DIMESettings.exe is 32-bit, so we need special handling for 64-bit regsvr32
static BOOL RegisterDllWithRegsvr32(LPCWSTR dllPath, BOOL useWow64)
{
    WCHAR regsvr32Path[MAX_PATH];
    
    if (useWow64)
    {
        // 32-bit regsvr32 for x86 DLLs
        // From a 32-bit process, GetSystemDirectoryW returns SysWOW64 (32-bit folder)
        GetSystemDirectoryW(regsvr32Path, MAX_PATH);
        StringCchCatW(regsvr32Path, MAX_PATH, L"\\regsvr32.exe");
    }
    else
    {
        // 64-bit regsvr32 for x64/ARM64 DLLs
        // From a 32-bit process, we must use "Sysnative" to bypass WOW64 redirection
        // Sysnative is a virtual folder that maps to the real 64-bit System32
        StringCchCopyW(regsvr32Path, MAX_PATH, L"C:\\Windows\\Sysnative\\regsvr32.exe");
    }
    
    // Build command line: regsvr32 /s "path\to\dll"
    WCHAR cmdLine[MAX_PATH * 2];
    StringCchPrintfW(cmdLine, MAX_PATH * 2, L"/s \"%s\"", dllPath);
    
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"open";  // Already elevated
    sei.lpFile = regsvr32Path;
    sei.lpParameters = cmdLine;
    sei.nShow = SW_HIDE;
    
    if (ShellExecuteExW(&sei))
    {
        WaitForSingleObject(sei.hProcess, 10000);  // Wait up to 10 seconds
        DWORD exitCode = 0;
        GetExitCodeProcess(sei.hProcess, &exitCode);
        CloseHandle(sei.hProcess);
        return (exitCode == 0);
    }
    else
    {
        return FALSE;
    }
}

// Helper function to register COM server directly to registry (HKLM)
// This bypasses regsvr32 and writes the registry entries directly
// Works for both x64 and x86 DLLs from 32-bit DIMESettings.exe
// Note: TSF requires HKLM registration - per-user (HKCU) is not supported
static BOOL RegisterCOMServerToRegistry(LPCWSTR dllPath, BOOL isWow64Dll)
{
    // DIME CLSID: {1DE68A87-FF3B-46A0-8F80-46730B2491B1}
    LPCWSTR clsid = L"{1DE68A87-FF3B-46A0-8F80-46730B2491B1}";
    
    WCHAR keyPath[MAX_PATH];
    HKEY hKey = NULL;
    BOOL success = FALSE;
    
    // For x86 DLL on x64 system, we need to write to WOW6432Node
    // Use KEY_WOW64_32KEY or KEY_WOW64_64KEY to specify registry view
    REGSAM samDesired = KEY_WRITE;
    if (isWow64Dll)
    {
        samDesired |= KEY_WOW64_32KEY;  // Write to 32-bit registry view
    }
    else
    {
        samDesired |= KEY_WOW64_64KEY;  // Write to 64-bit registry view
    }
    
    // Create CLSID key: HKLM\SOFTWARE\Classes\CLSID\{guid}
    StringCchPrintfW(keyPath, MAX_PATH, L"Software\\Classes\\CLSID\\%s", clsid);
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, keyPath, 0, NULL, 0, samDesired, NULL, &hKey, NULL) == ERROR_SUCCESS)
    {
        RegSetValueExW(hKey, NULL, 0, REG_SZ, (BYTE*)L"DIME", (DWORD)(wcslen(L"DIME") + 1) * sizeof(WCHAR));
        RegCloseKey(hKey);
        
        // Create InprocServer32 key with DLL path
        StringCchPrintfW(keyPath, MAX_PATH, L"Software\\Classes\\CLSID\\%s\\InprocServer32", clsid);
        if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, keyPath, 0, NULL, 0, samDesired, NULL, &hKey, NULL) == ERROR_SUCCESS)
        {
            RegSetValueExW(hKey, NULL, 0, REG_SZ, (BYTE*)dllPath, (DWORD)(wcslen(dllPath) + 1) * sizeof(WCHAR));
            RegSetValueExW(hKey, L"ThreadingModel", 0, REG_SZ, (BYTE*)L"Apartment", (DWORD)(wcslen(L"Apartment") + 1) * sizeof(WCHAR));
            RegCloseKey(hKey);
            success = TRUE;
        }
    }
    
    return success;
}

// Helper function to unregister COM server from registry
// Called during MSIX uninstall to clean up
// Tries both HKLM and HKCU since we don't know which was used during registration
static BOOL UnregisterCOMServerFromRegistry(BOOL isWow64Dll)
{
    // DIME CLSID: {1DE68A87-FF3B-46A0-8F80-46730B2491B1}
    LPCWSTR clsid = L"{1DE68A87-FF3B-46A0-8F80-46730B2491B1}";
    
    WCHAR keyPath[MAX_PATH];
    BOOL success = FALSE;
    
    REGSAM samDesired = KEY_WRITE;
    if (isWow64Dll)
    {
        samDesired |= KEY_WOW64_32KEY;
    }
    else
    {
        samDesired |= KEY_WOW64_64KEY;
    }
    
    // Try to delete from HKLM first (all users)
    StringCchPrintfW(keyPath, MAX_PATH, L"Software\\Classes\\CLSID\\%s\\InprocServer32", clsid);
    RegDeleteKeyExW(HKEY_LOCAL_MACHINE, keyPath, samDesired, 0);
    StringCchPrintfW(keyPath, MAX_PATH, L"Software\\Classes\\CLSID\\%s", clsid);
    if (RegDeleteKeyExW(HKEY_LOCAL_MACHINE, keyPath, samDesired, 0) == ERROR_SUCCESS)
    {
        success = TRUE;
    }
    
    // Also try to delete from HKCU (current user)
    StringCchPrintfW(keyPath, MAX_PATH, L"Software\\Classes\\CLSID\\%s\\InprocServer32", clsid);
    RegDeleteKeyExW(HKEY_CURRENT_USER, keyPath, samDesired, 0);
    StringCchPrintfW(keyPath, MAX_PATH, L"Software\\Classes\\CLSID\\%s", clsid);
    if (RegDeleteKeyExW(HKEY_CURRENT_USER, keyPath, samDesired, 0) == ERROR_SUCCESS)
    {
        success = TRUE;
    }
    
    return success;
}

// Register TSF profiles directly using ITfInputProcessorProfileMgr
// This allows DIMESettings.exe to register profiles without loading the DLL
// dllPath: The path to the DLL (used for icon)
static BOOL RegisterTSFProfiles(LPCWSTR dllPath)
{
    HRESULT hr = S_FALSE;
    ITfInputProcessorProfileMgr *pProfileMgr = nullptr;
    
    hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER,
        IID_ITfInputProcessorProfileMgr, (void**)&pProfileMgr);
    if (FAILED(hr))
    {
        return FALSE;
    }
    
    DWORD cchPath = (DWORD)wcslen(dllPath);
    
    // Profile descriptions
    WCHAR descDayi[64] = L"DIME 大易輸入法";
    WCHAR descArray[64] = L"DIME 行列輸入法";
    WCHAR descPhonetic[64] = L"DIME 傳統注音";
    WCHAR descGeneric[64] = L"DIME 自建輸入法";
    
    // Register Dayi profile
    hr = pProfileMgr->RegisterProfile(
        Global::DIMECLSID,
        TEXTSERVICE_LANGID,
        Global::DIMEDayiGuidProfile,
        descDayi, (ULONG)wcslen(descDayi),
        dllPath, cchPath,
        (UINT)TEXTSERVICE_DAYI_ICON_INDEX,
        NULL, 0, TRUE, 0);
    
    // Register Array profile
    hr = pProfileMgr->RegisterProfile(
        Global::DIMECLSID,
        TEXTSERVICE_LANGID,
        Global::DIMEArrayGuidProfile,
        descArray, (ULONG)wcslen(descArray),
        dllPath, cchPath,
        (UINT)TEXTSERVICE_ARRAY_ICON_INDEX,
        NULL, 0, TRUE, 0);
    
    // Register Phonetic profile
    hr = pProfileMgr->RegisterProfile(
        Global::DIMECLSID,
        TEXTSERVICE_LANGID,
        Global::DIMEPhoneticGuidProfile,
        descPhonetic, (ULONG)wcslen(descPhonetic),
        dllPath, cchPath,
        (UINT)TEXTSERVICE_PHONETIC_ICON_INDEX,
        NULL, 0, TRUE, 0);
    
    // Register Generic profile
    hr = pProfileMgr->RegisterProfile(
        Global::DIMECLSID,
        TEXTSERVICE_LANGID,
        Global::DIMEGenericGuidProfile,
        descGeneric, (ULONG)wcslen(descGeneric),
        dllPath, cchPath,
        (UINT)TEXTSERVICE_GENERIC_ICON_INDEX,
        NULL, 0, TRUE, 0);
    
    pProfileMgr->Release();
    return TRUE;
}

// Register TSF categories
static BOOL RegisterTSFCategories()
{
    ITfCategoryMgr* pCategoryMgr = nullptr;
    HRESULT hr = S_OK;

    hr = CoCreateInstance(CLSID_TF_CategoryMgr, NULL, CLSCTX_INPROC_SERVER, 
        IID_ITfCategoryMgr, (void**)&pCategoryMgr);
    if (FAILED(hr))
    {
        return FALSE;
    }

    for (int i = 0; i < ARRAYSIZE(SupportCategories); i++)
    {
        pCategoryMgr->RegisterCategory(Global::DIMECLSID, SupportCategories[i], Global::DIMECLSID);
    }

    pCategoryMgr->Release();
    return TRUE;
}

// Fix TIP registry entries to use correct DLL paths and icon indices
// TSF's RegisterProfile() writes the same icon path to both 32-bit and 64-bit registry,
// but we need x64 DLL path for 64-bit registry and x86 DLL path for 32-bit registry.
// This function manually corrects the IconFile and IconIndex entries after TSF registration.
//
// NOTE: HKLM\SOFTWARE\Microsoft\CTF\TIP is NOT WOW64-virtualized!
// Both 32-bit and 64-bit processes see the SAME registry key.
// So we only need to write once - using x64 path (preferred for 64-bit Windows).
// 32-bit apps can still load icons from x64 DLL path.
static BOOL FixTIPRegistryPaths(LPCWSTR x64DllPath, LPCWSTR x86DllPath)
{
    // DIME CLSID: {1DE68A87-FF3B-46A0-8F80-46730B2491B1}
    // TIP registry location: HKLM\SOFTWARE\Microsoft\CTF\TIP\{CLSID}\LanguageProfile\{langid}\{profileGUID}
    
    // Determine which path to use: prefer x64, fall back to x86
    LPCWSTR iconPath = NULL;
    if (x64DllPath && x64DllPath[0] != L'\0')
    {
        iconPath = x64DllPath;
    }
    else if (x86DllPath && x86DllPath[0] != L'\0')
    {
        iconPath = x86DllPath;
    }
    
    if (!iconPath)
    {
        return FALSE;
    }
    
    // LANGID 0x0404 = Traditional Chinese
    // Profiles and their corresponding icon indices (negative resource IDs)
    struct ProfileInfo {
        const GUID* guid;
        DWORD iconIndex;  // Stored as DWORD in registry
    };
    
    const ProfileInfo profiles[] = {
        { &Global::DIMEDayiGuidProfile, (DWORD)TEXTSERVICE_DAYI_ICON_INDEX },      // -11 = 0xFFFFFFF5
        { &Global::DIMEArrayGuidProfile, (DWORD)TEXTSERVICE_ARRAY_ICON_INDEX },    // -12 = 0xFFFFFFF4
        { &Global::DIMEPhoneticGuidProfile, (DWORD)TEXTSERVICE_PHONETIC_ICON_INDEX }, // -13 = 0xFFFFFFF3
        { &Global::DIMEGenericGuidProfile, (DWORD)TEXTSERVICE_GENERIC_ICON_INDEX }   // -14 = 0xFFFFFFF2
    };
    
    WCHAR clsidStr[64];
    StringFromGUID2(Global::DIMECLSID, clsidStr, 64);
    
    int updated = 0;
    
    for (int i = 0; i < ARRAYSIZE(profiles); i++)
    {
        WCHAR profileGuidStr[64];
        StringFromGUID2(*profiles[i].guid, profileGuidStr, 64);
        
        WCHAR keyPath[MAX_PATH];
        HKEY hKey = NULL;
        LONG result;
        
        StringCchPrintfW(keyPath, MAX_PATH, 
            L"SOFTWARE\\Microsoft\\CTF\\TIP\\%s\\LanguageProfile\\0x00000404\\%s",
            clsidStr, profileGuidStr);
        
        // CTF\TIP is NOT WOW64-virtualized - don't use KEY_WOW64_64KEY or KEY_WOW64_32KEY
        // Both 32-bit and 64-bit apps see the same key
        result = RegOpenKeyExW(HKEY_LOCAL_MACHINE, keyPath, 0, 
            KEY_WRITE, &hKey);
        if (result == ERROR_SUCCESS)
        {
            // Set IconFile (path to DLL)
            RegSetValueExW(hKey, L"IconFile", 0, REG_SZ, 
                (BYTE*)iconPath, (DWORD)(wcslen(iconPath) + 1) * sizeof(WCHAR));
            
            // Set IconIndex (negative resource ID stored as DWORD)
            DWORD iconIndex = profiles[i].iconIndex;
            RegSetValueExW(hKey, L"IconIndex", 0, REG_DWORD, 
                (BYTE*)&iconIndex, sizeof(DWORD));
            
            RegCloseKey(hKey);
            updated++;
        }
        else
        {
            debugPrint(L"FixTIPRegistryPaths: Failed to open key, error=%d, path=%s", result, keyPath);
        }
    }
    
    return (updated > 0);
}

// Unregister TSF profiles
static void UnregisterTSFProfiles()
{
    HRESULT hr = S_OK;
    ITfInputProcessorProfileMgr *pProfileMgr = nullptr;
    
    hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER,
        IID_ITfInputProcessorProfileMgr, (void**)&pProfileMgr);
    if (FAILED(hr))
    {
        return;
    }
    
    pProfileMgr->UnregisterProfile(Global::DIMECLSID, TEXTSERVICE_LANGID, Global::DIMEDayiGuidProfile, 0);
    pProfileMgr->UnregisterProfile(Global::DIMECLSID, TEXTSERVICE_LANGID, Global::DIMEArrayGuidProfile, 0);
    pProfileMgr->UnregisterProfile(Global::DIMECLSID, TEXTSERVICE_LANGID, Global::DIMEPhoneticGuidProfile, 0);
    pProfileMgr->UnregisterProfile(Global::DIMECLSID, TEXTSERVICE_LANGID, Global::DIMEGenericGuidProfile, 0);
    
    pProfileMgr->Release();
}

// Unregister TSF categories
static void UnregisterTSFCategories()
{
    ITfCategoryMgr* pCategoryMgr = nullptr;
    HRESULT hr = S_OK;

    hr = CoCreateInstance(CLSID_TF_CategoryMgr, NULL, CLSCTX_INPROC_SERVER, 
        IID_ITfCategoryMgr, (void**)&pCategoryMgr);
    if (FAILED(hr))
    {
        return;
    }

    for (int i = 0; i < ARRAYSIZE(SupportCategories); i++)
    {
        pCategoryMgr->UnregisterCategory(Global::DIMECLSID, SupportCategories[i], Global::DIMECLSID);
    }

    pCategoryMgr->Release();
}

// Unregister IME for MSIX package (called during uninstall)
// 1. Unregister TSF profiles and categories
// 2. Remove COM registration from HKLM (both 64-bit and 32-bit views)
// 3. Delete deployed DLLs from C:\ProgramData\DIME
static void UnregisterIMEForMSIX()
{
    BOOL isElevated = IsRunningAsAdmin();
    
    // If not elevated, try to elevate first
    // Note: During MSIX uninstall, UAC prompt may not work properly
    // So we copy the exe to temp and run from there
    if (!isElevated)
    {
        // Copy DIMESettings.exe to temp folder first (MSIX package folder may be locked during uninstall)
        WCHAR exePath[MAX_PATH];
        WCHAR tempPath[MAX_PATH];
        WCHAR tempExePath[MAX_PATH];
        
        GetModuleFileNameW(NULL, exePath, MAX_PATH);
        GetTempPathW(MAX_PATH, tempPath);
        StringCchPrintfW(tempExePath, MAX_PATH, L"%sDIMESettings_uninstall.exe", tempPath);
        
        // Copy exe to temp
        if (CopyFileW(exePath, tempExePath, FALSE))
        {
            // Run the copied exe with elevation
            SHELLEXECUTEINFOW sei = { sizeof(sei) };
            sei.lpVerb = L"runas";
            sei.lpFile = tempExePath;
            sei.lpParameters = L"/unregister_elevated";
            sei.nShow = SW_HIDE;
            sei.fMask = SEE_MASK_NOCLOSEPROCESS;
            
            if (ShellExecuteExW(&sei))
            {
                WaitForSingleObject(sei.hProcess, 30000);  // Wait up to 30 seconds
                CloseHandle(sei.hProcess);
            }
            
            // Delete temp exe
            DeleteFileW(tempExePath);
        }
        return;
    }
    
    // Elevated path - do the actual cleanup
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    
    // Unregister TSF profiles and categories directly (no DLL loading needed)
    UnregisterTSFProfiles();
    UnregisterTSFCategories();
    
    // Unregister COM from HKLM (both 64-bit and 32-bit registry views)
    UnregisterCOMServerFromRegistry(FALSE);  // 64-bit view
    UnregisterCOMServerFromRegistry(TRUE);   // 32-bit view (WOW6432Node)
    
    // Delete deployed DLLs from C:\ProgramData\DIME
    RemoveDeployedDlls();
    
    CoUninitialize();
}

// Build full path to a DLL relative to package root
static void GetPackageDllPath(LPCWSTR dllSubPath, LPWSTR outPath, size_t outPathSize)
{
    GetModuleFileNameW(NULL, outPath, (DWORD)outPathSize);
    
    // DIMESettings.exe is in: <package>\DIMESettings.exe (same folder as DIME subfolder)
    // DIME.dll is in: <package>\DIME\DIME.dll
    // So we just need to remove exe name and append the subpath
    WCHAR* lastSlash = wcsrchr(outPath, L'\\');
    if (lastSlash)
    {
        *(lastSlash + 1) = L'\0';  // Keep trailing slash -> <package>\ 
        StringCchCatW(outPath, outPathSize, dllSubPath);
    }
}

// Get the destination path for DLL deployment (accessible by all apps)
// Returns: C:\ProgramData\DIME\<arch>\DIME.dll
static void GetDeployedDllPath(BOOL isX86, LPWSTR outPath, size_t outPathSize)
{
    // Use ProgramData for system-wide access (requires admin to write, but readable by all)
    WCHAR programData[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_COMMON_APPDATA, NULL, 0, programData)))
    {
        StringCchPrintfW(outPath, outPathSize, L"%s\\DIME\\%s\\DIME.dll", 
            programData, isX86 ? L"x86" : L"x64");
    }
}


// Copy DLL from WindowsApps package to accessible location
// Returns TRUE if successful
static BOOL DeployDllToAccessibleLocation(LPCWSTR sourcePath, LPCWSTR destPath)
{
    // Create directory structure
    WCHAR destDir[MAX_PATH];
    StringCchCopyW(destDir, MAX_PATH, destPath);
    WCHAR* lastSlash = wcsrchr(destDir, L'\\');
    if (lastSlash)
    {
        *lastSlash = L'\0';
        
        // Create parent directories recursively
        WCHAR parentDir[MAX_PATH];
        StringCchCopyW(parentDir, MAX_PATH, destDir);
        WCHAR* slash = wcsrchr(parentDir, L'\\');
        if (slash)
        {
            *slash = L'\0';
            // Create C:\ProgramData\DIME (ignore error if already exists)
            if (!CreateDirectoryW(parentDir, NULL))
            {
                DWORD err = GetLastError();
                if (err != ERROR_ALREADY_EXISTS)
                {
                    debugPrint(L"DeployDll: Failed to create parent dir %s, error=%d", parentDir, err);
                }
            }
        }
        // Create C:\ProgramData\DIME\x64 or x86 (ignore error if already exists)
        if (!CreateDirectoryW(destDir, NULL))
        {
            DWORD err = GetLastError();
            if (err != ERROR_ALREADY_EXISTS)
            {
                debugPrint(L"DeployDll: Failed to create dest dir %s, error=%d", destDir, err);
            }
        }
    }
    
    // Copy the DLL (overwrite if exists)
    BOOL result = CopyFileW(sourcePath, destPath, FALSE);
    if (!result)
    {
        DWORD err = GetLastError();
        debugPrint(L"DeployDll: CopyFile failed, error=%d, src=%s, dest=%s", err, sourcePath, destPath);
    }
    return result;
}

// Remove deployed DLLs and CIN files during uninstall
static void RemoveDeployedDlls()
{
    WCHAR programData[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_COMMON_APPDATA, NULL, 0, programData)))
    {
        WCHAR path[MAX_PATH];
        
        // Delete x64 DLL
        StringCchPrintfW(path, MAX_PATH, L"%s\\DIME\\x64\\DIME.dll", programData);
        DeleteFileW(path);
        StringCchPrintfW(path, MAX_PATH, L"%s\\DIME\\x64", programData);
        RemoveDirectoryW(path);
        
        // Delete x86 DLL
        StringCchPrintfW(path, MAX_PATH, L"%s\\DIME\\x86\\DIME.dll", programData);
        DeleteFileW(path);
        StringCchPrintfW(path, MAX_PATH, L"%s\\DIME\\x86", programData);
        RemoveDirectoryW(path);
        
        // Delete .cin files
        const WCHAR* cinFiles[] = {
            L"Array.cin", L"Array40.cin", L"Dayi.cin", L"Phone.cin",
            L"Array-shortcode.cin", L"Array-special.cin",
            L"CnsPhone.cin", L"TCSC.cin"
        };
        for (int i = 0; i < ARRAYSIZE(cinFiles); i++)
        {
            StringCchPrintfW(path, MAX_PATH, L"%s\\DIME\\%s", programData, cinFiles[i]);
            DeleteFileW(path);
        }
        
        // Delete DIME folder
        StringCchPrintfW(path, MAX_PATH, L"%s\\DIME", programData);
        RemoveDirectoryW(path);
    }
}

// Deploy .cin files from package to C:\ProgramData\DIME\
// These files need to be accessible by all apps (unlike WindowsApps folder)
static BOOL DeployCinFiles()
{
    WCHAR programData[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_COMMON_APPDATA, NULL, 0, programData)))
    {
        return FALSE;
    }
    
    // Create C:\ProgramData\DIME folder
    WCHAR destDir[MAX_PATH];
    StringCchPrintfW(destDir, MAX_PATH, L"%s\\DIME", programData);
    CreateDirectoryW(destDir, NULL);
    
    // List of .cin files to deploy
    const WCHAR* cinFiles[] = {
        L"Array.cin", L"Array40.cin", L"Dayi.cin", L"Phone.cin",
        L"Array-shortcode.cin", L"Array-special.cin",
        L"CnsPhone.cin", L"TCSC.cin"
    };
    
    int deployed = 0;
    WCHAR srcPath[MAX_PATH];
    WCHAR destPath[MAX_PATH];
    
    for (int i = 0; i < ARRAYSIZE(cinFiles); i++)
    {
        // Source is in package root (same folder as DIMESettings.exe)
        GetPackageDllPath(cinFiles[i], srcPath, MAX_PATH);
        StringCchPrintfW(destPath, MAX_PATH, L"%s\\DIME\\%s", programData, cinFiles[i]);
        
        if (GetFileAttributesW(srcPath) != INVALID_FILE_ATTRIBUTES)
        {
            if (CopyFileW(srcPath, destPath, FALSE))
            {
                deployed++;
                debugPrint(L"DeployCinFiles: Deployed %s", cinFiles[i]);
            }
            else
            {
                debugPrint(L"DeployCinFiles: Failed to copy %s, error=%d", cinFiles[i], GetLastError());
            }
        }
    }
    
    debugPrint(L"DeployCinFiles: Deployed %d cin files", deployed);
    return (deployed > 0);
}

// Deploy cleanup script to C:\ProgramData\DIME\ for post-uninstall cleanup
// This script can be run by the user after uninstalling MSIX to clean up
// COM registrations, TSF profiles, and deployed DLLs
static BOOL DeployCleanupScript()
{
    WCHAR programData[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_COMMON_APPDATA, NULL, 0, programData)))
    {
        return FALSE;
    }
    
    // Create C:\ProgramData\DIME folder (should already exist from DLL deployment)
    WCHAR destDir[MAX_PATH];
    StringCchPrintfW(destDir, MAX_PATH, L"%s\\DIME", programData);
    CreateDirectoryW(destDir, NULL);
    
    // Copy DIME_Cleanup.ps1 from package to ProgramData
    WCHAR srcPath[MAX_PATH];
    WCHAR destPath[MAX_PATH];
    
    GetModuleFileNameW(NULL, srcPath, MAX_PATH);
    WCHAR* lastSlash = wcsrchr(srcPath, L'\\');
    if (lastSlash)
    {
        *(lastSlash + 1) = L'\0';
        StringCchCatW(srcPath, MAX_PATH, L"DIME_Cleanup.ps1");
    }
    
    StringCchPrintfW(destPath, MAX_PATH, L"%s\\DIME_Cleanup.ps1", destDir);
    
    // Check if source file exists
    if (GetFileAttributesW(srcPath) == INVALID_FILE_ATTRIBUTES)
    {
        debugPrint(L"DeployCleanupScript: Source not found: %s", srcPath);
        return FALSE;
    }
    
    BOOL result = CopyFileW(srcPath, destPath, FALSE);
    if (!result)
    {
        DWORD err = GetLastError();
        debugPrint(L"DeployCleanupScript: CopyFile failed, error=%d, src=%s, dest=%s", err, srcPath, destPath);
    }
    else
    {
        debugPrint(L"DeployCleanupScript: Success, %s -> %s", srcPath, destPath);
    }
    
    return result;
}

// Get the path to the deployed cleanup script in ProgramData
static void GetCleanupScriptPath(LPWSTR outPath, size_t outPathSize)
{
    WCHAR programData[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_COMMON_APPDATA, NULL, 0, programData)))
    {
        StringCchPrintfW(outPath, outPathSize, L"%s\\DIME\\DIME_Cleanup.ps1", programData);
    }
}

// Create a Start Menu shortcut that runs the cleanup script
// This shortcut is in the common Start Menu (all users) since cleanup requires admin
static BOOL CreateCleanupShortcut()
{
    // Get common program data path for script and shortcut
    WCHAR programData[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_COMMON_APPDATA, NULL, 0, programData)))
    {
        return FALSE;
    }
    
    // Shortcut path: C:\ProgramData\Microsoft\Windows\Start Menu\Programs\DIME 清理.lnk
    // This is the common Start Menu, visible to all users
    WCHAR shortcutPath[MAX_PATH];
    StringCchPrintfW(shortcutPath, MAX_PATH, 
        L"%s\\Microsoft\\Windows\\Start Menu\\Programs\\DIME 清理.lnk", programData);
    
    // Script path: C:\ProgramData\DIME\DIME_Cleanup.ps1
    WCHAR scriptPath[MAX_PATH];
    GetCleanupScriptPath(scriptPath, MAX_PATH);
    
    // Create shortcut using COM
    // Note: Don't call CoInitialize here - it's already initialized by caller
    
    IShellLinkW* pShellLink = NULL;
    IPersistFile* pPersistFile = NULL;
    BOOL success = FALSE;
    
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, 
                                   IID_IShellLinkW, (void**)&pShellLink);
    if (SUCCEEDED(hr))
    {
        // Set target: powershell.exe -ExecutionPolicy Bypass -File "script.ps1"
        WCHAR powershellPath[MAX_PATH];
        GetSystemDirectoryW(powershellPath, MAX_PATH);
        StringCchCatW(powershellPath, MAX_PATH, L"\\WindowsPowerShell\\v1.0\\powershell.exe");
        
        WCHAR arguments[MAX_PATH * 2];
        StringCchPrintfW(arguments, MAX_PATH * 2, 
            L"-ExecutionPolicy Bypass -NoProfile -File \"%s\"", scriptPath);
        
        pShellLink->SetPath(powershellPath);
        pShellLink->SetArguments(arguments);
        pShellLink->SetDescription(L"清理 DIME 輸入法的系統登錄和檔案 (解除安裝後執行)");
        pShellLink->SetIconLocation(powershellPath, 0);
        
        // Working directory
        WCHAR workingDir[MAX_PATH];
        StringCchPrintfW(workingDir, MAX_PATH, L"%s\\DIME", programData);
        pShellLink->SetWorkingDirectory(workingDir);
        
        // Save shortcut
        hr = pShellLink->QueryInterface(IID_IPersistFile, (void**)&pPersistFile);
        if (SUCCEEDED(hr))
        {
            hr = pPersistFile->Save(shortcutPath, TRUE);
            if (SUCCEEDED(hr))
            {
                success = TRUE;
                debugPrint(L"CreateCleanupShortcut: Success at %s", shortcutPath);
            }
            else
            {
                debugPrint(L"CreateCleanupShortcut: IPersistFile::Save failed, hr=0x%08X, path=%s", hr, shortcutPath);
            }
            pPersistFile->Release();
        }
        else
        {
            debugPrint(L"CreateCleanupShortcut: QueryInterface(IPersistFile) failed, hr=0x%08X", hr);
        }
        pShellLink->Release();
    }
    else
    {
        debugPrint(L"CreateCleanupShortcut: CoCreateInstance(ShellLink) failed, hr=0x%08X", hr);
    }
    
    return success;
}

// Register IME for MSIX package
// 1. Copy DLLs from WindowsApps to C:\ProgramData\DIME\ (accessible by all apps)
// 2. Register COM directly to registry (for both x64 and x86)
// 3. Register TSF profiles and categories directly (no DLL loading needed)
// testMode: TRUE to skip MSIX check (for testing without rebuilding package)
static void RegisterIMEForMSIX(BOOL isElevated, BOOL testMode)
{
    // Only register if running from MSIX package (unless in test mode)
    if (!testMode && !IsRunningAsMSIX())
    {
        debugPrint(L"Not running as MSIX, skipping registration");
        return;
    }

    if (!isElevated)
    {
        debugPrint(L"Not elevated, skipping registration");
        return;
    }

    debugPrint(L"RegisterIMEForMSIX: Starting deployment and registration");
    
    
    // Initialize COM for TSF registration (ITfInputProcessorProfileMgr, ITfCategoryMgr)
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    
    WCHAR srcPath[MAX_PATH];
    WCHAR destPath[MAX_PATH];
    WCHAR iconDllPath[MAX_PATH] = {0};  // Path for TSF profile icon
    
    // Deploy x64 DLL and register COM (for 64-bit apps)
    GetPackageDllPath(L"DIME\\DIME.dll", srcPath, MAX_PATH);
    GetDeployedDllPath(FALSE, destPath, MAX_PATH);  // x64 path
    
    BOOL x64Deployed = FALSE;
    BOOL x64Available = FALSE;  // TRUE if x64 DLL exists at destination (deployed or already there)
    BOOL x64SourceExists = (GetFileAttributesW(srcPath) != INVALID_FILE_ATTRIBUTES);
    BOOL x64DestExists = (GetFileAttributesW(destPath) != INVALID_FILE_ATTRIBUTES);
    
    if (x64SourceExists)
    {
        x64Deployed = DeployDllToAccessibleLocation(srcPath, destPath);
    }
    
    // Check if destination exists (either deployed now, already existed, or in test mode)
    x64DestExists = (GetFileAttributesW(destPath) != INVALID_FILE_ATTRIBUTES);
    if (x64DestExists)
    {
        x64Available = TRUE;
        // Register COM server to 64-bit registry view
        RegisterCOMServerToRegistry(destPath, FALSE);  // FALSE = 64-bit view
    }
    
    // Deploy x86 DLL and register COM (for 32-bit apps)
    GetPackageDllPath(L"DIME.x86\\DIME.dll", srcPath, MAX_PATH);
    GetDeployedDllPath(TRUE, destPath, MAX_PATH);  // x86 path
    
    BOOL x86Deployed = FALSE;
    BOOL x86Available = FALSE;  // TRUE if x86 DLL exists at destination
    BOOL x86SourceExists = (GetFileAttributesW(srcPath) != INVALID_FILE_ATTRIBUTES);
    BOOL x86DestExists = (GetFileAttributesW(destPath) != INVALID_FILE_ATTRIBUTES);
    WCHAR x86DllPath[MAX_PATH] = {0};  // Track x86 deployed path
    
    if (x86SourceExists)
    {
        x86Deployed = DeployDllToAccessibleLocation(srcPath, destPath);
    }
    
    // Check if destination exists
    x86DestExists = (GetFileAttributesW(destPath) != INVALID_FILE_ATTRIBUTES);
    if (x86DestExists)
    {
        x86Available = TRUE;
        // Register COM server to 32-bit registry view (WOW6432Node)
        RegisterCOMServerToRegistry(destPath, TRUE);  // TRUE = 32-bit view
        StringCchCopyW(x86DllPath, MAX_PATH, destPath);  // Save x86 path
    }
    
    // Get deployed DLL paths for TSF registration
    // x64 path for 64-bit registry, x86 path for 32-bit registry
    WCHAR x64DllPath[MAX_PATH] = {0};
    if (x64Available)
    {
        GetDeployedDllPath(FALSE, x64DllPath, MAX_PATH);  // x64 path
    }
    
    // For TSF profile registration, use x64 DLL path if available,
    // otherwise fall back to x86 path for initial registration.
    if (x64Available)
    {
        StringCchCopyW(iconDllPath, MAX_PATH, x64DllPath);
    }
    else if (x86Available)
    {
        StringCchCopyW(iconDllPath, MAX_PATH, x86DllPath);
    }
    
    // Register TSF profiles and categories directly (no DLL loading!)
    // This works because TSF COM interfaces can be called from any process
    if (iconDllPath[0] != L'\0')
    {
        RegisterTSFProfiles(iconDllPath);
        RegisterTSFCategories();
        
        // Fix TIP registry to use correct DLL paths for each registry view
        // TSF writes the same path to both views, but we need:
        // - x64 DLL path in 64-bit registry (for 64-bit apps)
        // - x86 DLL path in 32-bit registry/WOW6432Node (for 32-bit apps)
        FixTIPRegistryPaths(x64DllPath, x86DllPath);
    }
    
    // Deploy .cin files from package to C:\ProgramData\DIME\
    // These need to be accessible by all apps for the IME to load dictionaries
    debugPrint(L"RegisterIMEForMSIX: Deploying .cin files...");
    DeployCinFiles();
    
    // Deploy cleanup script and create Start Menu shortcut
    // These survive MSIX uninstall and allow users to clean up manually
    debugPrint(L"RegisterIMEForMSIX: Deploying cleanup script...");
    BOOL cleanupScriptDeployed = DeployCleanupScript();
    BOOL cleanupShortcutCreated = FALSE;
    
    if (cleanupScriptDeployed)
    {
        cleanupShortcutCreated = CreateCleanupShortcut();
        if (cleanupShortcutCreated)
        {
            debugPrint(L"Cleanup shortcut created in Start Menu");
        }
        else
        {
            debugPrint(L"Failed to create cleanup shortcut");
        }
    }
    else
    {
        debugPrint(L"Failed to deploy cleanup script");
    }
    
    debugPrint(L"RegisterIMEForMSIX: Completed");
    
    CoUninitialize();
    
    // Show completion message with cleanup status
    WCHAR msg[512];
    if (cleanupScriptDeployed && cleanupShortcutCreated)
    {
        StringCchPrintfW(msg, 512,
            L"DIME 輸入法註冊完成!\n\n"
            L"注意: 解除安裝 DIME 後，請從開始功能表執行\n"
            L"「DIME 清理」以移除系統登錄項目。");
    }
    else
    {
        StringCchPrintfW(msg, 512,
            L"DIME 輸入法註冊完成!\n\n"
            L"警告: 清理工具部署失敗 (腳本=%s, 捷徑=%s)\n"
            L"解除安裝後，請手動清除 DIME 的登錄項目。",
            cleanupScriptDeployed ? L"成功" : L"失敗",
            cleanupShortcutCreated ? L"成功" : L"失敗");
    }
    
    MessageBoxW(NULL, msg, L"DIME 註冊", MB_OK | MB_ICONINFORMATION);
}


typedef _Return_type_success_(return >= 0) LONG NTSTATUS;
#define STATUS_SUCCESS ((NTSTATUS)0x00000000)
#define STATUS_REVISION_MISMATCH ((NTSTATUS)0xC0000059)
typedef LONG(WINAPI* PFN_RtlVerifyVersionInfo)(OSVERSIONINFOEXW*, ULONG, ULONGLONG);

static BOOL inline IsWindowsVersionOrGreater(WORD major, WORD minor)
{
    static PFN_RtlVerifyVersionInfo RtlVerifyVersionInfoFn = NULL;
    if (!RtlVerifyVersionInfoFn)
    {
        HMODULE ntdllModule = GetModuleHandleW(L"ntdll.dll");
        if (ntdllModule)
        {
            RtlVerifyVersionInfoFn = (PFN_RtlVerifyVersionInfo)GetProcAddress(ntdllModule, "RtlVerifyVersionInfo");
        }
    }

    // Check if RtlVerifyVersionInfoFn is still NULL
    if (!RtlVerifyVersionInfoFn)
    {
        debugPrint(L"RtlVerifyVersionInfo function not found.");
        return FALSE; // Return FALSE if the function pointer is NULL
    }

    RTL_OSVERSIONINFOEXW versionInfo = { 0 };
    NTSTATUS status;
    ULONGLONG conditionMask = 0;
    versionInfo.dwOSVersionInfoSize = sizeof(RTL_OSVERSIONINFOEXW);
    versionInfo.dwMajorVersion = major;
    versionInfo.dwMinorVersion = minor;

    VER_SET_CONDITION(conditionMask, VER_MAJORVERSION, VER_GREATER_EQUAL);
    VER_SET_CONDITION(conditionMask, VER_MINORVERSION, VER_GREATER_EQUAL);

    status = RtlVerifyVersionInfoFn(&versionInfo,
        VER_MAJORVERSION | VER_MINORVERSION,
        conditionMask);

    if (status == STATUS_SUCCESS)
        return TRUE;

    return FALSE;
}

enum _PROCESS_DPI_AWARENESS
{
    Process_DPI_Unaware = 0,
    Process_System_DPI_Aware = 1,
    Process_Per_Monitor_DPI_Aware = 2
};


INT_PTR CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);

    BOOL isElevated = IsRunningAsAdmin();
    BOOL isMSIX = IsRunningAsMSIX();
    
    // Debug: Show what we received (disable for release)
    // WCHAR debugMsg[512];
    // StringCchPrintfW(debugMsg, 512, L"啟動參數: %s\n\n是否管理員: %s\n是否MSIX: %s", 
    //     lpCmdLine ? lpCmdLine : L"(無)", 
    //     isElevated ? L"是" : L"否",
    //     isMSIX ? L"是" : L"否");
    // MessageBoxW(NULL, debugMsg, L"DIMESettings 啟動", MB_OK);
    
    // Check for special command line arguments
    // /cleanup - Called by desktop6:UninstallAction during MSIX uninstall (not elevated)
    // This handler requests UAC elevation and runs the actual cleanup
    if (lpCmdLine && wcsstr(lpCmdLine, L"/cleanup") != NULL)
    {
        // Called by UninstallAction - request elevation and run cleanup
        UnregisterIMEForMSIX();
        return 0;
    }
    
    // /unregister_elevated - Called from temp copy with elevation, do actual cleanup
    if (lpCmdLine && wcsstr(lpCmdLine, L"/unregister_elevated") != NULL)
    {
        // This is the elevated instance running from temp folder
        // Do the actual unregistration work
        CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        
        // Unregister TSF profiles and categories directly (no DLL loading)
        UnregisterTSFProfiles();
        UnregisterTSFCategories();
        
        // Unregister COM from HKLM
        UnregisterCOMServerFromRegistry(FALSE);
        UnregisterCOMServerFromRegistry(TRUE);
        
        // Delete deployed DLLs
        RemoveDeployedDlls();
        
        CoUninitialize();
        return 0;
    }
    
    // /unregister - Called during MSIX uninstall (not elevated)
    if (lpCmdLine && wcsstr(lpCmdLine, L"/unregister") != NULL)
    {
        // Unregister mode - called during MSIX uninstall
        // Note: isMSIX may be false when running from temp copy
        UnregisterIMEForMSIX();
        return 0;
    }
    
    if (lpCmdLine && wcsstr(lpCmdLine, L"/register") != NULL)
    {
        // Silent registration mode - called after elevation
        // This does exactly what regsvr32 does: DllRegisterServer()
        // TODO: Remove /test bypass after testing
        BOOL testMode = (wcsstr(lpCmdLine, L"/test") != NULL);
        if (isMSIX || testMode)
        {
            RegisterIMEForMSIX(isElevated, testMode);
        }
        return 0;  // Exit after registration
    }
    
    // For MSIX: Check if we need to register
    // Use /forceregister to bypass the IsIMERegistered check
    BOOL forceRegister = (lpCmdLine && wcsstr(lpCmdLine, L"/forceregister") != NULL);
    if (isMSIX && (forceRegister || !IsIMERegistered()))
    {
        if (!isElevated)
        {
            // TSF registration requires HKLM access, so elevation is mandatory
            int result = MessageBoxW(NULL, 
                L"首次執行需要註冊輸入法。\n\n"
                L"TSF 輸入法必須以系統管理員身分註冊才能正常運作。\n\n"
                L"按「確定」繼續註冊（需要管理員權限）",
                L"DIME 輸入法註冊",
                MB_OKCANCEL | MB_ICONINFORMATION);
            
            if (result == IDOK)
            {
                // Re-launch with elevation and /register argument
                if (RelaunchAsAdmin(L"/register"))
                {
                    // Elevated process started successfully, wait a bit for it
                    Sleep(2000);
                }
            }
            // If user cancels, just continue to show settings (IME won't work until registered)
        }
        else
        {
            // Already elevated - register to HKLM (same as regsvr32)
            RegisterIMEForMSIX(TRUE);
        }
    }

    //Check single instance!
    //Make sure at most one instance of the tool is running
    HANDLE hMutexOneInstance(::CreateMutex(NULL, TRUE, DIME_SETTINGS_INSTANCE_MUTEX_NAME));
    bool bAlreadyRunning((::GetLastError() == ERROR_ALREADY_EXISTS));
    if (hMutexOneInstance == NULL || bAlreadyRunning)
    {
        if (hMutexOneInstance)
        {
            ::ReleaseMutex(hMutexOneInstance);
            ::CloseHandle(hMutexOneInstance);
        }
        SwitchToThisWindow(FindWindowW(NULL, L"DIME設定"), FALSE);
        return -1;
    }

    if (IsWindowsVersionOrGreater(8,1)){
        HMODULE hShcore = LoadLibrary(L"Shcore.dll");
        if (hShcore != NULL) {
            auto SetProcessDpiAwareness = reinterpret_cast<HRESULT(__stdcall*)(_PROCESS_DPI_AWARENESS)>(
                GetProcAddress(hShcore, "SetProcessDpiAwareness"));
            if (SetProcessDpiAwareness != nullptr) {
                SetProcessDpiAwareness(Process_System_DPI_Aware);
            } else {
                debugPrint(L"Failed to cast function SetProcessDpiAwareness in Shcore.dll");
            }
            FreeLibrary(hShcore);
        }    
    }

    HWND hDlg;
    MSG msg;
    BOOL ret;

    InitCommonControls();
    hDlg = CreateDialogParam(hInst, MAKEINTRESOURCE(IDD_DIMESETTINGS), 0, WndProc, 0);
    ShowWindow(hDlg, nCmdShow);

    while ((ret = GetMessage(&msg, 0, 0, 0)) != 0) {
        if (ret == -1)
            return -1;

        if (!IsDialogMessage(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return 0;

}

static void showIMESettings(HWND hDlg, IME_MODE imeMode)
{
    PROPSHEETPAGE psp;
    PROPSHEETHEADER psh;
    struct {
        int id;
        DLGPROC DlgProc;
    } DlgPage[] = {
        { IDD_DIALOG_COMMON, CConfig::CommonPropertyPageWndProc },
        { IDD_DIALOG_DICTIONARY, CConfig::DictionaryPropertyPageWndProc }

    };
    HPROPSHEETPAGE hpsp[_countof(DlgPage)] = { nullptr }; // Initialize all elements to nullptr
    int i;

    ZeroMemory(&psp, sizeof(PROPSHEETPAGE));
    psp.dwSize = sizeof(PROPSHEETPAGE);
    psp.dwFlags = PSP_PREMATURE;
    psp.hInstance = hInst;

    for (i = 0; i < _countof(DlgPage); i++)
    {
        psp.pszTemplate = MAKEINTRESOURCE(DlgPage[i].id);
        psp.pfnDlgProc = DlgPage[i].DlgProc;
        if (CreatePropertySheetPageW != nullptr) // Ensure the function pointer is valid
        {
            hpsp[i] = CreatePropertySheetPageW(&psp); // Call the function with the required argument
        }
    }

    ZeroMemory(&psh, sizeof(PROPSHEETHEADER));
    psh.dwSize = sizeof(PROPSHEETHEADER);
    psh.dwFlags = PSH_DEFAULT | PSH_NOCONTEXTHELP;
    psh.hInstance = hInst;
    psh.hwndParent = hDlg;
    psh.nPages = _countof(DlgPage);
    psh.phpage = hpsp;
    psh.pszCaption = L"DIME User Settings";

    WCHAR dialogCaption[MAX_PATH] = { 0 };

    CTfInputProcessorProfile* profile = new CTfInputProcessorProfile();
    LANGID langid = 1028;  //CHT Language ID.
    GUID guidProfile;

    if (imeMode == IME_MODE::IME_MODE_DAYI)
    {
        guidProfile = Global::DIMEDayiGuidProfile;
        StringCchCat(dialogCaption, MAX_PATH, L"DIME 大易輸入法設定");
    }
    else if (imeMode == IME_MODE::IME_MODE_ARRAY)
    {
        guidProfile = Global::DIMEArrayGuidProfile;
        StringCchCat(dialogCaption, MAX_PATH, L"DIME 行列輸入法設定");
    }
    else if (imeMode == IME_MODE::IME_MODE_GENERIC)
    {
        guidProfile = Global::DIMEGenericGuidProfile;
        StringCchCat(dialogCaption, MAX_PATH, L"DIME 自建輸入法設定");
    }
    else if (imeMode == IME_MODE::IME_MODE_PHONETIC)
    {
        guidProfile = Global::DIMEPhoneticGuidProfile;
        StringCchCat(dialogCaption, MAX_PATH, L"DIME 傳統注音輸入法設定");
    }
    // Reload reverse conversion list from system.
    if (SUCCEEDED(profile->CreateInstance()))
    {
        if(langid == 0)
            profile->GetCurrentLanguage(&langid);
        
        if (guidProfile == GUID_NULL)
        {
            CLSID clsid;
            profile->GetDefaultLanguageProfile(langid, GUID_TFCAT_TIP_KEYBOARD, &clsid, &guidProfile);
        }
        CDIMEArray <LanguageProfileInfo> langProfileInfoList;
        profile->GetReverseConversionProviders(langid, &langProfileInfoList);
        CConfig::SetReverseConvervsionInfoList(&langProfileInfoList);
    }
    // Load config file and set imeMode (will filtered out self from reverse conversion list)
    CConfig::LoadConfig(imeMode);
    // Show version on caption of IME settings dialog
    StringCchPrintf(dialogCaption, MAX_PATH, L"%s v%d.%d.%d.%d", dialogCaption,
        BUILD_VER_MAJOR, BUILD_VER_MINOR, BUILD_COMMIT_COUNT, BUILD_DATE_1);
    psh.pszCaption = dialogCaption;

    PropertySheetW(&psh);

}

// Message handler for about box.
INT_PTR CALLBACK WndProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDB_DIME_DAYI_SETTINGS:
            showIMESettings(hDlg, IME_MODE::IME_MODE_DAYI);
            break;
        case IDB_DIME_ARRAY_SETTINGS:
            showIMESettings(hDlg, IME_MODE::IME_MODE_ARRAY);
            break;
        case IDB_DIME_PHONETIC_SETTINGS:
            showIMESettings(hDlg, IME_MODE::IME_MODE_PHONETIC);
            break;
        case IDB_DIME_GENERIC_SETTINGS:
            showIMESettings(hDlg, IME_MODE::IME_MODE_GENERIC);
            break;
        case IDOK:
        case IDCANCEL:
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        default:
			break;
		}
        break;
    
        break;
    case WM_CLOSE:
        DestroyWindow(hDlg);
        return (INT_PTR)TRUE;

    case WM_DESTROY:
        PostQuitMessage(0);
        return (INT_PTR)TRUE;

    default:
        break;
    }
    return (INT_PTR)FALSE;
}
