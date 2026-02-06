// DIME Settings.cpp : Defines the entry point for the application.
//

#include "framework.h"
#include <CommCtrl.h>
#include <ntddkbd.h>
#include <appmodel.h>  // For GetCurrentPackageFullName
#include <shellapi.h>  // For ShellExecuteEx (elevation)
#include <ShlObj.h>    // For SHGetFolderPath
#include "DIMESettings.h"
#include "..\Globals.h"
#include "..\Config.h"
#include "..\BuildInfo.h"
#include "..\TfInputProcessorProfile.h"

#pragma comment(lib, "ComCtl32.lib")  
constexpr wchar_t DIME_SETTINGS_INSTANCE_MUTEX_NAME[] = L"{B11F1FB2-3ECC-409E-A036-4162ADCEF1A3}";

// Global Variables:
HINSTANCE hInst;                                // current instance

// Function pointer types for DLL exports
typedef HRESULT (STDAPICALLTYPE *PFN_DllRegisterTSFProfiles)(void);
typedef HRESULT (STDAPICALLTYPE *PFN_DllRegisterServer)(void);
typedef HRESULT (STDAPICALLTYPE *PFN_DllUnregisterServer)(void);

// Forward declarations
static void GetPackageDllPath(LPCWSTR dllSubPath, LPWSTR outPath, size_t outPathSize);
static void GetDeployedDllPath(BOOL isX86, LPWSTR outPath, size_t outPathSize);
static BOOL DeployDllToAccessibleLocation(LPCWSTR sourcePath, LPCWSTR destPath);
static void RemoveDeployedDlls();
static BOOL RegisterCOMServerToRegistry(LPCWSTR dllPath, BOOL isWow64Dll);
static BOOL UnregisterCOMServerFromRegistry(BOOL isWow64Dll);
static void UnregisterIMEForMSIX();
static void RegisterIMEForMSIX(BOOL isElevated);  // Note: TSF requires HKLM, per-user not possible

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
    
    WCHAR debugMsg[1024];
    StringCchPrintfW(debugMsg, 1024, L"執行 regsvr32:\n\n%s %s", regsvr32Path, cmdLine);
    MessageBoxW(NULL, debugMsg, L"DEBUG regsvr32", MB_OK);
    
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
        
        StringCchPrintfW(debugMsg, 1024, L"regsvr32 完成\n\n退出碼: %d", exitCode);
        MessageBoxW(NULL, debugMsg, L"DEBUG regsvr32 結果", MB_OK);
        return (exitCode == 0);
    }
    else
    {
        StringCchPrintfW(debugMsg, 1024, L"regsvr32 執行失敗\n\n錯誤碼: %d", GetLastError());
        MessageBoxW(NULL, debugMsg, L"DEBUG regsvr32 錯誤", MB_OK | MB_ICONERROR);
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
    
    WCHAR debugMsg[1024];
    StringCchPrintfW(debugMsg, 1024, L"COM 註冊 %s\n\n路徑: %s\n32位元: %s", 
        success ? L"成功" : L"失敗", dllPath, isWow64Dll ? L"是" : L"否");
    MessageBoxW(NULL, debugMsg, L"DEBUG COM 註冊", MB_OK);
    
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

// Unregister IME for MSIX package (called during uninstall)
// 1. Unregister TSF profiles via DllUnregisterServer
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
    
    WCHAR dllPath[MAX_PATH];
    
    // Load deployed x86 DLL to call DllUnregisterServer for TSF profiles
    GetDeployedDllPath(TRUE, dllPath, MAX_PATH);  // x86 path
    if (GetFileAttributesW(dllPath) != INVALID_FILE_ATTRIBUTES)
    {
        HMODULE hDIME = LoadLibraryW(dllPath);
        if (hDIME)
        {
            PFN_DllUnregisterServer pfnUnregister = 
                (PFN_DllUnregisterServer)GetProcAddress(hDIME, "DllUnregisterServer");
            if (pfnUnregister)
            {
                pfnUnregister();  // Unregisters TSF profiles and categories
            }
            FreeLibrary(hDIME);
        }
    }
    
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
            CreateDirectoryW(parentDir, NULL);  // Create C:\ProgramData\DIME
        }
        CreateDirectoryW(destDir, NULL);  // Create C:\ProgramData\DIME\x64 or x86
    }
    
    // Copy the DLL
    BOOL result = CopyFileW(sourcePath, destPath, FALSE);
    
    WCHAR msg[1024];
    StringCchPrintfW(msg, 1024, L"複製 DLL:\n\n來源: %s\n目標: %s\n結果: %s", 
        sourcePath, destPath, result ? L"成功" : L"失敗");
    MessageBoxW(NULL, msg, L"DEBUG 部署 DLL", MB_OK);
    
    return result;
}

