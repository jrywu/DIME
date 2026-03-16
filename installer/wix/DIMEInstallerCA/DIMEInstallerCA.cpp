// DIMEInstallerCA.cpp
//
// Dual-mode source file:
//
//   DLL mode (default, no BUILDING_DOWNGRADE_CHECK defined):
//     MSI Custom Action DLL embedded in DIME-64bit.msi and DIME-32bit.msi.
//     No CRT dependency -- uses only kernel32, user32, advapi32, msi.lib.
//     Exports:
//       ConfirmAndRemoveNsisInstall   -- immediate, user session
//       ConfirmDowngrade              -- immediate, user session
//       MoveLockedDimeDll             -- deferred, SYSTEM
//       RollbackMoveLockedDimeDll     -- rollback, SYSTEM
//       CleanupDimeDllBak             -- commit, SYSTEM
//       CleanupBurnARPEarly           -- immediate, UISequence (registry only)
//       CleanupBurnRegistration       -- commit, SYSTEM (registry + folder)
//
//   EXE mode (BUILDING_DOWNGRADE_CHECK defined):
//     Standalone Win32 EXE (DowngradeCheck.exe) embedded in DIME-Universal.exe
//     as an ExePackage that runs before any MSI package.
//     Handles the "old Burn bundle over new Burn bundle" downgrade case:
//     WixInternalUIBootstrapperApplication quits silently when it detects a
//     newer installed bundle -- this EXE runs first and shows the dialog.
//     argv[1] = bundle version being installed ([WixBundleVersion] from Burn).
//     Returns 0 (proceed) or 1 (cancel, mapped to State=cancel in Bundle.wxs).

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")

#include <msi.h>
#include <msiquery.h>
#pragma comment(lib, "msi.lib")

// FileDispositionInfoEx / POSIX_SEMANTICS -- defined since Win10 1709 (SDK 10.0.16299).
// Define manually so this file compiles against older SDKs without change.
#ifndef FileDispositionInfoEx
#define FileDispositionInfoEx ((FILE_INFO_BY_HANDLE_CLASS)21)
#endif
#ifndef FILE_DISPOSITION_FLAG_DELETE
#define FILE_DISPOSITION_FLAG_DELETE          0x00000001UL
#endif
#ifndef FILE_DISPOSITION_FLAG_POSIX_SEMANTICS
#define FILE_DISPOSITION_FLAG_POSIX_SEMANTICS 0x00000002UL
#endif
typedef struct { ULONG Flags; } FILE_DISPOSITION_INFO_EX_LOCAL;

// FileRenameInfoEx / POSIX rename -- Win10 1809+ (SDK 10.0.17763).
// SetFileInformationByHandle(FileRenameInfoEx) atomically replaces the target
// file's directory entry without requiring the target's openers to have granted
// FILE_SHARE_DELETE.  Existing openers keep their handle to the old inode.
#ifndef FileRenameInfoEx
#define FileRenameInfoEx ((FILE_INFO_BY_HANDLE_CLASS)22)
#endif
#ifndef FILE_RENAME_FLAG_REPLACE_IF_EXISTS
#define FILE_RENAME_FLAG_REPLACE_IF_EXISTS  0x00000001UL
#endif
#ifndef FILE_RENAME_FLAG_POSIX_SEMANTICS
#define FILE_RENAME_FLAG_POSIX_SEMANTICS    0x00000002UL
#endif
// Sized to hold a MAX_PATH target name.
struct FILE_RENAME_INFO_EX_LOCAL {
    DWORD  Flags;
    HANDLE RootDirectory;
    DWORD  FileNameLength;
    WCHAR  FileName[MAX_PATH + 1];
};

// ============================================================================
// Registry handoff key
//
// HKLM\SOFTWARE\DIME\InstallerState is a temporary scratch key used to
// pass bak paths from MoveLockedDimeDll (deferred) to CleanupDimeDllBak
// (commit) and RollbackMoveLockedDimeDll (rollback), which run in separate
// MSI engine invocations and cannot share in-memory state.
//
// Values:
//   nsis_bak_0 = "C:\Windows\System32\DIME.dll.1.2.3.4.ABCD1234ABCD1234"
//   nsis_bak_1 = "C:\Windows\SysWOW64\DIME.dll.1.2.3.4.ABCD1234ABCD1234"
//   wix_bak_0  = "C:\Program Files\DIME\amd64\DIME.dll.1.2.3.4.1234ABCD1234ABCD"
//   ...
// ============================================================================

static const wchar_t kStateKey[] = L"SOFTWARE\\DIME\\InstallerState";

static void WriteInstallerState(const wchar_t* valueName, const wchar_t* path)
{
    HKEY hk = nullptr;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, kStateKey, 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE,
                        nullptr, &hk, nullptr) == ERROR_SUCCESS)
    {
        RegSetValueExW(hk, valueName, 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(path),
                       (lstrlenW(path) + 1) * sizeof(wchar_t));
        RegCloseKey(hk);
    }
}

// Writes an InstallerState entry encoding both the original path and the bak path
// as "src|bak".  RollbackMoveLockedDimeDll uses the src part to rename back;
// CleanupDimeDllBak uses the bak part for deletion.
static void WriteInstallerStateEntry(const wchar_t* valueName,
                                     const wchar_t* src, const wchar_t* bak)
{
    // MAX_PATH for src + 1 for '|' + (MAX_PATH + 64) for bak = MAX_PATH*2 + 65
    wchar_t combined[MAX_PATH * 2 + 68] = {};
    lstrcpynW(combined, src, MAX_PATH);
    lstrcatW(combined, L"|");
    int used = lstrlenW(combined);
    int room = MAX_PATH * 2 + 67 - used;
    if (room > 0) lstrcpynW(combined + used, bak, room + 1);
    WriteInstallerState(valueName, combined);
}

// Split an InstallerState "src|bak" value into components.
// Legacy single-path entries (no '|') copy the value into both buffers.
static void SplitStatePair(const wchar_t* data,
                           wchar_t* srcBuf, wchar_t* bakBuf, int cch)
{
    const wchar_t* pipe = wcschr(data, L'|');
    if (!pipe)
    {
        lstrcpynW(srcBuf, data, cch);
        lstrcpynW(bakBuf, data, cch);
    }
    else
    {
        int srcLen = (int)(pipe - data);
        lstrcpynW(srcBuf, data, srcLen < cch ? srcLen + 1 : cch);
        if (srcLen < cch) srcBuf[srcLen] = L'\0';
        lstrcpynW(bakBuf, pipe + 1, cch);
    }
}

// BakEntry.path must hold "src|bak" combined strings (up to MAX_PATH*2+68 chars).
struct BakEntry { wchar_t name[64]; wchar_t path[MAX_PATH * 2 + 68]; };

// Read all values from InstallerState. Returns count.
static int ReadAllInstallerState(BakEntry* entries, int maxEntries)
{
    HKEY hk = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kStateKey, 0, KEY_READ, &hk) != ERROR_SUCCESS)
        return 0;

    int n = 0;
    for (DWORD idx = 0; n < maxEntries; ++idx)
    {
        wchar_t name[64] = {};               DWORD cchName = 64;
        wchar_t path[MAX_PATH * 2 + 68] = {}; DWORD cbPath  = sizeof(path);
        DWORD type = 0;
        LONG r = RegEnumValueW(hk, idx, name, &cchName, nullptr, &type,
                               reinterpret_cast<BYTE*>(path), &cbPath);
        if (r == ERROR_NO_MORE_ITEMS) break;
        if (r != ERROR_SUCCESS || type != REG_SZ || !path[0]) continue;
        lstrcpynW(entries[n].name, name,              64);
        lstrcpynW(entries[n].path, path, MAX_PATH * 2 + 68);
        ++n;
    }
    RegCloseKey(hk);
    return n;
}

static void DeleteInstallerState()
{
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, kStateKey);
}

static void DeleteInstallerStateValue(const wchar_t* valueName)
{
    HKEY hk = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kStateKey, 0, KEY_SET_VALUE, &hk) == ERROR_SUCCESS)
    {
        RegDeleteValueW(hk, valueName);
        RegCloseKey(hk);
    }
}

// ============================================================================
// Shared helpers
// ============================================================================

// Split pipe-delimited wide string in-place. Returns token count.
static int SplitPipe(wchar_t* s, wchar_t** tokens, int maxTokens)
{
    int n = 0;
    if (!s || !s[0] || maxTokens < 1) return 0;
    tokens[n++] = s;
    for (wchar_t* p = s; *p; ++p)
    {
        if (*p == L'|' && n < maxTokens) { *p = L'\0'; tokens[n++] = p + 1; }
    }
    return n;
}

// Build bak name: "...\DIME.dll" -> "...\DIME.dll.{ver}.{tick16hex}"
// cchBak must be at least MAX_PATH + 64.
static void BuildUniqueBakName(const wchar_t* dll, const wchar_t* ver,
                               wchar_t* bak, DWORD cchBak)
{
    lstrcpynW(bak, dll, (int)cchBak);

    int len = lstrlenW(bak);
    int need = len + 1 + lstrlenW(ver) + 1 + 16 + 1;
    if (need > (int)cchBak)
    {
        lstrcatW(bak, L".old");
        return;
    }

    // Append ".{ver}"
    bak[len++] = L'.';
    lstrcatW(bak, ver);
    len = lstrlenW(bak);

    // Append ".{GetTickCount64() as 16 uppercase hex chars}"
    bak[len++] = L'.';
    ULONGLONG tick = GetTickCount64();
    for (int i = 15; i >= 0; --i)
    {
        ULONGLONG nibble = tick & 0xF;
        bak[len + i] = (nibble < 10) ? (L'0' + (wchar_t)nibble)
                                      : (L'A' + (wchar_t)(nibble - 10));
        tick >>= 4;
    }
    bak[len + 16] = L'\0';
}

// Mark a file for deletion using the best available method, with fallbacks:
//   1. DeleteFileW                           -- immediate if no process holds it
//   2. FileDispositionInfoEx + POSIX         -- Win10 1709+: dir entry gone instantly
//   3. FileDispositionInfo delete-pending    -- older OS: auto-delete on last close
//   4. MOVEFILE_DELAY_UNTIL_REBOOT          -- absolute last resort
static void MarkFileForDeletion(const wchar_t* path)
{
    if (DeleteFileW(path)) return;

    HANDLE h = CreateFileW(path, DELETE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE)
    {
        // Do NOT call MoveFileExW(MOVEFILE_DELAY_UNTIL_REBOOT) — that sets the
        // system reboot-pending flag and causes Burn to write Resume=3 back to
        // its ARP key.  The bak file (DIME.dll.{ver}.{tick}) is harmless on
        // disk; the next install's CleanupDimeDllBak glob-scan will delete it.
        return;
    }

    // Try POSIX unlink (Win10 1709+) -- removes dir entry immediately
    FILE_DISPOSITION_INFO_EX_LOCAL fdix = { FILE_DISPOSITION_FLAG_DELETE |
                                            FILE_DISPOSITION_FLAG_POSIX_SEMANTICS };
    bool done = !!SetFileInformationByHandle(h, FileDispositionInfoEx,
                                             &fdix, sizeof(fdix));
    if (!done)
    {
        // Fallback: traditional delete-pending
        FILE_DISPOSITION_INFO fdi = {};
        fdi.DeleteFile = TRUE;
        done = !!SetFileInformationByHandle(h, FileDispositionInfo,
                                            &fdi, sizeof(fdi));
    }
    CloseHandle(h);

    // If still !done, leave the bak file on disk.  Do NOT fall back to
    // MOVEFILE_DELAY_UNTIL_REBOOT — that sets the reboot-pending flag and
    // causes Burn to write Resume=3 to its ARP key.
    // The bak (DIME.dll.{ver}.{tick}) is harmless; next install cleans it.
}

