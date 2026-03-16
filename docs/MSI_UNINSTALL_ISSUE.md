# MSI FilesInUse Dialog — Root Cause and Fix

## Problem

When installing or uninstalling DIME via direct MSI (`msiexec /i DIME-64bit.msi` or
double-click) with DIME.dll locked (DIME is the active IME), the **FilesInUse dialog**
appears asking the user to close the locking process. This blocks the operation until
the user clicks Ignore.

Burn bundle (`DIME-Universal.exe`) does NOT have this issue.

## Root Cause

### MSI elevation model

When a user double-clicks the MSI or runs `msiexec /i` from a non-elevated cmd:

1. `msiexec.exe` starts as a **non-elevated** client process
2. The `InstallUISequence` runs in this non-elevated client
3. The `InstallExecuteSequence` runs — **immediate CAs** still run in the non-elevated
   client; only **deferred CAs** run as SYSTEM via the MSI service
4. `EarlyRenameForValidate` (immediate CA, before `InstallValidate`) runs non-elevated
5. `MoveFileExW(DIME.dll, bak)` → **err=5** (non-elevated process cannot rename
   image-mapped DLLs in `C:\Program Files`)
6. `InstallValidate` finds the locked DLL → shows FilesInUse dialog

### Why Burn works

Burn's bootstrapper EXE has a `requireAdministrator` manifest → UAC prompts at launch →
the entire process (including the MSI client) runs elevated → `MoveFileExW` succeeds →
no dialog.

### Key test result

From **elevated cmd** with DIME.dll locked (DIME IME active, typed in Notepad):

```
MoveFileExW: err=0 (SUCCEEDS)
```

**Elevated admin CAN rename image-mapped DLLs.** The issue is purely about elevation
timing — not image sections, DACLs, or privileges.

## Fix: `SelfElevate` Custom Action

### How it works

A new immediate CA `SelfElevate` is added to the `InstallUISequence` as the very first
action (before `AppSearch`). It checks `IsUserAnAdmin()`:

- **Already elevated** (Burn, elevated cmd, `/qn` from elevated PowerShell):
  returns `ERROR_SUCCESS` → proceeds normally
- **Not elevated** (double-click, non-elevated cmd):
  calls `ShellExecuteExW` with `runas` verb to re-launch `msiexec /i <MSI path>` →
  UAC prompt → new msiexec runs elevated → returns `ERROR_INSTALL_USEREXIT` to abort
  the current non-elevated session

The re-launched msiexec runs elevated from the start. `SelfElevate` runs again in
the new session, detects elevated context, returns `ERROR_SUCCESS`, and the install
proceeds with all CAs running elevated.

### What this fixes

- `EarlyRenameForValidate` runs elevated → `MoveFileExW` succeeds → no FilesInUse dialog
- All other immediate CAs run elevated → no permission issues
- Same behavior as Burn — elevation happens before any MSI sequence
- `TerminalDialogs.wxs` (ExitDlg, UserExitDlg, FatalErrorDlg) remains as defense-in-depth
  for proper dialog navigation after install completes/cancels/fails

### Implementation

In `DIMEInstallerCA.cpp` — new export `SelfElevate`:

```cpp
extern "C" __declspec(dllexport)
UINT __stdcall SelfElevate(MSIHANDLE hInstall)
{
    if (IsUserAnAdmin())
        return ERROR_SUCCESS;

    wchar_t msiPath[MAX_PATH] = {};
    DWORD cch = MAX_PATH;
    MsiGetPropertyW(hInstall, L"OriginalDatabase", msiPath, &cch);
    if (!msiPath[0])
        return ERROR_SUCCESS;

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
        return ERROR_SUCCESS;

    if (sei.hProcess)
        CloseHandle(sei.hProcess);

    return ERROR_INSTALL_USEREXIT;
}
```

In both WXS files — CA definition + UI sequence scheduling:

```xml
<CustomAction Id="SelfElevate"
              BinaryRef="BinDIMEInstallerCA"
              DllEntry="SelfElevate"
              Execute="immediate"
              Return="check" />

<InstallUISequence>
    <Custom Action="SelfElevate" Before="AppSearch" />
</InstallUISequence>
```