// Remove deployed DLLs during uninstall
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
        
        // Delete DIME folder
        StringCchPrintfW(path, MAX_PATH, L"%s\\DIME", programData);
        RemoveDirectoryW(path);
    }
}

// Deploy cleanup script to %LOCALAPPDATA%\DIME\ for post-uninstall cleanup
// This script can be run by the user after uninstalling MSIX to clean up
// COM registrations, TSF profiles, and deployed DLLs
static BOOL DeployCleanupScript()
{
    WCHAR localAppData[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData)))
    {
        MessageBoxW(NULL, L"無法取得 LocalAppData 路徑", L"DEBUG DeployCleanupScript", MB_OK | MB_ICONERROR);
        return FALSE;
    }
    
    // Create %LOCALAPPDATA%\DIME folder
    WCHAR destDir[MAX_PATH];
    StringCchPrintfW(destDir, MAX_PATH, L"%s\\DIME", localAppData);
    CreateDirectoryW(destDir, NULL);
    
    // Copy DIME_Cleanup.ps1 from package to LocalAppData
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
        WCHAR msg[1024];
        StringCchPrintfW(msg, 1024, L"清理腳本不存在!\n\n來源: %s\n錯誤碼: %d", srcPath, GetLastError());
        MessageBoxW(NULL, msg, L"DEBUG DeployCleanupScript", MB_OK | MB_ICONERROR);
        return FALSE;
    }
    
    BOOL result = CopyFileW(srcPath, destPath, FALSE);
    
    WCHAR msg[1024];
    StringCchPrintfW(msg, 1024, L"複製清理腳本:\n\n來源: %s\n目標: %s\n結果: %s\n錯誤碼: %d", 
        srcPath, destPath, result ? L"成功" : L"失敗", GetLastError());
    MessageBoxW(NULL, msg, L"DEBUG DeployCleanupScript", MB_OK);
    
    debugPrint(L"DeployCleanupScript: %s -> %s, result=%d", srcPath, destPath, result);
    
    return result;
}

