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
#include "SettingsWindow.h"
#include "SettingsShell.h"

#include <io.h>      // _open_osfhandle, _dup2, _fileno
#include <fcntl.h>   // _O_WRONLY, _O_TEXT
#include <dwmapi.h>
#include <uxtheme.h>
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

// Calls uxtheme ordinal 135 SetPreferredAppMode:
//   1 = AllowDark  (dark mode enabled for windows that opt in)
//   3 = ForceLight (locks the entire process to light mode; DWM ignores AllowDarkModeForWindow)
// Must be called before any window is created and whenever the system theme changes.
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
// Enable ComCtl32 v6 (visual styles, TaskDialogIndirect support)
#pragma comment(linker, "\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#include <commctrl.h>  
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



int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);

    // Initialize Windows version flags (same as DllMain.cpp)
    if (IsWindowsVersionOrGreater(10, 0, 17763))
        Global::isWindows1809OrLater = TRUE;

    // Check for --mode (GUI launch to specific IME mode) vs CLI commands.
    // --legacy (the old launcher + property sheet, removed in phase 5b-10)
    // still counts as a GUI launch and opens the settings window.
    bool useLegacyUI = false;
    bool useNewUI = false;
    IME_MODE guiMode = IME_MODE::IME_MODE_DAYI;
    {
        // SettingsLifecycle::ParseLaunch decides; --reset-ui (reset the window
        // placement, then launch the GUI) is carried out here.
        SettingsLifecycle::LaunchArgs launch = SettingsLifecycle::ParseLaunch(lpCmdLine);
        useLegacyUI = launch.legacy != 0;
        useNewUI = launch.newUi != 0;
        guiMode = (IME_MODE)launch.guiMode;
        if (launch.resetUi)
            SettingsShell::DeletePlacement(SettingsShell::kPlacementKey, SettingsShell::kPlacementValue);
    }

    // CLI mode: if arguments are present and not a GUI launch, run headless.
    if (lpCmdLine && lpCmdLine[0] != L'\0' && !useNewUI && !useLegacyUI)
    {
        // Check if the parent already redirected our stdout (pipe from
        // FOR /F, | pipe, or > file).  GUI-subsystem apps receive inherited
        // handles when the parent sets STARTF_USESTDHANDLES.
        HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
        bool outputRedirected = (hStdOut != NULL && hStdOut != INVALID_HANDLE_VALUE);

        if (outputRedirected)
        {
            // Stdout is a pipe or file — wire up C runtime streams to the
            // inherited handles so fwprintf(stdout, ...) writes to the pipe.
            int fd = _open_osfhandle((intptr_t)hStdOut, _O_WRONLY | _O_TEXT);
            if (fd >= 0)
                _dup2(fd, _fileno(stdout));
            HANDLE hStdErr = GetStdHandle(STD_ERROR_HANDLE);
            if (hStdErr && hStdErr != INVALID_HANDLE_VALUE)
            {
                int efd = _open_osfhandle((intptr_t)hStdErr, _O_WRONLY | _O_TEXT);
                if (efd >= 0)
                    _dup2(efd, _fileno(stderr));
            }
            int rc = RunCLI(lpCmdLine);
            fflush(stdout);
            fflush(stderr);
            return rc;
        }

        // Interactive case: attach to the parent console (e.g. cmd.exe) so
        // output reaches the terminal.  If there is no parent console (e.g.
        // launched from a shortcut), allocate one.
        bool attachedExisting = AttachConsole(ATTACH_PARENT_PROCESS) != FALSE;
        if (!attachedExisting)
            AllocConsole();
        FILE* fp = nullptr;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONOUT$", "w", stderr);
        // When attaching to an existing console the parent shell (PowerShell /
        // cmd) has already printed its next prompt because GUI-subsystem apps
        // are launched asynchronously.  Overwrite that prompt line: move the
        // cursor to column 0, blank out the line, so our output starts clean.
        if (attachedExisting)
        {
            HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
            CONSOLE_SCREEN_BUFFER_INFO csbi;
            if (GetConsoleScreenBufferInfo(hOut, &csbi))
            {
                COORD lineStart = { 0, csbi.dwCursorPosition.Y };
                DWORD filled = 0;
                FillConsoleOutputCharacterW(hOut, L' ', csbi.dwSize.X, lineStart, &filled);
                SetConsoleCursorPosition(hOut, lineStart);
            }
        }
        int rc = RunCLI(lpCmdLine);
        fflush(stdout);
        fflush(stderr);
        // GUI-subsystem apps launched from a console shell run asynchronously:
        // the shell prints its next prompt and re-enters ReadConsole before our
        // process exits.  Injecting a silent Enter into the console input buffer
        // unblocks the shell's ReadConsole call so the prompt reappears without
        // the user having to press Enter manually.
        if (attachedExisting)
        {
            HANDLE hStdIn = GetStdHandle(STD_INPUT_HANDLE);
            DWORD  savedMode = 0;
            GetConsoleMode(hStdIn, &savedMode);
            SetConsoleMode(hStdIn, savedMode & ~ENABLE_ECHO_INPUT);
            INPUT_RECORD ir[2] = {};
            ir[0].EventType                        = KEY_EVENT;
            ir[0].Event.KeyEvent.bKeyDown          = TRUE;
            ir[0].Event.KeyEvent.wRepeatCount      = 1;
            ir[0].Event.KeyEvent.wVirtualKeyCode   = VK_RETURN;
            ir[0].Event.KeyEvent.uChar.UnicodeChar = L'\r';
            ir[1]                                  = ir[0];
            ir[1].Event.KeyEvent.bKeyDown          = FALSE;
            DWORD written = 0;
            WriteConsoleInputW(hStdIn, ir, 2, &written);
            SetConsoleMode(hStdIn, savedMode);
        }
        FreeConsole();
        return rc;
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
        // Try new UI window first, then old dialog
        HWND hExisting = FindWindowW(SETTINGS_WND_CLASS, nullptr);
        if (!hExisting)
            hExisting = FindWindowW(NULL, L"DIME設定");
        if (hExisting) {
            // Send mode info via WM_COPYDATA if we have --mode
            if (useNewUI && lpCmdLine) {
                COPYDATASTRUCT cds = {};
                cds.dwData = WM_SETTINGS_SWITCH_MODE;
                cds.cbData = (DWORD)(wcslen(lpCmdLine) + 1) * sizeof(WCHAR);
                cds.lpData = (void*)lpCmdLine;
                SendMessage(hExisting, WM_COPYDATA, 0, (LPARAM)&cds);
            }
            SetForegroundWindow(hExisting);
        }
        return -1;
    }

    // DPI awareness: tiered approach with runtime fallback
    // Tier 1: Per-Monitor V2 (Win10 1703+) — automatic non-client/dialog/control scaling
    // Tier 2: Per-Monitor V1 (Win8.1+) — requires manual WM_DPICHANGED handling
    // Tier 3: System DPI Aware (Win8.1+) — correct on primary monitor only
    // Tier 4: Win7 — no DPI awareness API, no-op
    {
        BOOL dpiSet = FALSE;
        // Tier 1: SetProcessDpiAwarenessContext from User32.dll (Win10 1607+)
        HMODULE hUser32 = GetModuleHandle(L"User32.dll");
        if (hUser32) {
            typedef BOOL (WINAPI *_T_SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT);
            auto SetDpiCtx = reinterpret_cast<_T_SetProcessDpiAwarenessContext>(
                GetProcAddress(hUser32, "SetProcessDpiAwarenessContext"));
            if (SetDpiCtx && SetDpiCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
                debugPrint(L"DPI: Per-Monitor V2 awareness set");
                dpiSet = TRUE;
            }
        }
        // Tier 2/3: SetProcessDpiAwareness from Shcore.dll (Win8.1+)
        if (!dpiSet && IsWindowsVersionOrGreater(8,1)) {
            HMODULE hShcore = LoadLibrary(L"Shcore.dll");
            if (hShcore != NULL) {
                auto SetProcessDpiAwareness = reinterpret_cast<HRESULT(__stdcall*)(_PROCESS_DPI_AWARENESS)>(
                    GetProcAddress(hShcore, "SetProcessDpiAwareness"));
                if (SetProcessDpiAwareness != nullptr) {
                    // Try Per-Monitor V1 first, fall back to System
                    if (SUCCEEDED(SetProcessDpiAwareness(Process_Per_Monitor_DPI_Aware))) {
                        debugPrint(L"DPI: Per-Monitor V1 awareness set");
                    } else {
                        SetProcessDpiAwareness(Process_System_DPI_Aware);
                        debugPrint(L"DPI: System DPI awareness set");
                    }
                } else {
                    debugPrint(L"Failed to cast function SetProcessDpiAwareness in Shcore.dll");
                }
                FreeLibrary(hShcore);
            }
        }
    }

    // Set preferred app mode BEFORE any window is created.
    // ForceDark(2) makes ALL windows dark including MessageBox; AllowDark(1) only affects opt-in windows.
    if (CConfig::IsSystemDarkTheme())
        SetAppPreferredMode(2);

    InitCommonControls();

    // ====================================================================
    // New UI: sidebar + card-based settings window
    // Launch when: no args (default), or --mode <name> without CLI commands
    // ====================================================================
    {
        if (!useNewUI) {
            // No --mode specified, default to Dayi
            guiMode = IME_MODE::IME_MODE_DAYI;
        }
        // Initialize COM for TSF profile enumeration
        CoInitialize(nullptr);
        // Enumerate reverse conversion providers (exact copy from legacy path)
        {
            CTfInputProcessorProfile* profile = new CTfInputProcessorProfile();
            LANGID langid = 1028;  // CHT Language ID
            if (SUCCEEDED(profile->CreateInstance())) {
                if (langid == 0)
                    profile->GetCurrentLanguage(&langid);
                CDIMEArray<LanguageProfileInfo> langProfileInfoList;
                profile->GetReverseConversionProviders(langid, &langProfileInfoList);
                CConfig::SetReverseConvervsionInfoList(&langProfileInfoList);
            }
            delete profile;
        }
        // Load config so reverse conversion selection is read
        CConfig::LoadConfig(guiMode);
        int result = SettingsWindow::Run(hInstance, nCmdShow, guiMode);
        if (hMutexOneInstance) {
            ::ReleaseMutex(hMutexOneInstance);
            ::CloseHandle(hMutexOneInstance);
        }
        return result;
    }
}