### Files modified

- `installer/wix/DIMEInstallerCA/DIMEInstallerCA.cpp` — added `SelfElevate` export,
  `#include <shellapi.h>`, `#include <shlobj.h>`, `#pragma comment(lib, "shell32.lib")`
- `installer/wix/DIME-64bit.wxs` — added SelfElevate CA definition + InstallUISequence entry
- `installer/wix/DIME-32bit.wxs` — same

## Test Scenario Impact

All test scenarios in `Test-DIMEInstaller.ps1` run from an elevated PowerShell session
using `msiexec /qn` (quiet mode). In this context:

- `IsUserAnAdmin()` returns TRUE → `SelfElevate` is a no-op → no behavior change
- The `/qn` flag skips the `InstallUISequence` entirely → `SelfElevate` never runs

| Scenario | Lock | Method | SelfElevate Impact |
|----------|------|--------|--------------------|
| A1/A2 (Fresh install) | None | Burn/MSI | No impact — no lock, no FilesInUse |
| B (NSIS migration) | None | Burn | No impact |
| C (Major upgrade, 4 combos) | None | Burn/MSI | No impact |
| D (Uninstall/reinstall cycles) | None | Burn/MSI | No impact |
| E1 (Fresh rollback, exclusive lock) | FileShare.None | MSI `/qn` | No impact — `/qn` skips UISequence; exclusive lock still causes 1603 |
| E2 (Upgrade rollback, exclusive lock) | FileShare.None | MSI `/qn` | No impact — same as E1 |
| F1/F2 (Downgrade) | None | Burn/MSI | No impact |
| G1 (DLL permissive lock) | FILE_SHARE_RWD | MSI `/qn` | No impact — MoveFileExW succeeds (FileStream, no image section) |
| G2 (CIN permissive lock) | FILE_SHARE_RWD | MSI `/qn` | No impact |
| G3 (CIN restrictive lock) | No FILE_SHARE_DELETE | MSI `/qn` | No impact |
| H (Uninstall + lock, 4 combos) | FILE_SHARE_RWD | Burn/MSI `/qn` | No impact — `MsiRestartManagerControl=Disable` assertion unchanged |
| I (Stale Dependent) | None | Burn/MSI | No impact |
| Matrix (Repair + cross-uninstall) | None | Burn/MSI | No impact |

**Key**: `SelfElevate` only activates in full-UI mode (`msiexec /i` without `/qn`) when
the client process is not elevated. All automated tests run elevated with `/qn`, so
`SelfElevate` is never triggered. No test changes needed.

### E-MsiRestartManagerControl sanity check

Scenario E verifies `MsiRestartManagerControl=Disable` is in the MSI Property table.
`SelfElevate` does not modify this property. The assertion remains valid.

## History of Approaches Tried

For reference, the following approaches were attempted before arriving at the
`SelfElevate` solution:

| # | Approach | Result |
|---|----------|--------|
| 1 | `MoveFileExW` from immediate CA | err=5 — non-elevated client can't rename |
| 2 | `CopyFileW` | err=5 — non-elevated |
| 3 | `CopyAndPosixReplace` | err=5 — non-elevated |
| 4 | Manual copy + POSIX rename | POSIX rename err=5 — non-elevated |
| 5 | Enable `SeBackupPrivilege` / `SeRestorePrivilege` | err=1300 — privileges not in non-elevated token |
| 6 | Impersonate SYSTEM via `SeDebugPrivilege` | err=1300 — not in non-elevated token |
| 7 | Deferred CA + move InstallInitialize | Deferred CAs execute at InstallFinalize, not at sequence position |
| 8 | `MsiRestartManagerControl=Disable` | Disables RM stall but legacy FilesInUse check still shows dialog |
| 9 | `MsiSetExternalUIW` callback | Must be called before `MsiInstallProduct`, not mid-session |
| 10 | Override FilesInUse dialog in WXS | Duplicate errors with WixUI_Common |
| 11 | POSIX delete on locked DLL | err=5 — non-elevated |
| **12** | **`SelfElevate` in UI sequence** | **Works — re-launches elevated, MoveFileExW succeeds** |