// ============================================================================
// NSIS detection
// ============================================================================

static bool GetNsisUninstallString(wchar_t* out, DWORD cchOut)
{
    static const wchar_t kKey[] =
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\DIME";
    static const wchar_t kVal[] = L"UninstallString";

    DWORD cb = cchOut * sizeof(wchar_t);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, kKey, kVal,
                     RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY,
                     nullptr, out, &cb) == ERROR_SUCCESS && out[0])
        return true;

    cb = cchOut * sizeof(wchar_t);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, kKey, kVal,
                     RRF_RT_REG_SZ | RRF_SUBKEY_WOW6432KEY,
                     nullptr, out, &cb) == ERROR_SUCCESS && out[0])
        return true;

    return false;
}

// ============================================================================
// DLL entry point
// ============================================================================

#ifndef BUILDING_DOWNGRADE_CHECK
BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }

// ============================================================================
// SelfElevate  Execute="immediate"  (InstallUISequence, before AppSearch)
//
// Per-machine MSI launched via double-click or non-elevated cmd runs with a
// non-elevated client process.  Immediate CAs (including EarlyRenameForValidate)
// run in this non-elevated context and cannot rename image-mapped DLLs (err=5).
//
// This CA detects non-elevated context and re-launches msiexec elevated via
// ShellExecuteEx "runas", then aborts the current non-elevated session.
// The re-launched msiexec runs elevated from the start — all CAs run elevated.
//
// When already elevated (Burn, elevated cmd, /qn from elevated PS), this is a
// no-op and returns ERROR_SUCCESS immediately.
// ============================================================================
extern "C" __declspec(dllexport)
UINT __stdcall SelfElevate(MSIHANDLE hInstall)
{
    if (IsUserAnAdmin())
        return ERROR_SUCCESS;

    wchar_t msiPath[MAX_PATH] = {};
    DWORD cch = MAX_PATH;
    MsiGetPropertyW(hInstall, L"OriginalDatabase", msiPath, &cch);
    if (!msiPath[0])
        return ERROR_SUCCESS;  // can't determine path — proceed non-elevated

    wchar_t params[MAX_PATH + 64] = {};
    wsprintfW(params, L"/i \"%s\"", msiPath);

    SHELLEXECUTEINFOW sei = {};
    sei.cbSize       = sizeof(sei);
    sei.fMask        = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb       = L"runas";
    sei.lpFile       = L"msiexec.exe";
    sei.lpParameters = params;
    sei.nShow        = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&sei))
        return ERROR_SUCCESS;  // UAC declined or ShellExecute failed — proceed

    // Don't wait for the elevated instance — just abort this non-elevated session.
    if (sei.hProcess)
        CloseHandle(sei.hProcess);

    return ERROR_INSTALL_USEREXIT;
}

// Forward declarations of helpers defined later in this compile unit.
static void CaBurnLog(const wchar_t* line);
static void ScrubPendingFileRenameOperation(const wchar_t* targetPath);