// Create a Start Menu shortcut that runs the cleanup script
// This shortcut survives MSIX uninstall since it's in user's AppData
static BOOL CreateCleanupShortcut()
{
    // Get paths
    WCHAR appData[MAX_PATH];
    WCHAR localAppData[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appData)) ||
        FAILED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData)))
    {
        MessageBoxW(NULL, L"無法取得 AppData 路徑", L"DEBUG CreateCleanupShortcut", MB_OK | MB_ICONERROR);
        return FALSE;
    }
    
    // Shortcut path: %APPDATA%\Microsoft\Windows\Start Menu\Programs\DIME 清理.lnk
    WCHAR shortcutPath[MAX_PATH];
    StringCchPrintfW(shortcutPath, MAX_PATH, 
        L"%s\\Microsoft\\Windows\\Start Menu\\Programs\\DIME 清理.lnk", appData);
    
    // Script path: %LOCALAPPDATA%\DIME\DIME_Cleanup.ps1
    WCHAR scriptPath[MAX_PATH];
    StringCchPrintfW(scriptPath, MAX_PATH, L"%s\\DIME\\DIME_Cleanup.ps1", localAppData);
    
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
        StringCchPrintfW(workingDir, MAX_PATH, L"%s\\DIME", localAppData);
        pShellLink->SetWorkingDirectory(workingDir);
        
        // Save shortcut
        hr = pShellLink->QueryInterface(IID_IPersistFile, (void**)&pPersistFile);
        if (SUCCEEDED(hr))
        {
            hr = pPersistFile->Save(shortcutPath, TRUE);
            if (SUCCEEDED(hr))
            {
                success = TRUE;
                WCHAR msg[1024];
                StringCchPrintfW(msg, 1024, L"捷徑建立成功!\n\n路徑: %s", shortcutPath);
                MessageBoxW(NULL, msg, L"DEBUG CreateCleanupShortcut", MB_OK | MB_ICONINFORMATION);
                debugPrint(L"Created cleanup shortcut: %s", shortcutPath);
            }
            else
            {
                WCHAR msg[1024];
                StringCchPrintfW(msg, 1024, L"儲存捷徑失敗!\n\n路徑: %s\nHRESULT: 0x%08X", shortcutPath, hr);
                MessageBoxW(NULL, msg, L"DEBUG CreateCleanupShortcut", MB_OK | MB_ICONERROR);
            }
            pPersistFile->Release();
        }
        else
        {
            WCHAR msg[512];
            StringCchPrintfW(msg, 512, L"QueryInterface(IPersistFile) 失敗\nHRESULT: 0x%08X", hr);
            MessageBoxW(NULL, msg, L"DEBUG CreateCleanupShortcut", MB_OK | MB_ICONERROR);
        }
        pShellLink->Release();
    }
    else
    {
        WCHAR msg[512];
        StringCchPrintfW(msg, 512, L"CoCreateInstance(ShellLink) 失敗\nHRESULT: 0x%08X", hr);
        MessageBoxW(NULL, msg, L"DEBUG CreateCleanupShortcut", MB_OK | MB_ICONERROR);
    }
    
    return success;
}

