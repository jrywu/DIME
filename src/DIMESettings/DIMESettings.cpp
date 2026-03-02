// DIME Settings.cpp : Defines the entry point for the application.
//

#include "framework.h"
#include <CommCtrl.h>
#include <ntddkbd.h>
#include "DIMESettings.h"
#include "..\Globals.h"
#include "..\CompositionProcessorEngine.h"
#include "..\Config.h"
#include "..\BuildInfo.h"
#include "..\TfInputProcessorProfile.h"

#include <dwmapi.h>
#include <uxtheme.h>
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

// Theme state for main launcher
static bool   s_isDarkTheme  = false;
static HBRUSH s_hBrushDlgBg  = nullptr;
static HBRUSH s_hBrushControlBg = nullptr;
// Set while showIMESettings is active to suppress re-applying theme during
// WM_THEMECHANGED broadcasts triggered by SetWindowTheme on property-sheet controls.
static bool   s_suppressThemeReapply = false;

// Calls uxtheme ordinal 135 SetPreferredAppMode:
//   1 = AllowDark  (dark mode enabled for windows that opt in)
//   3 = ForceLight (locks the entire process to light mode; DWM ignores AllowDarkModeForWindow)
// Must be called before any window is created and whenever the system theme changes.
#define WM_REASSERT_TITLEBAR (WM_APP + 1)
static void SetAppPreferredMode(int mode)
{
    typedef void (WINAPI* FnSetPreferredAppMode)(int);
    HMODULE hUxtheme = GetModuleHandleW(L"uxtheme.dll");
    if (hUxtheme) {
        auto pFn = reinterpret_cast<FnSetPreferredAppMode>(
            GetProcAddress(hUxtheme, MAKEINTRESOURCEA(135))
        );
        if (pFn) pFn(mode);
    }
}

#pragma comment(lib, "ComCtl32.lib")  
constexpr wchar_t DIME_SETTINGS_INSTANCE_MUTEX_NAME[] = L"{B11F1FB2-3ECC-409E-A036-4162ADCEF1A3}";

// Global Variables:
HINSTANCE hInst;                                // current instance


typedef _Return_type_success_(return >= 0) LONG NTSTATUS;
#define STATUS_SUCCESS ((NTSTATUS)0x00000000)
#define STATUS_REVISION_MISMATCH ((NTSTATUS)0xC0000059)
typedef LONG(WINAPI* PFN_RtlVerifyVersionInfo)(OSVERSIONINFOEXW*, ULONG, ULONGLONG);

// Global Windows version info for DIMESettings
namespace Global {
    DWORD g_WinMajorVersion = 0;
    DWORD g_WinMinorVersion = 0;
    DWORD g_WinBuildNumber = 0;
}

