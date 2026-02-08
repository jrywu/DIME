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
constexpr wchar_t DIME_SETTINGS_INSTANCE_MUTEX_NAME[] = L"{B11F1FB2-3ECC-409E-A036-4162ADCEF1A3}";

// Global Variables:
HINSTANCE hInst;                                // current instance


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