// Register IME for MSIX package
// 1. Copy DLLs from WindowsApps to C:\ProgramData\DIME\ (accessible by all apps)
// 2. Register COM with the deployed paths
// 3. Register TSF profiles using the deployed DLL
static void RegisterIMEForMSIX(BOOL isElevated)
{
    MessageBoxW(NULL, L"進入 RegisterIMEForMSIX 函數", L"DEBUG 1", MB_OK);
    
    // Only register if running from MSIX package
    if (!IsRunningAsMSIX())
    {
        MessageBoxW(NULL, L"IsRunningAsMSIX 返回 FALSE，跳過註冊", L"DEBUG 2", MB_OK);
        debugPrint(L"Not running as MSIX, skipping registration");
        return;
    }

    if (!isElevated)
    {
        MessageBoxW(NULL, L"未提權，跳過註冊", L"DEBUG", MB_OK);
        debugPrint(L"Not elevated, skipping registration");
        return;
    }


    MessageBoxW(NULL, L"IsRunningAsMSIX=TRUE, isElevated=TRUE，開始註冊", L"DEBUG 3", MB_OK);
    debugPrint(L"RegisterIMEForMSIX: Starting deployment and registration");
    
    // Initialize COM for TSF registration (ITfInputProcessorProfileMgr, ITfCategoryMgr)
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    
    WCHAR srcPath[MAX_PATH];
    WCHAR destPath[MAX_PATH];
    WCHAR msg[512];
    
    // Deploy and register native architecture DLL (x64 on x64 Windows, ARM64 on ARM64)
    GetPackageDllPath(L"DIME\\DIME.dll", srcPath, MAX_PATH);
    GetDeployedDllPath(FALSE, destPath, MAX_PATH);  // x64 path
    
    if (GetFileAttributesW(srcPath) != INVALID_FILE_ATTRIBUTES)
    {
        StringCchPrintfW(msg, 512, L"部署 Native DLL:\n來源: %s\n目標: %s", srcPath, destPath);
        MessageBoxW(NULL, msg, L"DEBUG Native DLL", MB_OK);
        
        // Copy DLL to accessible location
        if (DeployDllToAccessibleLocation(srcPath, destPath))
        {
            // Register COM with the DEPLOYED path (not WindowsApps path)
            // FALSE for isWow64Dll (native DLL)
            RegisterCOMServerToRegistry(destPath, FALSE);
        }
    }
    else
    {
        StringCchPrintfW(msg, 512, L"Native DLL 不存在: %s", srcPath);
        MessageBoxW(NULL, msg, L"DEBUG - 檔案不存在", MB_OK | MB_ICONWARNING);
    }
    
    // Deploy and register x86 DLL for 32-bit application support
    GetPackageDllPath(L"DIME.x86\\DIME.dll", srcPath, MAX_PATH);
    GetDeployedDllPath(TRUE, destPath, MAX_PATH);  // x86 path
    
    if (GetFileAttributesW(srcPath) != INVALID_FILE_ATTRIBUTES)
    {
        StringCchPrintfW(msg, 512, L"部署 x86 DLL:\n來源: %s\n目標: %s", srcPath, destPath);
        MessageBoxW(NULL, msg, L"DEBUG x86 DLL", MB_OK);
        
        // Copy DLL to accessible location
        if (DeployDllToAccessibleLocation(srcPath, destPath))
        {
            // Register COM with the DEPLOYED path (32-bit view / WOW6432Node)
            // TRUE for isWow64Dll (x86 DLL)
            RegisterCOMServerToRegistry(destPath, TRUE);
            
            // Load the DEPLOYED x86 DLL to call TSF registration
            // This ensures the icon path in TSF registration points to the accessible location
            HMODULE hDIME = LoadLibraryW(destPath);
            if (hDIME)
            {
                PFN_DllRegisterTSFProfiles pfnRegisterTSF = 
                    (PFN_DllRegisterTSFProfiles)GetProcAddress(hDIME, "DllRegisterTSFProfiles");
                if (pfnRegisterTSF)
                {
                    HRESULT hr = pfnRegisterTSF();
                    StringCchPrintfW(msg, 512, L"DllRegisterTSFProfiles: 0x%08X", hr);
                    MessageBoxW(NULL, msg, L"DEBUG TSF 註冊", MB_OK);
                }
                else
                {
                    MessageBoxW(NULL, L"找不到 DllRegisterTSFProfiles 函數", L"DEBUG", MB_OK | MB_ICONWARNING);
                }
                FreeLibrary(hDIME);
            }
            else
            {
                DWORD err = GetLastError();
                StringCchPrintfW(msg, 512, L"無法載入已部署的 x86 DLL\n錯誤碼: %d", err);
                MessageBoxW(NULL, msg, L"DEBUG LoadLibrary 失敗", MB_OK | MB_ICONWARNING);
            }
        }
    }
    else
        {
            debugPrint(L"x86 DLL not found (optional): %s", srcPath);
        }
    
        // Deploy cleanup script and create Start Menu shortcut
        // These survive MSIX uninstall and allow users to clean up manually
        debugPrint(L"RegisterIMEForMSIX: Deploying cleanup script...");
        if (DeployCleanupScript())
        {
            if (CreateCleanupShortcut())
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
    
        MessageBoxW(NULL, 
            L"DIME 輸入法註冊完成!\n\n"
            L"注意: 解除安裝 DIME 後，請從開始功能表執行\n"
            L"「DIME 清理」以移除系統登錄項目。", 
            L"DIME 註冊", MB_OK | MB_ICONINFORMATION);
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
        
        WCHAR dllPath[MAX_PATH];
        
        // Load deployed x86 DLL to call DllUnregisterServer for TSF profiles
        GetDeployedDllPath(TRUE, dllPath, MAX_PATH);
        if (GetFileAttributesW(dllPath) != INVALID_FILE_ATTRIBUTES)
        {
            HMODULE hDIME = LoadLibraryW(dllPath);
            if (hDIME)
            {
                PFN_DllUnregisterServer pfnUnregister = 
                    (PFN_DllUnregisterServer)GetProcAddress(hDIME, "DllUnregisterServer");
                if (pfnUnregister)
                {
                    pfnUnregister();
                }
                FreeLibrary(hDIME);
            }
        }
        
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
        MessageBoxW(NULL, L"進入 /register 模式", L"DIMESettings", MB_OK);
        
        // Silent registration mode - called after elevation
        // This does exactly what regsvr32 does: DllRegisterServer()
        if (isMSIX)
        {
            RegisterIMEForMSIX(isElevated);
        }
        else
        {
            MessageBoxW(NULL, L"不是 MSIX 模式，跳過註冊", L"DIMESettings", MB_OK);
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
