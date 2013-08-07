//
//
// Derived from Microsoft Sample IME by Jeremy '13,7,17
//
//


#include "Private.h"
#include "Globals.h"

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
        {
			Global::isWindows8 = TRUE;
        }
		//load global resource strings
		LoadString(Global::dllInstanceHandle, IDS_IME_MODE, Global::ImeModeDescription, 50);
		LoadString(Global::dllInstanceHandle, IDS_DOUBLE_SINGLE_BYTE, Global::DoubleSingleByteDescription, 50);



        break;

    case DLL_PROCESS_DETACH:

        DeleteCriticalSection(&Global::CS);

        break;

    case DLL_THREAD_ATTACH:

        break;

    case DLL_THREAD_DETACH:

        break;
    }

    return TRUE;
}
