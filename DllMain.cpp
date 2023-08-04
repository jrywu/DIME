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
#pragma warning(disable : 4996)
//+---------------------------------------------------------------------------
//
// DllMain
//
//----------------------------------------------------------------------------

BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID pvReserved)
{
	pvReserved;

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
		// Check windows version
		OSVERSIONINFOW g_ovi;
		ZeroMemory(&g_ovi, sizeof(g_ovi));
        g_ovi.dwOSVersionInfoSize = sizeof(g_ovi);
        GetVersionEx(&g_ovi);
		if((g_ovi.dwMajorVersion == 6 && g_ovi.dwMinorVersion >= 2) || g_ovi.dwMajorVersion > 6)
        { // >windows 8
			Global::isWindows8 = TRUE;
        }
		if ((g_ovi.dwMajorVersion == 6 && g_ovi.dwMinorVersion > 2) || g_ovi.dwMajorVersion > 6)
		{ // > windows 8.1.  Load Shcore.dll for dpi awared font size adjustment
			Global::hShcore = LoadLibrary(L"Shcore.dll");
			_T_GetDpiForMonitor getDpiForMonitor = nullptr;
			if(Global::hShcore)
				getDpiForMonitor = reinterpret_cast<_T_GetDpiForMonitor>(GetProcAddress(Global::hShcore, "GetDpiForMonitor"));
			if(getDpiForMonitor) 
				CConfig::SetGetDpiForMonitor(getDpiForMonitor);
			else
				debugPrint(L"DllMain() Failed to cast function GetDpiForMonitor in Shcore.dll");
		}
		//load global resource strings
		LoadString(Global::dllInstanceHandle, IDS_IME_MODE, Global::ImeModeDescription, 50);
		LoadString(Global::dllInstanceHandle, IDS_DOUBLE_SINGLE_BYTE, Global::DoubleSingleByteDescription, 50);



        break;

    case DLL_PROCESS_DETACH:
		debugPrint(L"DllMain() DLL_PROCESS_DETACH");

		if(Global::hShcore)  FreeLibrary(Global::hShcore);

        DeleteCriticalSection(&Global::CS);

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
