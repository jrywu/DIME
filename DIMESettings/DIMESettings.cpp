// DIME Settings.cpp : Defines the entry point for the application.
//

#include "framework.h"
#include <CommCtrl.h>
#include <ntddkbd.h>
#include "DIMESettings.h"
#include "..\Globals.h"
#include "..\Config.h"
#include "..\BuildInfo.h"
#include "..\TfInputProcessorProfile.h"

#pragma comment(lib, "ComCtl32.lib")
//#pragma comment(lib, "shcore.lib")
#define DIME_SETTINGS_INSTANCE_MUTEX_NAME L"{B11F1FB2-3ECC-409E-A036-4162ADCEF1A3}"

// Global Variables:
HINSTANCE hInst;                                // current instance

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
    UNREFERENCED_PARAMETER(lpCmdLine);

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

    // Check windows version
    BOOL isAfterWindows8 = FALSE;

    typedef LONG(WINAPI* _T_RtlGetVersion)(RTL_OSVERSIONINFOEXW*);
    RTL_OSVERSIONINFOEXW pk_OsVer;
    memset(&pk_OsVer, 0, sizeof(RTL_OSVERSIONINFOEXW));
    pk_OsVer.dwOSVersionInfoSize = sizeof(RTL_OSVERSIONINFOEXW);
    HMODULE hNtDll = NULL;
    hNtDll = GetModuleHandleW(L"ntdll.dll");
    if (hNtDll != NULL)
    {
        _T_RtlGetVersion _RtlGetVersion = (_T_RtlGetVersion)GetProcAddress(hNtDll, "RtlGetVersion");
        if (hNtDll == NULL || _RtlGetVersion == nullptr)
            debugPrint(L"Failed to cast function RtlGetVersion in ntdll.dll"); // This will never happen (all processes load ntdll.dll)
        if (_RtlGetVersion(&pk_OsVer) == 0 &&
            ((pk_OsVer.dwMajorVersion == 6 && pk_OsVer.dwMinorVersion >= 2) || pk_OsVer.dwMajorVersion > 6)) isAfterWindows8 = TRUE;
        if (hNtDll != NULL) FreeLibrary(hNtDll);
        if (isAfterWindows8)
        {
            // tell system we are dpi aware
            HINSTANCE hShcore = NULL;
            hShcore = LoadLibrary(L"Shcore.dll");
            if (hShcore != NULL)
            {
                typedef HRESULT(__stdcall* _T_SetProcessDpiAwareness)(_Inout_  _PROCESS_DPI_AWARENESS awareness);
                _T_SetProcessDpiAwareness _SetProcessDpiAwareness = NULL;
                _SetProcessDpiAwareness = reinterpret_cast<_T_SetProcessDpiAwareness>(GetProcAddress(hShcore, "SetProcessDpiAwareness"));
                if (_SetProcessDpiAwareness == nullptr) {
                    debugPrint(L"Failed to cast function SetProcessDpiAwareness in Shcore.dll");
                }
                else
                    _SetProcessDpiAwareness(Process_System_DPI_Aware);
                if (hShcore != NULL) FreeLibrary(hShcore);
            }

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

void showIMESettings(HWND hDlg, IME_MODE imeMode)
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
    HPROPSHEETPAGE hpsp[_countof(DlgPage)];
    int i;

    ZeroMemory(&psp, sizeof(PROPSHEETPAGE));
    psp.dwSize = sizeof(PROPSHEETPAGE);
    psp.dwFlags = PSP_PREMATURE;
    psp.hInstance = hInst;

    for (i = 0; i < _countof(DlgPage); i++)
    {
        psp.pszTemplate = MAKEINTRESOURCE(DlgPage[i].id);
        psp.pfnDlgProc = DlgPage[i].DlgProc;
        if (CreatePropertySheetPageW)
            hpsp[i] = (*CreatePropertySheetPageW)(&psp);
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