// ============================================================================
// UnregisterDimeDll  Execute="deferred"  Impersonate="no" (SYSTEM)
//
// Replacement for WixQuietExec64 on the Unregister CAs.  Runs
// `regsvr32 /u /s <dll>` and sends INSTALLMESSAGE_PROGRESS heartbeats every
// second so the MSI progress dialog keeps receiving messages and Windows never
// flags the installer window as "not responding".
//
// WixQuietExec64 already waits INFINITE, but it sends no progress messages.
// During the ~2-minute TSF SendMessage broadcast that DllUnregisterServer()
// performs, the installer window appears frozen.  After ~2 minutes Windows
// shows a "not responding" Retry/Cancel dialog.  Sending heartbeats here
// prevents that detection entirely, letting regsvr32 finish however long it
// takes.
//
// CustomActionData: full quoted command line set by the preceding SetProperty,
//   e.g. "C:\Windows\system32\regsvr32.exe" /u /s "C:\Program Files\DIME\amd64\DIME.dll"
// Returns ERROR_SUCCESS always (best-effort, never aborts).
// ============================================================================
extern "C" __declspec(dllexport)
UINT __stdcall UnregisterDimeDll(MSIHANDLE hInstall)
{
    wchar_t cmd[MAX_PATH * 2 + 64] = {};
    DWORD cch = ARRAYSIZE(cmd);
    if (MsiGetPropertyW(hInstall, L"CustomActionData", cmd, &cch) != ERROR_SUCCESS
        || !cmd[0])
    {
        CaBurnLog(L"[UnregisterDimeDll] no CustomActionData — skipping");
        return ERROR_SUCCESS;
    }

    wchar_t logBuf[MAX_PATH * 2 + 80] = {};
    lstrcpyW(logBuf, L"[UnregisterDimeDll] ");
    lstrcatW(logBuf, cmd);
    CaBurnLog(logBuf);

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(nullptr, cmd, nullptr, nullptr,
                        FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
    {
        CaBurnLog(L"[UnregisterDimeDll] CreateProcessW failed — skipping");
        return ERROR_SUCCESS;
    }
    CloseHandle(pi.hThread);

    // Wait with 1-second ticks.  Send an INSTALLMESSAGE_PROGRESS heartbeat
    // each tick so the MSI engine and UI thread keep receiving messages,
    // preventing the Windows "not responding" detection regardless of how long
    // the TSF broadcast inside DllUnregisterServer() takes.
    for (;;)
    {
        DWORD dw = WaitForSingleObject(pi.hProcess, 1000);
        if (dw != WAIT_TIMEOUT)
            break;
        // field[1]=2 (progress tick), field[2]=0 (add 0 ticks to meter)
        MSIHANDLE hRec = MsiCreateRecord(2);
        if (hRec)
        {
            MsiRecordSetInteger(hRec, 1, 2);
            MsiRecordSetInteger(hRec, 2, 0);
            MsiProcessMessage(hInstall, INSTALLMESSAGE_PROGRESS, hRec);
            MsiCloseHandle(hRec);
        }
    }

    CloseHandle(pi.hProcess);
    CaBurnLog(L"[UnregisterDimeDll] regsvr32 finished");
    return ERROR_SUCCESS;
}

// ============================================================================
// ConfirmAndRemoveNsisInstall  Execute="immediate"  Impersonate="yes" (user)
//
// Detects and silently migrates a legacy NSIS DIME installation:
//   For each of System32\DIME.dll and SysWOW64\DIME.dll that exists:
//     1. Rename to unique bak  (old inode; running processes keep their ref)
//     2. CopyFileW(bak -> dll) (fresh unlocked inode; NSIS can delete it)
//     3. WriteInstallerState("nsis_bak_N", bak)
//   Launch uninst.exe /S and wait up to 2 minutes.
//
// Returns ERROR_SUCCESS always -- never aborts install.
// ============================================================================
extern "C" __declspec(dllexport)
UINT __stdcall ConfirmAndRemoveNsisInstall(MSIHANDLE hInstall)
{
    wchar_t uninstPath[MAX_PATH] = {};
    if (!GetNsisUninstallString(uninstPath, MAX_PATH))
        return ERROR_SUCCESS;

    wchar_t ver[64] = {};
    DWORD cch = 64;
    MsiGetPropertyW(hInstall, L"ProductVersion", ver, &cch);
    if (!ver[0]) lstrcpyW(ver, L"0.0.0.0");

    wchar_t sys32[MAX_PATH] = {};
    wchar_t syswow[MAX_PATH] = {};
    GetSystemDirectoryW(sys32, MAX_PATH);
    GetSystemWow64DirectoryW(syswow, MAX_PATH);

    const wchar_t* dirs[2] = { sys32, syswow };
    int nsisIdx = 0;

    for (int i = 0; i < 2; ++i)
    {
        if (!dirs[i][0]) continue;

        wchar_t dll[MAX_PATH + 16] = {};
        lstrcpynW(dll, dirs[i], MAX_PATH);
        lstrcatW(dll, L"\\DIME.dll");
        if (GetFileAttributesW(dll) == INVALID_FILE_ATTRIBUTES) continue;

        wchar_t bak[MAX_PATH + 64] = {};
        BuildUniqueBakName(dll, ver, bak, MAX_PATH + 64);

        // Scrub any stale PendingFileRenameOperations entry for this path so
        // it does not contribute to MsiSystemRebootPending=1 at InstallInitialize
        // and does not cascade into reboot-path handling for .cin and other files.
        ScrubPendingFileRenameOperation(dll);
        if (!MoveFileExW(dll, bak, 0)) continue;

        // Create fresh unlocked inode at the original path for NSIS to delete
        CopyFileW(bak, dll, FALSE);

        // Record src+bak pair for RollbackMoveLockedDimeDll and CleanupDimeDllBak.
        wchar_t valName[16] = L"nsis_bak_";
        valName[9]  = L'0' + (wchar_t)nsisIdx++;
        valName[10] = L'\0';
        WriteInstallerStateEntry(valName, dll, bak);
    }

    // Launch NSIS uninstaller silently
    wchar_t cmdLine[MAX_PATH + 16] = {};
    lstrcatW(cmdLine, L"\"");
    lstrcatW(cmdLine, uninstPath);
    lstrcatW(cmdLine, L"\" /S");

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    if (CreateProcessW(nullptr, cmdLine, nullptr, nullptr,
                       FALSE, 0, nullptr, nullptr, &si, &pi))
    {
        WaitForSingleObject(pi.hProcess, 120000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    return ERROR_SUCCESS;
}

// (forward declarations moved to just after DllMain above)

// ============================================================================
// ScrubPendingFileRenameOperation
//
// Removes any entry in HKLM\SYSTEM\CurrentControlSet\Control\Session Manager
// PendingFileRenameOperations whose source matches `targetPath`.
//
// PendingFileRenameOperations is REG_MULTI_SZ. Each rename is two consecutive
// strings: source (\??\<path>) and destination (empty = delete, \??\... = rename).
// If a previous failed install left a delete-pending entry for our DLL, it
// would delete the freshly installed DLL on the next reboot.  Scrubbing here
// prevents that.
// ============================================================================
static void ScrubPendingFileRenameOperation(const wchar_t* targetPath)
{
    static const wchar_t kKey[] =
        L"SYSTEM\\CurrentControlSet\\Control\\Session Manager";
    static const wchar_t kVal[] = L"PendingFileRenameOperations";

    // Build the \??\ prefix form of the target path
    wchar_t ntPath[MAX_PATH + 8] = L"\\??\\";
    lstrcatW(ntPath, targetPath);
    int ntLen = lstrlenW(ntPath);

    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kKey, 0, KEY_QUERY_VALUE | KEY_SET_VALUE,
                      &hKey) != ERROR_SUCCESS)
        return;

    DWORD type = 0, cbData = 0;
    if (RegQueryValueExW(hKey, kVal, nullptr, &type, nullptr, &cbData) != ERROR_SUCCESS
        || type != REG_MULTI_SZ || cbData < 4)
    {
        RegCloseKey(hKey);
        return;
    }

    DWORD nWchars = cbData / sizeof(wchar_t);
    wchar_t* buf = new wchar_t[nWchars + 2]{};
    if (RegQueryValueExW(hKey, kVal, nullptr, &type,
                         reinterpret_cast<BYTE*>(buf), &cbData) != ERROR_SUCCESS)
    {
        delete[] buf;
        RegCloseKey(hKey);
        return;
    }

    // Walk pairs (src, dst) and copy non-matching pairs to out[]
    wchar_t* out = new wchar_t[nWchars + 2]{};
    DWORD outLen = 0;
    bool changed = false;

    const wchar_t* p = buf;
    const wchar_t* end = buf + nWchars;
    while (p < end && *p)
    {
        const wchar_t* src = p;
        int srcLen = lstrlenW(src);
        p += srcLen + 1;

        const wchar_t* dst = (p < end) ? p : L"";
        int dstLen = lstrlenW(dst);
        if (p < end) p += dstLen + 1;

        // Case-insensitive comparison of source against our target
        bool match = (srcLen == ntLen
                      && CompareStringW(LOCALE_INVARIANT, NORM_IGNORECASE,
                                        src, srcLen, ntPath, ntLen) == CSTR_EQUAL);
        if (match)
        {
            changed = true;
            continue;  // drop this pair
        }

        // Keep this pair
        lstrcpyW(out + outLen, src); outLen += srcLen + 1;
        lstrcpyW(out + outLen, dst); outLen += dstLen + 1;
    }
    out[outLen++] = L'\0';  // final terminator

    if (changed)
    {
        RegSetValueExW(hKey, kVal, 0, REG_MULTI_SZ,
                       reinterpret_cast<const BYTE*>(out),
                       outLen * sizeof(wchar_t));
    }

    delete[] buf;
    delete[] out;
    RegCloseKey(hKey);
}

// ============================================================================
// CopyAndPosixReplace
//
// Fallback for MoveFileExW when the file is open without FILE_SHARE_DELETE:
//   1. CopyFileW(src -> tmpPath)          -- read src via FILE_SHARE_READ (granted)
//   2. Open tmpPath with DELETE access    -- we created it; no other openers
//   3. SetFileInformationByHandle(FileRenameInfoEx, POSIX|REPLACE, target=src)
//      -- atomically replaces src's directory entry with tmpPath's inode.
//         Existing openers of src keep their handle to the old inode (now
//         nameless); src at its original path is a fresh unlocked copy.
//
// Win10 1809+ only.  Returns false on older systems (SetFileInformationByHandle
// returns ERROR_INVALID_PARAMETER when FileRenameInfoEx is unsupported).
// On success tmpPath no longer exists (it was renamed to src).
// On failure tmpPath is deleted before returning.
//
// No InstallerState entry is required: src already holds the original content
// as a fresh inode; MSI's own per-file rollback restores it on failure.
// ============================================================================
static bool CopyAndPosixReplace(const wchar_t* src, const wchar_t* tmpPath)
{
    if (!CopyFileW(src, tmpPath, FALSE))
        return false;

    HANDLE hTmp = CreateFileW(tmpPath, DELETE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, 0, nullptr);
    if (hTmp == INVALID_HANDLE_VALUE)
    {
        DeleteFileW(tmpPath);
        return false;
    }

    int targetLen = lstrlenW(src);
    if (targetLen == 0 || targetLen > MAX_PATH)
    {
        CloseHandle(hTmp);
        DeleteFileW(tmpPath);
        return false;
    }

    FILE_RENAME_INFO_EX_LOCAL fri = {};
    fri.Flags          = FILE_RENAME_FLAG_REPLACE_IF_EXISTS | FILE_RENAME_FLAG_POSIX_SEMANTICS;
    fri.RootDirectory  = nullptr;
    fri.FileNameLength = (DWORD)(targetLen * sizeof(WCHAR));
    lstrcpynW(fri.FileName, src, MAX_PATH + 1);

    bool ok = !!SetFileInformationByHandle(hTmp, FileRenameInfoEx, &fri, sizeof(fri));
    CloseHandle(hTmp);
    if (!ok)
        DeleteFileW(tmpPath);  // rename failed; tmpPath still on disk — clean up
    return ok;
}

// ============================================================================
// MoveLockedDimeDll  Execute="deferred"  Impersonate="no" (SYSTEM)
//
// Renames each DLL listed in CustomActionData to a unique bak name and
// records the bak path in InstallerState.  Does NOT mark baks delete-pending
// -- that is CleanupDimeDllBak's responsibility (commit phase only), so that
// RollbackMoveLockedDimeDll can safely rename them back if install fails.
//
// Also scrubs PendingFileRenameOperations for each DLL path so that a
// delete-pending entry left by a previous failed install does not delete the
// freshly installed DLL on the next reboot.
//
// After a successful rename, copies the bak back to the original dll path
// (fresh unlocked inode, same bytes).  This lets RemoveExistingProducts (in
// the upgrade/downgrade case) unregister and delete the DLL cleanly without
// requesting a reboot.  Running processes keep their reference to the bak
// inode and are unaffected.  CleanupDimeDllBak (commit) marks the bak for
// deletion after the install succeeds.
//
// CustomActionData: "{ProductVersion}|{dll1}|{dll2}|..."
// Returns ERROR_SUCCESS always (best-effort, never aborts).
// ============================================================================
extern "C" __declspec(dllexport)
UINT __stdcall MoveLockedDimeDll(MSIHANDLE hInstall)
{
    wchar_t data[4096] = {};
    DWORD cch = 4096;
    if (MsiGetPropertyW(hInstall, L"CustomActionData", data, &cch) != ERROR_SUCCESS
        || !data[0])
    {
        CaBurnLog(L"[MoveLockedDimeDll] no CustomActionData — skipping");
        return ERROR_SUCCESS;
    }

    wchar_t* tokens[17] = {};
    int n = SplitPipe(data, tokens, 17);
    if (n < 2)
    {
        CaBurnLog(L"[MoveLockedDimeDll] too few tokens — skipping");
        return ERROR_SUCCESS;
    }

    wchar_t logBuf[512] = {};
    wsprintfW(logBuf, L"[MoveLockedDimeDll] ver=%s paths=%d", tokens[0], n - 1);
    CaBurnLog(logBuf);

    const wchar_t* ver = tokens[0];
    int wixIdx = 0;
    int cinIdx = 0;

    // Promote "early_bak_N" InstallerState entries written by EarlyRenameForValidate
    // (immediate CA, Before="InstallValidate") to "wix_bak_K" so that
    // RollbackMoveLockedDimeDll and CleanupDimeDllBak see them.
    // Mark the corresponding token as nullptr so the DLL loop skips it.
    {
        BakEntry stateEntries[16] = {};
        int stateCount = ReadAllInstallerState(stateEntries, 16);
        for (int si = 0; si < stateCount; ++si)
        {
            if (wcsncmp(stateEntries[si].name, L"early_bak_", 10) != 0) continue;
            wchar_t eDll[MAX_PATH] = {}, eBak[MAX_PATH] = {};
            SplitStatePair(stateEntries[si].path, eDll, eBak, MAX_PATH);
            if (!eDll[0] || !eBak[0]) continue;
            for (int ti = 1; ti < n; ++ti)
            {
                if (!tokens[ti] || !tokens[ti][0]) continue;
                if (lstrcmpiW(tokens[ti], eDll) != 0) continue;
                wchar_t newName[16] = L"wix_bak_";
                newName[8] = L'0' + (wchar_t)wixIdx++;
                newName[9] = L'\0';
                WriteInstallerStateEntry(newName, eDll, eBak);
                DeleteInstallerStateValue(stateEntries[si].name);
                wsprintfW(logBuf, L"[MoveLockedDimeDll] promoted %s -> %s: %s",
                          stateEntries[si].name, newName, eDll);
                CaBurnLog(logBuf);
                tokens[ti] = nullptr;
                break;
            }
        }
    }

    for (int i = 1; i < n; ++i)
    {
        const wchar_t* dll = tokens[i];
        if (!dll || !dll[0]) continue;

        if (GetFileAttributesW(dll) == INVALID_FILE_ATTRIBUTES)
        {
            wsprintfW(logBuf, L"[MoveLockedDimeDll] not found (ok): %s", dll);
            CaBurnLog(logBuf);
            continue;
        }

        wchar_t bak[MAX_PATH + 64] = {};
        BuildUniqueBakName(dll, ver, bak, MAX_PATH + 64);

        // Scrub any stale PendingFileRenameOperations entry for this path
        // left by a previous failed install, before we do anything else.
        ScrubPendingFileRenameOperation(dll);

        if (MoveFileExW(dll, bak, 0))
        {
            wsprintfW(logBuf, L"[MoveLockedDimeDll] renamed: %s -> %s", dll, bak);
            CaBurnLog(logBuf);
            wchar_t valName[16] = L"wix_bak_";
            valName[8] = L'0' + (wchar_t)wixIdx++;
            valName[9] = L'\0';
            WriteInstallerStateEntry(valName, dll, bak);

            // Copy bak back to the original dll path (fresh unlocked inode,
            // same bytes). Running processes keep their open handle to the
            // bak inode and are unaffected. RemoveExistingProducts can now
            // unregister and delete the dll at its normal path without
            // locking, so no reboot is needed. CleanupDimeDllBak (commit)
            // will mark the bak for deletion after install succeeds.
            if (CopyFileW(bak, dll, FALSE))
            {
                wsprintfW(logBuf, L"[MoveLockedDimeDll] copy-back OK: %s", dll);
            }
            else
            {
                wsprintfW(logBuf, L"[MoveLockedDimeDll] copy-back FAILED err=%u: %s",
                          GetLastError(), dll);
            }
            CaBurnLog(logBuf);
        }
        else
        {
            DWORD err = GetLastError();
            wsprintfW(logBuf, L"[MoveLockedDimeDll] MoveFileExW FAILED err=%u: %s", err, dll);
            CaBurnLog(logBuf);
            // Fallback: POSIX copy-replace (Win10 1809+).
            // When the opener withholds FILE_SHARE_DELETE, MoveFileExW returns
            // ERROR_SHARING_VIOLATION.  CopyAndPosixReplace copies the file to bak
            // then uses SetFileInformationByHandle(FileRenameInfoEx, POSIX|REPLACE)
            // to atomically replace the original directory entry with the fresh inode.
            // After success, dll at its original path is unlocked; InstallFiles can
            // overwrite it without PendingFileRenameOperations or a reboot.
            // No InstallerState entry is written: original content is at dll path;
            // MSI's own per-file rollback restores it if the install fails.
            if (err == ERROR_SHARING_VIOLATION)
            {
                if (CopyAndPosixReplace(dll, bak))
                {
                    wsprintfW(logBuf, L"[MoveLockedDimeDll] POSIX fallback OK: %s", dll);
                    CaBurnLog(logBuf);
                }
                else
                {
                    wsprintfW(logBuf,
                        L"[MoveLockedDimeDll] POSIX fallback failed (old OS or err): %s", dll);
                    CaBurnLog(logBuf);
                    // Old Windows: cannot replace a file held open without
                    // FILE_SHARE_DELETE.  InstallFiles will queue PFRO → 3010.
                }
            }
        }
    }

    // Extend the same scrub + rename + copy-back pattern to all .cin files in
    // INSTALLDIR.  INSTALLDIR is derived from the first DLL token by stripping
    // two path components (arch subdir + filename).  .cin files live directly
    // in INSTALLDIR, not in a subdirectory.
    if (n >= 2 && tokens[1] && tokens[1][0])
    {
        wchar_t installDir[MAX_PATH] = {};
        lstrcpynW(installDir, tokens[1], MAX_PATH);
        // Strip filename (DIME.dll)
        wchar_t* p = installDir + lstrlenW(installDir);
        while (p > installDir && *p != L'\\') --p;
        if (*p == L'\\') *p = L'\0';
        // Strip arch subdirectory (amd64 / arm64 / x86)
        p = installDir + lstrlenW(installDir);
        while (p > installDir && *p != L'\\') --p;
        if (*p == L'\\') *p = L'\0';

        if (installDir[0])
        {
            wchar_t cinPattern[MAX_PATH + 8] = {};
            lstrcpynW(cinPattern, installDir, MAX_PATH);
            lstrcatW(cinPattern, L"\\*.cin");

            WIN32_FIND_DATAW fd = {};
            HANDLE hFind = FindFirstFileW(cinPattern, &fd);
            if (hFind != INVALID_HANDLE_VALUE)
            {
                do
                {
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

                    wchar_t cinPath[MAX_PATH] = {};
                    lstrcpynW(cinPath, installDir, MAX_PATH);
                    lstrcatW(cinPath, L"\\");
                    lstrcatW(cinPath, fd.cFileName);

                    wchar_t cinBak[MAX_PATH + 64] = {};
                    BuildUniqueBakName(cinPath, ver, cinBak, MAX_PATH + 64);

                    ScrubPendingFileRenameOperation(cinPath);

                    if (MoveFileExW(cinPath, cinBak, 0))
                    {
                        wsprintfW(logBuf, L"[MoveLockedDimeDll] cin renamed: %s -> %s",
                                  cinPath, cinBak);
                        CaBurnLog(logBuf);

                        wchar_t valName[16] = L"cin_bak_";
                        valName[8] = L'0' + (wchar_t)cinIdx++;
                        valName[9] = L'\0';
                        WriteInstallerStateEntry(valName, cinPath, cinBak);

                        // Copy-back: fresh unlocked inode at original path.
                        // RemoveExistingProducts can now delete/overwrite the .cin
                        // without hitting a sharing violation or triggering reboot-pending.
                        if (CopyFileW(cinBak, cinPath, FALSE))
                            wsprintfW(logBuf, L"[MoveLockedDimeDll] cin copy-back OK: %s", cinPath);
                        else
                            wsprintfW(logBuf, L"[MoveLockedDimeDll] cin copy-back FAILED err=%u: %s",
                                      GetLastError(), cinPath);
                        CaBurnLog(logBuf);
                    }
                    else
                    {
                        DWORD err = GetLastError();
                        wsprintfW(logBuf, L"[MoveLockedDimeDll] cin MoveFileExW FAILED err=%u: %s",
                                  err, cinPath);
                        CaBurnLog(logBuf);
                        // Fallback: POSIX copy-replace (Win10 1809+).
                        // Same mechanism as for DLL: copy cinPath → cinBak then
                        // POSIX-rename cinBak → cinPath, bypassing FILE_SHARE_DELETE
                        // requirement on the target.  No InstallerState entry needed.
                        if (err == ERROR_SHARING_VIOLATION)
                        {
                            if (CopyAndPosixReplace(cinPath, cinBak))
                            {
                                wsprintfW(logBuf,
                                    L"[MoveLockedDimeDll] cin POSIX fallback OK: %s", cinPath);
                                CaBurnLog(logBuf);
                            }
                            else
                            {
                                wsprintfW(logBuf,
                                    L"[MoveLockedDimeDll] cin POSIX fallback failed (old OS or err): %s",
                                    cinPath);
                                CaBurnLog(logBuf);
                                // Old Windows: InstallFiles will queue PFRO → 3010.
                            }
                        }
                    }
                }
                while (FindNextFileW(hFind, &fd));
                FindClose(hFind);
            }
        }
    }

    return ERROR_SUCCESS;
}

// ============================================================================
// RollbackMoveLockedDimeDll  Execute="rollback"  Impersonate="no" (SYSTEM)
//
// Reads bak paths from InstallerState and renames each back to DIME.dll.
// Safe because CleanupDimeDllBak (commit) has not run yet -- baks are still
// plain files and can be renamed normally.
// Deletes InstallerState key when done.
// ============================================================================
extern "C" __declspec(dllexport)
UINT __stdcall RollbackMoveLockedDimeDll(MSIHANDLE /*hInstall*/)
{
    BakEntry entries[16] = {};
    int n = ReadAllInstallerState(entries, 16);

    for (int i = 0; i < n; ++i)
    {
        // Each InstallerState entry is "src|bak" (written by WriteInstallerStateEntry).
        // Use src as the rename target so any file type (.dll, .cin, ...) is
        // restored to its exact original path without hardcoding filenames.
        wchar_t srcPath[MAX_PATH] = {};
        wchar_t bakPath[MAX_PATH] = {};
        SplitStatePair(entries[i].path, srcPath, bakPath, MAX_PATH);

        if (!srcPath[0] || !bakPath[0]) continue;
        if (GetFileAttributesW(bakPath) == INVALID_FILE_ATTRIBUTES) continue;
        MoveFileExW(bakPath, srcPath, MOVEFILE_REPLACE_EXISTING);
    }

    DeleteInstallerState();
    return ERROR_SUCCESS;
}

// ============================================================================
// HeartbeatThread — background thread spawned by EarlyRenameForValidate.
//
// Sends INSTALLMESSAGE_PROGRESS every 500 ms for up to `durationMs`
// milliseconds.  This keeps the Burn bootstrapper's "not responding" timer
// from firing while InstallValidate blocks on the Restart Manager's 60-second
// WM_QUERYENDSESSION timeout (caused by sandboxed processes such as
// msedgewebview2 that hold DIME.dll open).  After `durationMs`, the thread
// exits on its own; no explicit cancellation is needed.
//
// NOTE: hInstall is the MSI session handle, which remains valid until
// InstallFinalize ends — well past the 90 s window we use here.
// ============================================================================
struct HeartbeatCtx { MSIHANDLE hInstall; DWORD durationMs; };

static DWORD WINAPI HeartbeatThread(LPVOID param)
{
    HeartbeatCtx* ctx = reinterpret_cast<HeartbeatCtx*>(param);
    MSIHANDLE     h   = ctx->hInstall;
    DWORD         end = GetTickCount() + ctx->durationMs;
    delete ctx;

    while (static_cast<long>(end - GetTickCount()) > 0)
    {
        Sleep(500);
        MSIHANDLE hRec = MsiCreateRecord(2);
        if (hRec)
        {
            MsiRecordSetInteger(hRec, 1, 2);  // field[1]=2: progress tick
            MsiRecordSetInteger(hRec, 2, 0);  // field[2]=0: add 0 ticks
            MsiProcessMessage(h, INSTALLMESSAGE_PROGRESS, hRec);
            MsiCloseHandle(hRec);
        }
    }
    return 0;
}

// ============================================================================
// EarlyRenameForValidate  Execute="immediate" (server-side, elevated)
//
// Scheduled Before="InstallValidate" in InstallExecuteSequence.
//
// Problem: InstallValidate's built-in file-in-use detection blocks for ~60 s
// when a sandboxed process (e.g. msedgewebview2) holds a handle to DIME.dll.
// This 60-second silence triggers Burn's "Installer 不再有回應" (package not
// responding) dialog.
//
// Fix: rename each DIME.dll to a unique bak name and copy it back at the
// original path (fresh unlocked inode).  InstallValidate then finds nothing
// locked, completing in milliseconds.  Subsequent Unregister CAs still run
// regsvr32 /u against the fresh (unlocked) dll path.
//
// Rnamed bak paths are recorded in HKLM\SOFTWARE\DIME\InstallerState as
// "early_bak_N" entries.  MoveLockedDimeDll (deferred, runs later) detects
// them, promotes each to "wix_bak_K", and skips re-renaming.
// RollbackMoveLockedDimeDll and CleanupDimeDllBak handle all InstallerState
// entries regardless of key prefix.
//
// Returns ERROR_SUCCESS always (never aborts).
// ============================================================================
extern "C" __declspec(dllexport)
UINT __stdcall EarlyRenameForValidate(MSIHANDLE hInstall)
{
    wchar_t remove[64] = {};
    DWORD cch = 64;
    MsiGetPropertyW(hInstall, L"REMOVE", remove, &cch);
    if (!remove[0])
    {
        CaBurnLog(L"[EarlyRenameForValidate] REMOVE not set — skipping");
        return ERROR_SUCCESS;
    }

    wchar_t upgradingCode[MAX_PATH] = {};
    cch = MAX_PATH;
    MsiGetPropertyW(hInstall, L"UPGRADINGPRODUCTCODE", upgradingCode, &cch);
    if (upgradingCode[0])
    {
        CaBurnLog(L"[EarlyRenameForValidate] UPGRADINGPRODUCTCODE set — skipping");
        return ERROR_SUCCESS;
    }

    // SetMoveLockedDimeDllUninstall (immediate SetProperty, Before="EarlyRenameForValidate")
    // populates this property.  Format: "{ProductVersion}|{dll1}|{dll2}|..."
    wchar_t data[4096] = {};
    cch = 4096;
    if (MsiGetPropertyW(hInstall, L"MoveLockedDimeDllUninstall", data, &cch) != ERROR_SUCCESS
        || !data[0])
    {
        CaBurnLog(L"[EarlyRenameForValidate] MoveLockedDimeDllUninstall property empty — skipping");
        return ERROR_SUCCESS;
    }

    wchar_t* tokens[17] = {};
    int n = SplitPipe(data, tokens, 17);
    if (n < 2)
    {
        CaBurnLog(L"[EarlyRenameForValidate] too few tokens — skipping");
        return ERROR_SUCCESS;
    }

    const wchar_t* ver = tokens[0];
    wchar_t logBuf[512] = {};
    wsprintfW(logBuf, L"[EarlyRenameForValidate] REMOVE=%s ver=%s dlls=%d",
              remove, ver, n - 1);
    CaBurnLog(logBuf);

    int earlyIdx = 0;
    for (int i = 1; i < n; ++i)
    {
        const wchar_t* dll = tokens[i];
        if (!dll || !dll[0]) continue;

        if (GetFileAttributesW(dll) == INVALID_FILE_ATTRIBUTES)
        {
            wsprintfW(logBuf, L"[EarlyRenameForValidate] not found (ok): %s", dll);
            CaBurnLog(logBuf);
            continue;
        }

        wchar_t bak[MAX_PATH + 64] = {};
        BuildUniqueBakName(dll, ver, bak, MAX_PATH + 64);

        ScrubPendingFileRenameOperation(dll);

        if (MoveFileExW(dll, bak, 0))
        {
            // Fresh unlocked inode at original path so:
            //   InstallValidate → no lock found → fast
            //   Unregister CAs → regsvr32 /u still succeeds on the fresh dll
            CopyFileW(bak, dll, FALSE);

            wchar_t valName[16] = L"early_bak_";
            valName[10] = L'0' + (wchar_t)earlyIdx++;
            valName[11] = L'\0';
            WriteInstallerStateEntry(valName, dll, bak);

            wsprintfW(logBuf, L"[EarlyRenameForValidate] renamed: %s -> %s", dll, bak);
            CaBurnLog(logBuf);
        }
        else
        {
            DWORD err = GetLastError();
            wsprintfW(logBuf, L"[EarlyRenameForValidate] MoveFileExW err=%u: %s", err, dll);
            CaBurnLog(logBuf);
            if (err == ERROR_SHARING_VIOLATION)
            {
                // POSIX copy-replace: atomically replaces the dir entry with a
                // fresh inode (Win10 1809+).  No InstallerState entry needed
                // because the fresh inode at the original path is unlocked.
                if (CopyAndPosixReplace(dll, bak))
                {
                    wsprintfW(logBuf, L"[EarlyRenameForValidate] POSIX fallback OK: %s", dll);
                    CaBurnLog(logBuf);
                }
                else
                {
                    wsprintfW(logBuf, L"[EarlyRenameForValidate] POSIX fallback failed: %s", dll);
                    CaBurnLog(logBuf);
                }
            }
        }
    }

    // Scrub stale PFRO entries for .cin files and .dll bak files.
    // These can be left by earlier failed install cycles and cause
    // InstallInitialize to set MsiSystemRebootPending=1 even after the DLL
    // rename+copy-back above gives us fresh unlocked inodes.
    // EarlyRenameForValidate runs Before="InstallValidate" i.e. before
    // InstallInitialize, so scrubbing here prevents the reboot-pending flag
    // from being set in the first place.
    if (n >= 2 && tokens[1] && tokens[1][0])
    {
        // Derive installDir: strip filename (DIME.dll) and arch subdir
        wchar_t installDir[MAX_PATH] = {};
        lstrcpynW(installDir, tokens[1], MAX_PATH);
        wchar_t* p = installDir + lstrlenW(installDir);
        while (p > installDir && *p != L'\\') --p;
        if (*p == L'\\') *p = L'\0';
        p = installDir + lstrlenW(installDir);
        while (p > installDir && *p != L'\\') --p;
        if (*p == L'\\') *p = L'\0';

        if (installDir[0])
        {
            // Scrub PFRO for all *.cin files in installDir
            wchar_t cinPattern[MAX_PATH + 8] = {};
            lstrcpynW(cinPattern, installDir, MAX_PATH);
            lstrcatW(cinPattern, L"\\*.cin");
            WIN32_FIND_DATAW fd = {};
            HANDLE hFind = FindFirstFileW(cinPattern, &fd);
            if (hFind != INVALID_HANDLE_VALUE)
            {
                do {
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                    wchar_t cinPath[MAX_PATH] = {};
                    lstrcpynW(cinPath, installDir, MAX_PATH);
                    lstrcatW(cinPath, L"\\");
                    lstrcatW(cinPath, fd.cFileName);
                    ScrubPendingFileRenameOperation(cinPath);
                } while (FindNextFileW(hFind, &fd));
                FindClose(hFind);
            }

            // Scrub PFRO for DLL bak files in each arch subdir
            const wchar_t* archDirs[] = { L"amd64", L"arm64", L"x86" };
            for (int ai = 0; ai < 3; ++ai)
            {
                wchar_t bakPattern[MAX_PATH + 16] = {};
                lstrcpynW(bakPattern, installDir, MAX_PATH);
                lstrcatW(bakPattern, L"\\");
                lstrcatW(bakPattern, archDirs[ai]);
                lstrcatW(bakPattern, L"\\DIME.dll.*");
                hFind = FindFirstFileW(bakPattern, &fd);
                if (hFind == INVALID_HANDLE_VALUE) continue;
                do {
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                    if (lstrcmpiW(fd.cFileName, L"DIME.dll") == 0) continue;
                    wchar_t bakPath[MAX_PATH] = {};
                    lstrcpynW(bakPath, installDir, MAX_PATH);
                    lstrcatW(bakPath, L"\\");
                    lstrcatW(bakPath, archDirs[ai]);
                    lstrcatW(bakPath, L"\\");
                    lstrcatW(bakPath, fd.cFileName);
                    ScrubPendingFileRenameOperation(bakPath);
                } while (FindNextFileW(hFind, &fd));
                FindClose(hFind);
            }

            wsprintfW(logBuf, L"[EarlyRenameForValidate] PFRO scrub done for %s", installDir);
            CaBurnLog(logBuf);
        }
    }

    // Spawn a background heartbeat thread so Burn's "not responding" timer does
    // not fire during InstallValidate's Restart Manager wait.  The RM API can
    // block up to ~60 s per file set while waiting for WM_QUERYENDSESSION
    // responses from sandboxed processes (msedgewebview2).  Our thread sends
    // INSTALLMESSAGE_PROGRESS every 500 ms for 90 s, which is longer than the
    // worst-case RM timeout and harmlessly overlaps with UnregisterDimeDll
    // (which also heartbeats).  The thread exits automatically — no join needed.
    {
        HeartbeatCtx* ctx = new HeartbeatCtx{ hInstall, 90000u };
        HANDLE hThread = CreateThread(nullptr, 0, HeartbeatThread, ctx, 0, nullptr);
        if (hThread) { CloseHandle(hThread); }
        else         { delete ctx; }  // CreateThread failed; proceed without heartbeat
        CaBurnLog(L"[EarlyRenameForValidate] heartbeat thread spawned for 90 s");
    }

    wsprintfW(logBuf, L"[EarlyRenameForValidate] done, early baks written: %d", earlyIdx);
    CaBurnLog(logBuf);
    return ERROR_SUCCESS;
}

// ============================================================================
// CleanupDimeDllBak  Execute="commit"  Impersonate="no" (SYSTEM)
//
// Marks each bak in InstallerState for deletion using the 4-step cascade:
//   1. DeleteFileW                    -- immediate if no process holds it
//   2. FileDispositionInfoEx+POSIX    -- Win10 1709+: dir entry gone instantly
//   3. FileDispositionInfo pending    -- older OS: auto-delete on last close
//   4. MOVEFILE_DELAY_UNTIL_REBOOT   -- last resort
//
// Also glob-scans known DLL directories for orphan baks from prior
// interrupted install cycles, then deletes InstallerState key.
// ============================================================================

static void CleanupBaksInDir(const wchar_t* dir)
{
    if (!dir || !dir[0]) return;

    wchar_t pattern[MAX_PATH + 16] = {};
    lstrcpynW(pattern, dir, MAX_PATH);
    lstrcatW(pattern, L"\\DIME.dll.*");

    WIN32_FIND_DATAW fd = {};
    HANDLE hFind = FindFirstFileW(pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do
    {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (lstrcmpiW(fd.cFileName, L"DIME.dll") == 0)
        {
            wchar_t logBuf[MAX_PATH + 64] = {};
            wsprintfW(logBuf, L"[CleanupBaksInDir] skipping live DLL: %s\\%s", dir, fd.cFileName);
            CaBurnLog(logBuf);
            continue;
        }
        wchar_t full[MAX_PATH + 260] = {};
        lstrcpynW(full, dir, MAX_PATH);
        lstrcatW(full, L"\\");
        lstrcatW(full, fd.cFileName);
        CaBurnLog(full);  // log what we're about to delete
        MarkFileForDeletion(full);
    }
    while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
}

// Glob-scan `dir` for orphan .cin bak files (pattern "*.cin.*") left by
// prior interrupted install cycles and mark them for deletion.
// WARNING: Win32 FindFirstFile trailing-dot quirk — "*.cin.*" also matches
// plain "Array.cin" (same as "DIME.dll.*" matches "DIME.dll").  We must
// explicitly skip any filename that ends exactly with ".cin" (no suffix).
static void CleanupCinBaksInDir(const wchar_t* dir)
{
    if (!dir || !dir[0]) return;

    wchar_t pattern[MAX_PATH + 16] = {};
    lstrcpynW(pattern, dir, MAX_PATH);
    lstrcatW(pattern, L"\\*.cin.*");

    WIN32_FIND_DATAW fd = {};
    HANDLE hFind = FindFirstFileW(pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do
    {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

        // Skip live .cin files — only delete bak files that have a suffix
        // after ".cin" (e.g. "Array.cin.1.3.0.0.ABCD1234ABCD1234").
        // wcsstr finds ".cin." anywhere in the name; if absent the file ends
        // exactly with ".cin" and is a live input-method table, not a bak.
        if (!wcsstr(fd.cFileName, L".cin.")) continue;

        wchar_t full[MAX_PATH + 260] = {};
        lstrcpynW(full, dir, MAX_PATH);
        lstrcatW(full, L"\\");
        lstrcatW(full, fd.cFileName);
        CaBurnLog(full);
        MarkFileForDeletion(full);
    }
    while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
}

// ============================================================================
// CleanupBurnRegistration  Execute="commit"  Impersonate="no" (SYSTEM)
//
// When the user uninstalls DIME via the MSI's ARP entry (bypassing the Burn
// bundle), Burn's own registration entry is left behind in the registry and
// Package Cache.  This CA locates that entry by searching for a key whose
// "BundleUpgradeCode" value contains the known bundle UpgradeCode GUID,
// deletes it, and removes the corresponding Package Cache folder.
//
// Key facts learned from registry inspection:
//   - Burn registers under SOFTWARE\WOW6432Node\...\Uninstall (32-bit view),
//     even on a 64-bit OS, because the Burn engine is a 32-bit process.
//   - BundleUpgradeCode is stored as "{{GUID}}" (double-braces, list format).
//     We use wcsstr() to find the bare GUID within the value.
//   - WiX v6 does not support a fixed Bundle Id/ProductCode — the GUID is
//     always content-hashed per build.  So we enumerate and match by
//     BundleUpgradeCode, deleting ALL matching keys (handles stale entries
//     left by prior builds/upgrades).
//
// Runs only on full removal (REMOVE="ALL"), skipped during major-upgrade
// product-removal (UPGRADINGPRODUCTCODE).
// ============================================================================
// Simple log helper — writes to both C:\Windows\Temp\ (SYSTEM-writable) and
// C:\Users\Public\ (world-readable) so the log is accessible without elevation.
static void CaBurnLog(const wchar_t* line)
{
    const wchar_t* paths[] = {
        L"C:\\Windows\\Temp\\DIMEBurnCA.txt",
        L"C:\\Users\\Public\\DIMEBurnCA.txt"
    };
    for (int i = 0; i < 2; ++i)
    {
        HANDLE hFile = CreateFileW(paths[i],
            FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) continue;
        char buf[512] = {};
        int n = WideCharToMultiByte(CP_UTF8, 0, line, -1, buf, 510, nullptr, nullptr);
        if (n > 1) { buf[n - 1] = '\n'; WriteFile(hFile, buf, n, nullptr, nullptr); }
        CloseHandle(hFile);
    }
}

// Recursively delete a directory and all its contents.
static void DeleteDirectoryRecursive(const wchar_t* path)
{
    wchar_t search[MAX_PATH + 4] = {};
    lstrcpynW(search, path, MAX_PATH);
    lstrcatW(search, L"\\*");

    WIN32_FIND_DATAW fd = {};
    HANDLE hFind = FindFirstFileW(search, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do
    {
        if (lstrcmpW(fd.cFileName, L".") == 0 || lstrcmpW(fd.cFileName, L"..") == 0)
            continue;

        wchar_t child[MAX_PATH + 2] = {};
        lstrcpynW(child, path, MAX_PATH);
        lstrcatW(child, L"\\");
        lstrcatW(child, fd.cFileName);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            DeleteDirectoryRecursive(child);
            RemoveDirectoryW(child);
        }
        else
        {
            SetFileAttributesW(child, FILE_ATTRIBUTE_NORMAL);
            DeleteFileW(child);
        }
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
    RemoveDirectoryW(path);
}

// Shared implementation: enumerate 32-bit Uninstall hive and delete all DIME
// Burn ARP entries matched by fingerprint (BundleVersion present +
// Publisher == "Jeremy Wu" + DisplayName contains "DIME").
// When deleteFolder is true, also removes the Package Cache folder for each
// matched entry.  Called by both exported CA entry points below.
// hInstall: MSI session handle; used to read BURN_BUNDLE_ID so the current
// bundle's own ARP key is skipped (only orphans from prior builds are deleted).
static void CleanupBurnARPImpl(MSIHANDLE hInstall, bool deleteFolder)
{
    // Read the current bundle GUID so we can skip its ARP key.
    // Bundle.wxs passes [WixBundleProviderKey] as the BURN_BUNDLE_ID property.
    wchar_t burnBundleId[64] = {};
    if (hInstall)
    {
        DWORD cch = ARRAYSIZE(burnBundleId);
        MsiGetPropertyW(hInstall, L"BURN_BUNDLE_ID", burnBundleId, &cch);
        if (burnBundleId[0])
        {
            wchar_t msg[128] = {};
            wsprintfW(msg, L"current bundle GUID (will skip): %s", burnBundleId);
            CaBurnLog(msg);
        }
    }

    static const wchar_t kUninstallBase[] =
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall";

    HKEY hUninstall = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kUninstallBase, 0,
                      KEY_READ | KEY_WRITE | KEY_WOW64_32KEY, &hUninstall) != ERROR_SUCCESS)
    {
        CaBurnLog(L"ERROR: failed to open Uninstall hive");
        return;
    }

    bool anyDeleted = false;
    for (DWORD idx = 0; ; )
    {
        wchar_t subName[64] = {};
        DWORD cchSub = 64;
        LONG r = RegEnumKeyExW(hUninstall, idx, subName, &cchSub,
                               nullptr, nullptr, nullptr, nullptr);
        if (r == ERROR_NO_MORE_ITEMS) break;
        if (r != ERROR_SUCCESS) { ++idx; continue; }

        HKEY hSub = nullptr;
        if (RegOpenKeyExW(hUninstall, subName, 0,
                          KEY_READ | KEY_WOW64_32KEY, &hSub) != ERROR_SUCCESS)
            { ++idx; continue; }

        // Must have BundleVersion — Burn-specific value absent from MSI entries.
        wchar_t bundleVer[64] = {};
        DWORD cb = sizeof(bundleVer);
        bool isBurn = (RegGetValueW(hSub, nullptr, L"BundleVersion",
                                    RRF_RT_REG_SZ, nullptr, bundleVer, &cb) == ERROR_SUCCESS
                       && bundleVer[0]);

        // Publisher must be "Jeremy Wu".
        wchar_t publisher[128] = {};
        cb = sizeof(publisher);
        bool rightPublisher = isBurn &&
            (RegGetValueW(hSub, nullptr, L"Publisher",
                          RRF_RT_REG_SZ, nullptr, publisher, &cb) == ERROR_SUCCESS)
            && (_wcsicmp(publisher, L"Jeremy Wu") == 0);

        // DisplayName must contain "DIME".
        wchar_t dispName[256] = {};
        cb = sizeof(dispName);
        bool rightName = rightPublisher &&
            (RegGetValueW(hSub, nullptr, L"DisplayName",
                          RRF_RT_REG_SZ, nullptr, dispName, &cb) == ERROR_SUCCESS)
            && (wcsstr(dispName, L"DIME") != nullptr);

        if (!rightName)
        {
            RegCloseKey(hSub);
            ++idx;
            continue;
        }

        // Skip the currently-running bundle so Burn can manage its own
        // registration.  Only orphans from prior builds are removed.
        if (burnBundleId[0] && _wcsicmp(subName, burnBundleId) == 0)
        {
            CaBurnLog(L"skipping current bundle ARP key:");
            CaBurnLog(subName);
            RegCloseKey(hSub);
            ++idx;
            continue;
        }

        // Matched — read BundleCachePath before deleting the key.
        wchar_t cachePath[MAX_PATH] = {};
        cb = sizeof(cachePath);
        RegGetValueW(hSub, nullptr, L"BundleCachePath",
                     RRF_RT_REG_SZ, nullptr, cachePath, &cb);
        if (cachePath[0])
        {
            wchar_t* lastSlash = wcsrchr(cachePath, L'\\');
            if (lastSlash) *lastSlash = L'\0';
        }

        RegCloseKey(hSub);

        // Delete ARP registry tree (key + \variables subkey).
        CaBurnLog(L"deleting ARP tree:");
        CaBurnLog(subName);
        LONG rd = RegDeleteTreeW(hUninstall, subName);
        if (rd == ERROR_SUCCESS)
        {
            CaBurnLog(L"ARP tree deleted OK");
            anyDeleted = true;
            // Do NOT increment idx — deleting shifts all subsequent indices.
        }
        else
        {
            CaBurnLog(L"ERROR: RegDeleteTreeW failed");
            ++idx;
        }

        // Commit phase only (SYSTEM context has write access to HKLM\SOFTWARE\Classes).
        // After deleting the orphan bundle's ARP entry, also remove its Dependents
        // subkey from every package provider under HKLM\SOFTWARE\Classes\Installer\Dependencies.
        // Without this, the orphan bundle GUID lingers as a phantom dependent that blocks
        // subsequent Burn uninstalls: Burn sees the phantom and refuses to uninstall the MSI.
        if (deleteFolder)
        {
            HKEY hDeps = nullptr;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                              L"SOFTWARE\\Classes\\Installer\\Dependencies",
                              0, KEY_READ | KEY_WRITE, &hDeps) == ERROR_SUCCESS)
            {
                wchar_t provName[256] = {};
                DWORD   cchProv       = 256;
                for (DWORD pidx = 0; ; )
                {
                    cchProv = 256;
                    LONG rp = RegEnumKeyExW(hDeps, pidx, provName, &cchProv,
                                            nullptr, nullptr, nullptr, nullptr);
                    if (rp == ERROR_NO_MORE_ITEMS) break;
                    if (rp != ERROR_SUCCESS) { ++pidx; continue; }

                    // Try to delete Dependents\{orphanBundleGuid} under this provider.
                    wchar_t deptsPath[320] = {};
                    lstrcpynW(deptsPath, provName, 256);
                    lstrcatW(deptsPath, L"\\Dependents");
                    HKEY hDepts = nullptr;
                    if (RegOpenKeyExW(hDeps, deptsPath, 0,
                                      KEY_READ | KEY_WRITE, &hDepts) == ERROR_SUCCESS)
                    {
                        if (RegDeleteKeyW(hDepts, subName) == ERROR_SUCCESS)
                        {
                            CaBurnLog(L"removed orphan dependent from provider:");
                            CaBurnLog(provName);
                        }
                        RegCloseKey(hDepts);
                    }
                    ++pidx;
                }
                RegCloseKey(hDeps);
            }
        }

        // UISequence call (deleteFolder=false): save the cache path to the side-channel
        // key so the commit-phase call can delete the Package Cache folder even though
        // this call already deleted the ARP registry key.
        if (!deleteFolder && cachePath[0])
        {
            HKEY hSave = nullptr;
            if (RegCreateKeyExW(HKEY_LOCAL_MACHINE,
                                L"SOFTWARE\\DIME\\BurnCacheCleanup",
                                0, nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE,
                                nullptr, &hSave, nullptr) == ERROR_SUCCESS)
            {
                RegSetValueExW(hSave, L"CachePath", 0, REG_SZ,
                               reinterpret_cast<const BYTE*>(cachePath),
                               (lstrlenW(cachePath) + 1) * sizeof(wchar_t));
                RegCloseKey(hSave);
            }
        }

        // Commit phase (deleteFolder=true): delete the folder now.
        if (deleteFolder && cachePath[0])
        {
            CaBurnLog(L"deleting Package Cache folder:");
            CaBurnLog(cachePath);
            DeleteDirectoryRecursive(cachePath);
            CaBurnLog(L"Package Cache folder deleted");
        }
    }

    // --- Reverse scan: remove stale orphan Dependents from this MSI's provider key ---
    // Problem: when a bundle is reinstalled/upgraded, the old bundle's GUID remains
    // registered as a Dependent under the DIME package provider.  Burn will refuse
    // to uninstall the MSI if it sees any external Dependent GUID besides its own,
    // resulting in error 2803.  We cannot rely on the ARP check because at commit
    // time the old bundle's ARP entry is still present (the old Burn session hasn't
    // ended yet).  Instead, we use the current bundle GUID saved by CleanupBurnARPEarly
    // (UISequence, before commit) in the side-channel key.  Any Dependent GUID that
    // is not the current bundle GUID is stale and is removed unconditionally.
    //
    // The provider key name is exactly the MSI ProductCode, passed in via
    // CustomActionData (commit CAs cannot read arbitrary session properties —
    // only CustomActionData is reliably available in that context).  We target that
    // single key directly — no prefix-guessing needed (prefix patterns change with
    // every version build and become stale).
    if (deleteFolder)
    {
        // Read the current bundle GUID from the side-channel key written by
        // CleanupBurnARPEarly (UISequence path, where BURN_BUNDLE_ID is accessible).
        wchar_t currentBundleId[64] = {};
        {
            DWORD cbId = sizeof(currentBundleId);
            RegGetValueW(HKEY_LOCAL_MACHINE,
                         L"SOFTWARE\\DIME\\BurnCacheCleanup", L"CurrentBundleId",
                         RRF_RT_REG_SZ, nullptr, currentBundleId, &cbId);
        }
        // Also check burnBundleId (readable in commit context on some MSI versions).
        // When neither is known (MSI-direct install, no live Burn session), there is
        // no current bundle to protect — remove all Dependents (they are all orphans).
        bool haveCurrentId = currentBundleId[0] || burnBundleId[0];

        // Read this MSI's ProductCode from CustomActionData — commit CAs cannot read
        // arbitrary session properties via MsiGetPropertyW; only CustomActionData
        // is reliably available.  The setter CA SetCleanupBurnRegistrationData
        // (an immediate CA in InstallExecuteSequence) passes [ProductCode] here.
        wchar_t productCode[64] = {};
        {
            DWORD cch = ARRAYSIZE(productCode);
            MsiGetPropertyW(hInstall, L"CustomActionData", productCode, &cch);
        }

        {
            wchar_t dbg[256] = {};
            wsprintfW(dbg, L"DepsKeyScan: productCode=%s currentBundleId=%s haveCurrentId=%d",
                      productCode[0] ? productCode : L"(empty)",
                      currentBundleId[0] ? currentBundleId : L"(empty)",
                      (int)haveCurrentId);
            CaBurnLog(dbg);
        }

        if (productCode[0])
        {
            // Build path: SOFTWARE\Classes\Installer\Dependencies\{ProductCode}\Dependents
            wchar_t deptsFullPath[320] = {};
            lstrcpynW(deptsFullPath,
                      L"SOFTWARE\\Classes\\Installer\\Dependencies\\",
                      ARRAYSIZE(deptsFullPath));
            lstrcatW(deptsFullPath, productCode);
            lstrcatW(deptsFullPath, L"\\Dependents");

            HKEY hDepts = nullptr;
            LONG openErr = RegOpenKeyExW(HKEY_LOCAL_MACHINE, deptsFullPath, 0,
                                         KEY_READ | KEY_WRITE, &hDepts);
            if (openErr != ERROR_SUCCESS)
            {
                wchar_t dbg[320] = {};
                wsprintfW(dbg, L"DepsKeyScan: RegOpenKeyEx failed err=%ld path=%s",
                          openErr, deptsFullPath);
                CaBurnLog(dbg);
            }
            else
            {
                for (DWORD didx = 0; ; )
                {
                    wchar_t depGuid[64] = {};
                    DWORD cchDep = 64;
                    if (RegEnumKeyExW(hDepts, didx, depGuid, &cchDep,
                                      nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
                        break;

                    // Keep the current bundle's own registration.
                    // When haveCurrentId is false (MSI-direct install, no live bundle),
                    // treat every entry as an orphan — there is nothing to protect.
                    bool isCurrent = haveCurrentId &&
                        ((currentBundleId[0] && _wcsicmp(depGuid, currentBundleId) == 0) ||
                         (burnBundleId[0]    && _wcsicmp(depGuid, burnBundleId)    == 0));
                    if (isCurrent) { ++didx; continue; }

                    // This GUID is not the current bundle — it's a stale orphan.
                    // Remove it regardless of ARP presence: at commit time the old
                    // bundle's ARP entry still exists (old Burn session not yet ended),
                    // so an ARP check would incorrectly skip still-live-looking orphans.
                    if (RegDeleteKeyW(hDepts, depGuid) == ERROR_SUCCESS)
                    {
                        wchar_t logMsg[320] = {};
                        wsprintfW(logMsg,
                            L"removed stale orphan dependent %s from provider %s",
                            depGuid, productCode);
                        CaBurnLog(logMsg);
                        // Do NOT increment didx — deletion shifts enumeration index.
                    }
                    else
                    {
                        ++didx;
                    }
                }
                RegCloseKey(hDepts);
            }
        }
    }

    // Always save the current bundle GUID for the commit CA's reverse scan.
    // This runs unconditionally (not just when a matching ARP entry was found)
    // so the commit CA knows which GUID is live even on fresh installs with no
    // orphan ARP entries to delete.  Written to HKLM only in UISequence context
    // (deleteFolder=false) where BURN_BUNDLE_ID is accessible via MsiGetPropertyW.
    if (!deleteFolder && burnBundleId[0])
    {
        HKEY hSave = nullptr;
        if (RegCreateKeyExW(HKEY_LOCAL_MACHINE,
                            L"SOFTWARE\\DIME\\BurnCacheCleanup",
                            0, nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE,
                            nullptr, &hSave, nullptr) == ERROR_SUCCESS)
        {
            RegSetValueExW(hSave, L"CurrentBundleId", 0, REG_SZ,
                           reinterpret_cast<const BYTE*>(burnBundleId),
                           (lstrlenW(burnBundleId) + 1) * sizeof(wchar_t));
            RegCloseKey(hSave);
        }
    }

    RegCloseKey(hUninstall);

    if (!anyDeleted)
        CaBurnLog(L"no matching entries found (already clean)");
}
//
// Fires unconditionally at the very start of every MSI invocation — even if
// the user immediately cancels the maintenance dialog — because InstallUISequence
// always runs.  Deletes registry only (not Package Cache folder) because Burn
// may still have the folder open while the MSI is running.
// ============================================================================
extern "C" __declspec(dllexport)
UINT __stdcall CleanupBurnARPEarly(MSIHANDLE hInstall)
{
    CaBurnLog(L"[CleanupBurnARPEarly] CA started");
    CleanupBurnARPImpl(hInstall, false /* registry only */);
    CaBurnLog(L"[CleanupBurnARPEarly] CA complete");
    return ERROR_SUCCESS;
}

// ============================================================================
// CleanupBurnRegistration  Execute="commit"  Impersonate="no" (SYSTEM)
//
// Fires after a successful install commit.  Deletes both the registry tree
// and the Package Cache folder for every matched DIME Burn entry.
// Burn re-registers its ARP at Apply-end (after commit), so Burn's current-run
// entry will be re-created with SystemComponent=1 (hidden from Programs &
// Features) — that is expected and harmless.  Orphans from any prior builds
// are permanently gone.
// ============================================================================
extern "C" __declspec(dllexport)
UINT __stdcall CleanupBurnRegistration(MSIHANDLE hInstall)
{
    CaBurnLog(L"[CleanupBurnRegistration] CA started");
    CleanupBurnARPImpl(hInstall, true /* registry + folder */);

    // CleanupBurnARPEarly (UISequence) may have already deleted the ARP key,
    // leaving no entry for CleanupBurnARPImpl(true) to match and no folder to
    // delete.  Read the Package Cache path saved to the side-channel key and
    // delete the folder from here.
    {
        static const wchar_t kBurnCache[] = L"SOFTWARE\\DIME\\BurnCacheCleanup";
        wchar_t savedCache[MAX_PATH] = {};
        DWORD cb = sizeof(savedCache);
        if (RegGetValueW(HKEY_LOCAL_MACHINE, kBurnCache, L"CachePath",
                         RRF_RT_REG_SZ, nullptr, savedCache, &cb) == ERROR_SUCCESS
            && savedCache[0])
        {
            CaBurnLog(L"[CleanupBurnRegistration] deleting Package Cache via side-channel:");
            CaBurnLog(savedCache);
            DeleteDirectoryRecursive(savedCache);
            CaBurnLog(L"[CleanupBurnRegistration] Package Cache deleted");
        }
        RegDeleteKeyW(HKEY_LOCAL_MACHINE, kBurnCache);
    }

    // Delete the now-empty SOFTWARE\DIME parent key — but only when this is an
    // uninstall.  On uninstall, the MSI engine removes InstalledVersion in the
    // execute phase (before commit CAs run), so the value is absent by now.
    // On install, InstalledVersion is still present here.
    // We distinguish the two by peeking at InstalledVersion rather than reading
    // REMOVE: commit CAs cannot read arbitrary session properties via
    // MsiGetPropertyW — only CustomActionData is reliably available.
    {
        wchar_t ver[64] = {};
        DWORD cb = sizeof(ver);
        bool isUninstall = (RegGetValueW(HKEY_LOCAL_MACHINE,
                                         L"SOFTWARE\\DIME", L"InstalledVersion",
                                         RRF_RT_REG_SZ, nullptr, ver, &cb) != ERROR_SUCCESS
                            || !ver[0]);
        if (isUninstall)
            RegDeleteKeyW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\DIME");
    }

    CaBurnLog(L"[CleanupBurnRegistration] CA complete");
    return ERROR_SUCCESS;
}

// ============================================================================
// ConfirmDowngrade  Execute="immediate"  Impersonate="yes" (user)
//
// Fires whenever WIX_UPGRADE_DETECTED is set, which WiX v6 MajorUpgrade sets
// for ALL version replacements -- both upgrades and downgrades (AllowDowngrades="yes"
// means no separate WIX_DOWNGRADE_DETECTED row is generated).
//
// Compares ProductVersion (this installer) against the installed version to
// determine direction, then shows an appropriate bilingual Yes/No messagebox.
//
// UILevel <= 2 (quiet/silent): allow upgrades silently; block downgrades.
// UILevel >= 3 (any interactive UI): always ask.
//
// Returns ERROR_SUCCESS           => MSI engine proceeds.
// Returns ERROR_INSTALL_USEREXIT  => MSI cancels cleanly (exit code 1602).
// ============================================================================

// Parse "major.minor.build.rev" into a comparable ULONGLONG (16 bits per field).
static ULONGLONG ParseVersionStr(const wchar_t* s)
{
    ULONGLONG p[4] = {};
    int f = 0;
    for (; *s && f < 4; ++s)
    {
        if      (*s == L'.')                   ++f;
        else if (*s >= L'0' && *s <= L'9') p[f] = p[f] * 10 + (*s - L'0');
    }
    return (p[0] << 48) | (p[1] << 32) | (p[2] << 16) | p[3];
}

extern "C" __declspec(dllexport)
UINT __stdcall ConfirmDowngrade(MSIHANDLE hInstall)
{
    wchar_t uiLevelStr[8] = {};
    DWORD cch = 8;
    MsiGetPropertyW(hInstall, L"UILevel", uiLevelStr, &cch);
    DWORD uiLevel = (uiLevelStr[0] >= L'2' && uiLevelStr[0] <= L'9')
                    ? (DWORD)(uiLevelStr[0] - L'0') : 5;

    wchar_t logBuf[256] = {};
    wsprintfW(logBuf, L"[ConfirmDowngrade] CA started UILevel=%u", uiLevel);
    CaBurnLog(logBuf);

    // WIX_UPGRADE_DETECTED holds the ProductCode of the installed product
    // being replaced (set for both upgrades and downgrades by WiX v6).
    wchar_t upgradePC[64] = {};
    cch = 64;
    MsiGetPropertyW(hInstall, L"WIX_UPGRADE_DETECTED", upgradePC, &cch);
    wsprintfW(logBuf, L"[ConfirmDowngrade] WIX_UPGRADE_DETECTED=%s", upgradePC[0] ? upgradePC : L"(empty)");
    CaBurnLog(logBuf);

    // No existing product detected — fresh install, proceed silently.
    if (!upgradePC[0])
    {
        CaBurnLog(L"[ConfirmDowngrade] fresh install — returning SUCCESS");
        return ERROR_SUCCESS;
    }

    // This installer's version (the one being installed now).
    wchar_t installingVer[64] = L"?";
    cch = 64;
    MsiGetPropertyW(hInstall, L"ProductVersion", installingVer, &cch);

    // Look up the installed version via the ProductCode.
    wchar_t installedVer[64] = L"?";
    DWORD cbV = 64;
    MsiGetProductInfoW(upgradePC, INSTALLPROPERTY_VERSIONSTRING, installedVer, &cbV);

    bool isDowngrade = ParseVersionStr(installingVer) < ParseVersionStr(installedVer);

    wsprintfW(logBuf, L"[ConfirmDowngrade] installed=%s installing=%s isDowngrade=%d",
              installedVer, installingVer, (int)isDowngrade);
    CaBurnLog(logBuf);

    // Upgrades need no confirmation — allow silently.
    if (!isDowngrade)
    {
        CaBurnLog(L"[ConfirmDowngrade] upgrade — returning SUCCESS");
        return ERROR_SUCCESS;
    }

    // Downgrade: force file overwrite regardless of version.
    // REINSTALLMODE=amus is a public (all-caps) property that transfers from the
    // client UISequence to the elevated server execute sequence.  CostFinalize
    // uses it to overwrite higher-versioned on-disk files with the older ones
    // being installed, which is the root cause of the "DLLs missing" bug.
    MsiSetPropertyW(hInstall, L"REINSTALLMODE", L"amus");
    CaBurnLog(L"[ConfirmDowngrade] set REINSTALLMODE=amus for downgrade");

    // Silent downgrade: block, unless DIME_DOWNGRADE_APPROVED is set.
    // DIME_DOWNGRADE_APPROVED=1 on the command line lets automated tests exercise
    // the approved-downgrade path without showing a dialog.
    if (uiLevel <= 2)
    {
        wchar_t approved[8] = {};
        DWORD cchA = 8;
        MsiGetPropertyW(hInstall, L"DIME_DOWNGRADE_APPROVED", approved, &cchA);
        if (approved[0])
        {
            CaBurnLog(L"[ConfirmDowngrade] silent downgrade — DIME_DOWNGRADE_APPROVED set, proceeding");
            return ERROR_SUCCESS;
        }
        CaBurnLog(L"[ConfirmDowngrade] silent downgrade — blocking");
        return ERROR_INSTALL_USEREXIT;
    }

    // Interactive downgrade: ask user.
    wchar_t msg[512] = {};
    wsprintfW(msg,
        L"目前已安裝 DIME %s\n"
        L"此安裝程式版本為 DIME %s（較舊）\n\n"
        L"降版將移除目前版本並安裝舊版。\n"
        L"確定要繼續降版？",
        installedVer, installingVer);

    CaBurnLog(L"[ConfirmDowngrade] showing messagebox");
    int result = MessageBoxW(
        nullptr, msg, L"DIME 輸入法 — 確認降版",
        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2 | MB_TOPMOST | MB_SETFOREGROUND);

    CaBurnLog(result == IDYES ? L"[ConfirmDowngrade] user YES"
                              : L"[ConfirmDowngrade] user NO — cancelling");
    return (result == IDYES) ? ERROR_SUCCESS : ERROR_INSTALL_USEREXIT;
}

extern "C" __declspec(dllexport)
UINT __stdcall CleanupDimeDllBak(MSIHANDLE /*hInstall*/)
{
    // Process recorded bak paths.  Each entry is "src|bak"; delete only the bak.
    BakEntry entries[16] = {};
    int n = ReadAllInstallerState(entries, 16);
    for (int i = 0; i < n; ++i)
    {
        if (!entries[i].path[0]) continue;
        wchar_t srcPath[MAX_PATH] = {};
        wchar_t bakPath[MAX_PATH] = {};
        SplitStatePair(entries[i].path, srcPath, bakPath, MAX_PATH);
        if (bakPath[0]) MarkFileForDeletion(bakPath);
    }
    DeleteInstallerState();

    // Glob-scan legacy NSIS locations (System32, SysWOW64)
    wchar_t sys32[MAX_PATH] = {};
    wchar_t syswow[MAX_PATH] = {};
    GetSystemDirectoryW(sys32, MAX_PATH);
    GetSystemWow64DirectoryW(syswow, MAX_PATH);
    CleanupBaksInDir(sys32);
    CleanupBaksInDir(syswow);

    // Glob-scan WiX ProgramFiles locations
    // %ProgramFiles% resolves correctly on both x64 process (C:\Program Files)
    // and x86 process on x86 OS (C:\Program Files).
    // DIME-32bit.msi has a Launch Condition blocking 64-bit OS, so x86 DLL
    // CleanupDimeDllBak never runs on a 64-bit OS where %ProgramFiles% would
    // be the (x86) folder.
    wchar_t pf[MAX_PATH] = {};
    GetEnvironmentVariableW(L"ProgramFiles", pf, MAX_PATH);
    if (pf[0])
    {
        const wchar_t* subDirs[] = { L"amd64", L"arm64", L"x86" };
        for (int i = 0; i < 3; ++i)
        {
            wchar_t dir[MAX_PATH + 32] = {};
            lstrcpynW(dir, pf, MAX_PATH);
            lstrcatW(dir, L"\\DIME\\");
            lstrcatW(dir, subDirs[i]);
            CleanupBaksInDir(dir);
            // Remove the subdir if now empty (MSI can't do it because the bak
            // file was still present when RemoveFiles ran).
            RemoveDirectoryW(dir);
        }
        // Remove orphan .cin baks from prior interrupted install cycles,
        // then remove the DIME parent dir if now empty.
        wchar_t dimeDir[MAX_PATH + 8] = {};
        lstrcpynW(dimeDir, pf, MAX_PATH);
        lstrcatW(dimeDir, L"\\DIME");
        CleanupCinBaksInDir(dimeDir);

        // Scrub PendingFileRenameOperations for every live .cin file in the DIME dir.
        // When a .cin is locked without FILE_SHARE_DELETE during an upgrade,
        // MoveLockedDimeDll cannot rename it, so RemoveExistingProducts falls back to
        // MoveFileExW(MOVEFILE_DELAY_UNTIL_REBOOT) — MSI then exits 3010, which is
        // unavoidable (the exit code is decided before commit CAs run).  Scrubbing
        // here ensures the newly-installed .cin files are NOT deleted on the next
        // reboot, preventing a silent post-reboot corruption of the installation.
        {
            wchar_t cinPattern2[MAX_PATH + 8] = {};
            lstrcpynW(cinPattern2, dimeDir, MAX_PATH);
            lstrcatW(cinPattern2, L"\\*.cin");
            WIN32_FIND_DATAW cfd = {};
            HANDLE hCinFind = FindFirstFileW(cinPattern2, &cfd);
            if (hCinFind != INVALID_HANDLE_VALUE)
            {
                do
                {
                    if (cfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                    // Skip bak files — live .cin files have no dot-suffix after ".cin"
                    if (wcsstr(cfd.cFileName, L".cin.")) continue;
                    wchar_t cinFull[MAX_PATH + 16] = {};
                    lstrcpynW(cinFull, dimeDir, MAX_PATH);
                    lstrcatW(cinFull, L"\\");
                    lstrcatW(cinFull, cfd.cFileName);
                    ScrubPendingFileRenameOperation(cinFull);
                }
                while (FindNextFileW(hCinFind, &cfd));
                FindClose(hCinFind);
            }
        }

        RemoveDirectoryW(dimeDir);
    }

    return ERROR_SUCCESS;
}

#endif // !BUILDING_DOWNGRADE_CHECK

// ============================================================================
// DowngradeCheck EXE entry point
// Compiled by DowngradeCheck.vcxproj with BUILDING_DOWNGRADE_CHECK defined.
//
// Finds the highest installed DIME version by enumerating both MSI UpgradeCodes
// via MsiEnumRelatedProductsW. Compares against the version being installed
// (argv[1] from Burn InstallCommand="[WixBundleVersion]").
// Shows a bilingual Yes/No dialog on downgrade. Returns 0 (proceed) or 1 (cancel).
// ============================================================================
#ifdef BUILDING_DOWNGRADE_CHECK
#pragma comment(lib, "user32.lib")

// Parse "major.minor.build.rev" into a comparable ULONGLONG (16 bits per field).
static ULONGLONG ParseVersion(const wchar_t* s)
{
    ULONGLONG p[4] = {};
    int f = 0;
    for (; *s && f < 4; ++s)
    {
        if      (*s == L'.')                   ++f;
        else if (*s >= L'0' && *s <= L'9') p[f] = p[f] * 10 + (*s - L'0');
    }
    return (p[0] << 48) | (p[1] << 32) | (p[2] << 16) | p[3];
}

// Enumerate both DIME MSI UpgradeCodes; return the highest installed version.
// Built as Win32 (x86), but KEY_WOW64_64KEY is passed to MsiEnumRelatedProductsW
// implicitly via the MSI API -- it always reads from the correct registry view.
static bool GetInstalledDimeVersion(wchar_t* out, DWORD cchOut)
{
    // x64 MSI UpgradeCode (DIME-64bit.msi) and x86 MSI UpgradeCode (DIME-32bit.msi)
    static const wchar_t* kUpgradeCodes[] = {
        L"{6F8A4D2E-1B3C-5E7F-9A0B-2D4F6C8E0A1B}",
        L"{7A9B1C3D-2E4F-6A8B-0C1E-3F5A7B9C1D3F}",
    };
    ULONGLONG best = 0;
    out[0] = L'\0';
    for (int u = 0; u < 2; ++u)
    {
        for (DWORD idx = 0; ; ++idx)
        {
            wchar_t pc[64] = {};
            UINT r = MsiEnumRelatedProductsW(kUpgradeCodes[u], 0, idx, pc);
            if (r == ERROR_NO_MORE_ITEMS) break;
            if (r != ERROR_SUCCESS)       continue;
            wchar_t ver[64] = {};
            DWORD cch = 64;
            if (MsiGetProductInfoW(pc, INSTALLPROPERTY_VERSIONSTRING, ver, &cch)
                    == ERROR_SUCCESS && ver[0])
            {
                ULONGLONG v = ParseVersion(ver);
                if (v > best) { best = v; lstrcpynW(out, ver, (int)cchOut); }
            }
        }
    }
    return best > 0;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR lpCmdLine, int)
{
    // Strip any leading whitespace/quotes from Burn's substitution.
    const wchar_t* installingVerStr = lpCmdLine;
    while (*installingVerStr == L' ' || *installingVerStr == L'"') ++installingVerStr;
    if (!installingVerStr[0]) return 0;  // no version arg -> proceed

    wchar_t installedVerStr[64] = {};
    if (!GetInstalledDimeVersion(installedVerStr, 64)) return 0;  // nothing installed -> proceed

    if (ParseVersion(installedVerStr) <= ParseVersion(installingVerStr)) return 0;  // upgrade/same -> proceed

    // Downgrade detected -- ask user.
    wchar_t msg[1024];
    wsprintfW(msg,
        L"\u76ee\u524d\u5df2\u5b89\u88dd DIME %s\n"
        L"\u5b89\u88dd\u7a0b\u5f0f\u7248\u672c\u70ba DIME %s\uff08\u8f03\u820a\uff09\n\n"
        L"\u964d\u7248\u5b89\u88dd\u5c07\u5148\u79fb\u9664\u5df2\u5b89\u88dd\u7684\u65b0\u7248\uff0c\n"
        L"\u518d\u5b89\u88dd\u820a\u7248\u3002\u662f\u5426\u78ba\u5b9a\u8981\u964d\u7248\uff1f\n\n"
        L"\x2015\x2015\x2015\x2015\x2015\x2015\x2015\x2015\x2015\x2015\x2015\x2015\x2015\x2015\n"
        L"Installed version : DIME %s\n"
        L"This installer    : DIME %s (older)\n\n"
        L"Downgrade will remove the newer version,\n"
        L"then install this older version.\n"
        L"Proceed with downgrade?",
        installedVerStr, installingVerStr,
        installedVerStr, installingVerStr);

    int r = MessageBoxW(
        nullptr, msg,
        L"DIME \u8f38\u5165\u6cd5 \x2014 \u78ba\u8a8d\u964d\u7248 / Downgrade Confirmation",
        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2 | MB_TOPMOST | MB_SETFOREGROUND);

    return (r == IDYES) ? 0 : 1;  // 1 mapped to State="cancel" in Bundle.wxs
}

#endif // BUILDING_DOWNGRADE_CHECK