// Returns TRUE if current Windows version is at least (major, minor, build)
BOOL IsWindowsVersionOrGreater(DWORD major, DWORD minor, DWORD build)
{
    // Get version info once and cache
    static bool versionFetched = false;
    if (!versionFetched) {
        typedef NTSTATUS (WINAPI* RtlGetVersionFn)(PRTL_OSVERSIONINFOW);
        HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
        if (hNtdll) {
            auto pFn = reinterpret_cast<RtlGetVersionFn>(
                GetProcAddress(hNtdll, "RtlGetVersion")
            );
            RTL_OSVERSIONINFOW osvi = { sizeof(osvi) };
            if (pFn && pFn(&osvi) == 0) {
                Global::g_WinMajorVersion = osvi.dwMajorVersion;
                Global::g_WinMinorVersion = osvi.dwMinorVersion;
                Global::g_WinBuildNumber = osvi.dwBuildNumber;
            }
        }
        versionFetched = true;
    }
    if (Global::g_WinMajorVersion > major) return TRUE;
    if (Global::g_WinMajorVersion < major) return FALSE;
    if (Global::g_WinMinorVersion > minor) return TRUE;
    if (Global::g_WinMinorVersion < minor) return FALSE;
    if (Global::g_WinBuildNumber >= build) return TRUE;
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

    // Set preferred app mode BEFORE any window is created.
    // In dark mode: AllowDark(1) so our windows can opt-in.
    // In light mode: do NOT call SetPreferredAppMode at all — Windows defaults to
    // light and any call here primes uxtheme to track the window, causing
    // WM_THEMECHANGED broadcasts from the property sheet to flip the title bar.
    if (CConfig::IsSystemDarkTheme())
        SetAppPreferredMode(1);

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

static void ReassertTitleBarTheme(HWND hDlg)
{
    // In light mode, call NO dark-mode APIs — Windows defaults to light
    // naturally. Calling DwmSetWindowAttribute/AllowDarkModeForWindow even
    // with FALSE values primes uxtheme to track this window, and subsequent
    // WM_THEMECHANGED broadcasts from SetWindowTheme can then flip the title
    // bar dark. The default (no API calls) is always correct for light mode.
    if (!s_isDarkTheme) return;
    BOOL useDark = TRUE;
    DwmSetWindowAttribute(hDlg, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(BOOL));
    typedef BOOL (WINAPI* FnAllowDarkModeForWindow)(HWND, BOOL);
    HMODULE hUxtheme = GetModuleHandleW(L"uxtheme.dll");
    if (hUxtheme) {
        auto pFn = reinterpret_cast<FnAllowDarkModeForWindow>(
            GetProcAddress(hUxtheme, MAKEINTRESOURCEA(133))
        );
        if (pFn) pFn(hDlg, TRUE);
    }
    SetWindowPos(hDlg, nullptr, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
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
		// Create an owned DialogContext with its own temporary engine for settings UI.
		DialogContext* pCtx = new (std::nothrow) DialogContext();
		if (pCtx) {
			// Construct a temporary composition engine for the settings dialog
			// so the UI uses the same validation logic as the runtime IME.
			pCtx->imeMode = imeMode;
			// Set maxCodes based on IME mode (phonetic uses longer keys)
			pCtx->maxCodes = (imeMode == IME_MODE::IME_MODE_PHONETIC) ? MAX_KEY_LENGTH : CConfig::GetMaxCodes();
			pCtx->pEngine = new (std::nothrow) CCompositionProcessorEngine(nullptr);
			pCtx->engineOwned = true;
			if (pCtx->pEngine) {
				pCtx->pEngine->SetupDictionaryFile(imeMode);
				pCtx->pEngine->SetupConfiguration(imeMode);
				pCtx->pEngine->SetupKeystroke(imeMode);
			}
			psp.lParam = (LPARAM)pCtx;
		} else {
			psp.lParam = 0;
		}
        if (CreatePropertySheetPageW != nullptr) // Ensure the function pointer is valid
        {
            hpsp[i] = CreatePropertySheetPageW(&psp); // Call the function with the required argument
        }
        if (!hpsp[i] && pCtx) {
            // cleanup if page creation failed
            if (pCtx->engineOwned && pCtx->pEngine) delete pCtx->pEngine;
            delete pCtx;
        }
    }

    ZeroMemory(&psh, sizeof(PROPSHEETHEADER));
    psh.dwSize = sizeof(PROPSHEETHEADER);
    psh.dwFlags = PSH_DEFAULT | PSH_NOCONTEXTHELP | PSH_USECALLBACK;
    psh.pfnCallback = CConfig::PropSheetCallback;
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

    // Load Rich Edit control library (must be loaded before dialog creation)
    HINSTANCE dllRichEditHandle = LoadLibrary(L"msftedit.dll");

    PropertySheetW(&psh);
    if (dllRichEditHandle)
        FreeLibrary(dllRichEditHandle);
    // PropertySheetW is blocking. When it returns, queued WM_THEMECHANGED / WM_NCACTIVATE
    // messages from property-sheet teardown may still be pending in the queue.
    // Post a deferred message so ReassertTitleBarTheme runs AFTER all those are drained.
    PostMessage(hDlg, WM_REASSERT_TITLEBAR, 0, 0);
}

// Message handler for about box.
INT_PTR CALLBACK WndProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        s_isDarkTheme = CConfig::IsSystemDarkTheme();
        if (s_isDarkTheme) {
            s_hBrushDlgBg    = CreateSolidBrush(DARK_DIALOG_BG);
            s_hBrushControlBg = CreateSolidBrush(DARK_CONTROL_BG);
        }
        // Apply dark/light theme to child controls first; SetWindowTheme() inside broadcasts
        // WM_THEMECHANGED which can trigger DWM to re-read dark-mode state — so we set the
        // DWM title-bar attribute AFTER all child theming is complete.
        s_suppressThemeReapply = true;
        CConfig::ApplyDialogDarkTheme(hDlg, s_isDarkTheme);
        s_suppressThemeReapply = false;
        ReassertTitleBarTheme(hDlg);
        return (INT_PTR)TRUE;

    case WM_CTLCOLORDLG:
        if (s_isDarkTheme && s_hBrushDlgBg)
        {
            SetBkColor((HDC)wParam, DARK_DIALOG_BG);
            return (INT_PTR)s_hBrushDlgBg;
        }
        break;

    case WM_CTLCOLORSTATIC:
        if (s_isDarkTheme && s_hBrushDlgBg)
        {
            SetTextColor((HDC)wParam, DARK_TEXT);
            SetBkColor((HDC)wParam, DARK_DIALOG_BG);
            return (INT_PTR)s_hBrushDlgBg;
        }
        break;

    case WM_CTLCOLORBTN:
        if (s_isDarkTheme && s_hBrushControlBg)
        {
            SetTextColor((HDC)wParam, DARK_TEXT);
            SetBkColor((HDC)wParam, DARK_CONTROL_BG);
            SetBkMode((HDC)wParam, OPAQUE);
            return (INT_PTR)s_hBrushControlBg;
        }
        break;

    case WM_DRAWITEM:
        {
            LPDRAWITEMSTRUCT pdis = (LPDRAWITEMSTRUCT)lParam;
            if (pdis && pdis->CtlType == ODT_BUTTON && s_isDarkTheme)
            {
                CConfig::DrawDarkButton(pdis);
                return (INT_PTR)TRUE;
            }
        }
        break;

    case WM_NCACTIVATE:
        // Only reassert in dark mode. In light mode, touching dark-mode APIs here
        // primes uxtheme and causes the title bar to flip on WM_THEMECHANGED storms.
        if (s_isDarkTheme)
            ReassertTitleBarTheme(hDlg);
        break;

    case WM_THEMECHANGED:
        if (s_suppressThemeReapply) {
            ReassertTitleBarTheme(hDlg); // no-op in light mode
            break;
        }
        s_isDarkTheme = CConfig::IsSystemDarkTheme();
        // Update process-wide mode only when switching to dark.
        // In light mode, calling SetAppPreferredMode triggers uxtheme tracking
        // that can flip the title bar on subsequent WM_THEMECHANGED storms.
        if (s_isDarkTheme)
            SetAppPreferredMode(1);
        if (s_hBrushDlgBg) { DeleteObject(s_hBrushDlgBg); s_hBrushDlgBg = nullptr; }
        if (s_hBrushControlBg) { DeleteObject(s_hBrushControlBg); s_hBrushControlBg = nullptr; }
        if (s_isDarkTheme) {
            s_hBrushDlgBg    = CreateSolidBrush(DARK_DIALOG_BG);
            s_hBrushControlBg = CreateSolidBrush(DARK_CONTROL_BG);
        }
        s_suppressThemeReapply = true;
        CConfig::ApplyDialogDarkTheme(hDlg, s_isDarkTheme);
        s_suppressThemeReapply = false;
        ReassertTitleBarTheme(hDlg); // no-op in light mode
        InvalidateRect(hDlg, NULL, TRUE);
        break;

    case WM_REASSERT_TITLEBAR:
        // Deferred re-assertion posted after PropertySheetW returns so we run
        // after all teardown broadcasts have been fully processed by the message loop.
        ReassertTitleBarTheme(hDlg);
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

    case WM_CLOSE:
        DestroyWindow(hDlg);
        return (INT_PTR)TRUE;

    case WM_DESTROY:
        if (s_hBrushDlgBg) { DeleteObject(s_hBrushDlgBg); s_hBrushDlgBg = nullptr; }
        if (s_hBrushControlBg) { DeleteObject(s_hBrushControlBg); s_hBrushControlBg = nullptr; }
        PostQuitMessage(0);
        return (INT_PTR)TRUE;

    default:
        break;
    }
    return (INT_PTR)FALSE;
}
