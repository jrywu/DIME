//
//
// Derived from Microsoft Sample IME by Jeremy '13,7,17
//
//

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
