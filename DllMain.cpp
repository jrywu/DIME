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

#include <mutex>
#include <sal.h> // Include SAL annotations
#include "Globals.h"
//#include <VersionHelpers.h> // Include VersionHelpers for IsWindows* macros
#pragma warning(disable : 4996)

std::mutex g_mutex;

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

//+---------------------------------------------------------------------------
//
// DllMain
//
//----------------------------------------------------------------------------

BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID pvReserved)
{
    // Mark pvReserved as unreferenced to suppress the warning
    (void)pvReserved;

    switch (dwReason)
    {
    case DLL_PROCESS_ATTACH:
        debugPrint(L"DllMain() DLL_PROCESS_ATTACH");
        Global::dllInstanceHandle = hInstance;

        if (!InitializeCriticalSectionAndSpinCount(&Global::CS, 0))
        {
            return FALSE;
        }

        if (!Global::RegisterWindowClass()) {
            return FALSE;
        }

        // Check Windows version using IsWindows* macros
        if (IsWindowsVersionOrGreater(8,0))
        { // Windows 8 or greater
            Global::isWindows8 = TRUE;
        }
        if (IsWindowsVersionOrGreater(8,1))
        { // Windows 8.1 or greater. Load Shcore.dll for DPI-aware font size adjustment
            Global::hShcore = LoadLibrary(L"Shcore.dll");
            _T_GetDpiForMonitor getDpiForMonitor = nullptr;
            if (Global::hShcore)
                getDpiForMonitor = reinterpret_cast<_T_GetDpiForMonitor>(GetProcAddress(Global::hShcore, "GetDpiForMonitor"));
            if (getDpiForMonitor)
                CConfig::SetGetDpiForMonitor(getDpiForMonitor);
            else
                debugPrint(L"DllMain() Failed to cast function GetDpiForMonitor in Shcore.dll");
        }

        // Load global resource strings
        LoadString(Global::dllInstanceHandle, IDS_IME_MODE, Global::ImeModeDescription, 50);
        LoadString(Global::dllInstanceHandle, IDS_DOUBLE_SINGLE_BYTE, Global::DoubleSingleByteDescription, 50);

        break;

    case DLL_PROCESS_DETACH:
        debugPrint(L"DllMain() DLL_PROCESS_DETACH");

        if (Global::hShcore)
        {
            FreeLibrary(Global::hShcore);
            Global::hShcore = nullptr; // Set to nullptr after freeing
        }

        DeleteCriticalSection(&Global::CS);

#ifdef _DEBUG
        // Report memory leaks at process detach, after other cleanup
        _CrtDumpMemoryLeaks();
#endif
        break;

    case DLL_THREAD_ATTACH:
        debugPrint(L"DllMain() DLL_THREAD_ATTACH");
        break;

    case DLL_THREAD_DETACH:
        debugPrint(L"DllMain() DLL_THREAD_DETACH");
        break;
    }

    return TRUE;
}
