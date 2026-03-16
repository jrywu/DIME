# MSI Uninstall FilesInUse Issue

## Problem

When uninstalling DIME via direct MSI (`msiexec /i DIME-64bit.msi`) with DIME.dll locked
(e.g., DIME is the active IME in Notepad), the **FilesInUse dialog** appears asking the user
to close the process. This blocks uninstall until the user clicks Ignore.

Burn bundle (`DIME-Universal.exe`) does NOT have this issue — it runs MSI in quiet mode
where FilesInUse is auto-ignored.

### History

1. **Original issue**: Error 2803 ("dialog failed to create") — MSI tried to show FilesInUse
   dialog but the dialog definition was missing from the WXS.
2. **Fix (commit c847a3e)**: Added `<DialogRef Id="FilesInUse" />` plus CancelDlg, ExitDlg,
   UserExitDlg, FatalErrorDlg. Error 2803 is gone — the dialog now shows correctly.
3. **Current issue**: The FilesInUse **dialog itself** is unwanted. We want uninstall to
   proceed silently past locked files (the deferred CA handles the rename later).

## Root Cause

`EarlyRenameForValidate` is an **immediate CA** that runs before `InstallValidate` (seq 1400).
It tries to rename locked DIME.dll so InstallValidate finds nothing locked.

**The immediate CA fails with err=5 (ACCESS_DENIED)**:
- Immediate CAs run as the **admin user**, not SYSTEM
- Files in `C:\Program Files\DIME\` are TrustedInstaller-owned
- When DIME.dll has an active image section (loaded via LoadLibrary by COM/TSF),
  admin cannot rename it — only SYSTEM can
- Both `MoveFileExW` and `CopyFileW` fail with err=5
- Confirmed: `move` from elevated cmd also fails with "access denied"

**The deferred CA (`MoveLockedDimeDll`) succeeds** because it runs as SYSTEM.
But deferred CAs only execute at `InstallFinalize` time — AFTER `InstallValidate`
has already shown the FilesInUse dialog.

**MSI execution model constraint**: Deferred CAs are written to the installation script
when encountered in the sequence, but the script only executes when `InstallFinalize`
runs. There is no way to execute a deferred CA (as SYSTEM) before `InstallValidate`.

## What Was Tried (This Session)

### Category 1: Try to rename the locked DLL from immediate CA (admin)

| # | Approach | Result |
|---|----------|--------|
| 1 | `MoveFileExW(dll, bak)` | err=5 (ACCESS_DENIED) — image section blocks admin rename |
| 2 | `CopyFileW(src, dst)` | err=5 — CopyFileW copies security descriptors, blocked by image section |
| 3 | `CopyAndPosixReplace` (CopyFileW + POSIX rename) | CopyFileW fails at step 1 (err=5) |
| 4 | Manual copy (`CreateFileW` + `ReadFile`/`WriteFile`) | READ succeeds, but POSIX rename of target fails err=5 |
| 5 | Enable `SE_BACKUP_NAME` / `SE_RESTORE_NAME` | Not in admin token (SE_BACKUP=0) |
| 6 | Impersonate SYSTEM via winlogon.exe token (`SeDebugPrivilege`) | err=1300 (NOT_ALL_ASSIGNED) — privilege not in CA's token |

**Conclusion**: Admin cannot rename image-mapped DLLs. No privilege escalation
available from MSI immediate CA context.

### Category 2: Run as SYSTEM before InstallValidate

| # | Approach | Result |
|---|----------|--------|
| 7 | Make EarlyRenameForValidate deferred + move InstallInitialize to seq 1300 | Deferred CAs execute at InstallFinalize, not at sequence position |

**Conclusion**: MSI execution model prevents deferred CAs from running before
InstallValidate.

### Category 3: Suppress the FilesInUse dialog

| # | Approach | Result |
|---|----------|--------|
| 8 | `MsiRestartManagerControl=Disable` (mixed case, in WXS) | Disables RM (no 60s stall) but legacy FilesInUse check still shows dialog |
| 9 | `MSIRESTARTMANAGERCONTROL` (uppercase, in WXS) | Different dialog style (window titles), still shows |
| 10 | Set `MSIRESTARTMANAGERCONTROL=Disable` from CA at runtime | Same as #8 |
| 11 | Override FilesInUse dialog in TerminalDialogs.wxs | Duplicate dialog errors with WixUI_Common |
| 12 | Remove `<DialogRef Id="FilesInUse" />` | WixUI_Common still provides the dialog definition |
| 13 | `MsiSetExternalUIW` callback returning IDIGNORE for FILESINUSE | Handler installed (log confirms) but dialog still appears. MSDN: `MsiSetExternalUI` must be called BEFORE `MsiInstallProduct` — calling from within a CA (mid-session) does not intercept the dialog engine. |

**Conclusion**: `MsiRestartManagerControl=Disable` prevents the RM stall but NOT the
legacy file-in-use dialog. WixUI_Common always provides the FilesInUse dialog definition.
`MsiSetExternalUI` cannot be called from within a CA to intercept dialogs.

### Verified facts from testing

| Test | Result |
|------|--------|
| `copy DIME.dll %TEMP%\test.dll` (elevated cmd) | **Succeeds** — admin CAN read image-mapped DLL |
| `echo test > amd64\test.txt` (elevated cmd) | **Succeeds** — admin CAN write to directory |
| `move DIME.dll DIME.dll.bak` (elevated cmd) | **Fails** — image section blocks admin rename |
| POSIX rename (PowerShell test script) | **Fails err=5** — image section blocks POSIX replace too |
| Deferred CA `MoveLockedDimeDll` (SYSTEM) | **Succeeds** — SYSTEM exempt from image section restriction |
| All test scenarios (`/qn` mode) | **Pass** — silent mode auto-ignores FilesInUse |
| Burn bundle (`/quiet` mode) | **Works** — same as `/qn` |
| Controlled Folder Access | **OFF** — not the cause |

## Corrected Timeline

```
1. Committed code (HEAD = c847a3e):
   - EarlyRenameForValidate: immediate CA, MoveFileExW + CopyFileW copy-back
   - <FilesInUse Suppress="yes" /> in UISequence
   - <Property Id="MsiRestartManagerControl" Value="Disable" />
   - NO <DialogRef Id="FilesInUse" />
   - WixUI_Common provides FilesInUse dialog definition
   - Result: FilesInUse dialog shows when DLL is locked (WixUI_Common's dialog)

2. Folder-rename era (previous session, uncommitted on top of HEAD):
   - CA changed to try directory rename (failed with err=5)
   - WXS condition: REMOVE AND NOT UPGRADINGPRODUCTCODE → REMOVE OR UPGRADINGPRODUCTCODE
   - Added <DialogRef Id="ExitDlg" />, UserExitDlg, FatalErrorDlg (fix other 2803s)
   - Added <DialogRef Id="FilesInUse" /> (defense-in-depth)
   - Dialog was GONE — user confirmed "no 2803 error anymore"

3. This session — test script + my fixes:
   - Ran Test-DIMEInstaller.ps1 → 96 failures (shortcut, COM/TSF keys)
   - I modified CA code to fix failures (removed copy-back, changed conditions, etc.)
   - Dialog came back and stayed through all subsequent fix attempts
```

**The "working state" (step 2) had MORE changes than HEAD, not fewer. It included ALL
the DialogRef additions AND the condition change. These changes are still in the current
working tree — they were never lost.**

The dialog returned in step 3 because of my CA code modifications, not because of
WXS changes. The CA code changes affected how EarlyRenameForValidate behaves, which
determines whether InstallValidate finds a locked file.

## Unsolved Mystery: Folder-Rename Era Working State

The user confirmed: during the folder-rename era, manual testing with `msiexec /i` and
DIME.dll locked (DIME IME active in Notepad) produced NO FilesInUse dialog. The burn log
from that period shows `dir rename FAILED err=5` — the directory rename did not work.
Yet the dialog did not appear.

**We cannot reproduce this state.** Every approach tried in this session fails because
the immediate CA (admin) cannot rename image-mapped DLLs (err=5), and InstallValidate
then detects the locked file and shows the dialog.

**Burn log line 311**: The ONE successful MoveFileExW from the immediate CA during a
manual test (`UILevel=5`, `REMOVE=FeatureMain`). This proves MoveFileExW CAN succeed
from admin on THIS system — but only when the image section is not blocking it.

**Possible explanations for the "working" state**:
1. DIME.dll was not image-mapped at that moment (IME was inactive or unloaded by TSF)
2. A specific combination of WXS/CA state prevented InstallValidate from checking, which
   we haven't identified
3. The MSI built during that era had different internal state (WiX compilation difference)

## Approaches NOT Yet Tried (at time of writing)

| # | Approach | Description | Risk/Complexity |
|---|----------|-------------|-----------------|
| A | `schtasks` as SYSTEM | Immediate CA creates+runs a one-shot scheduled task as SYSTEM to rename DLLs. | Medium. Depends on Task Scheduler. |
| B | WiX `<EmbeddedUI>` DLL | Build a minimal DLL that intercepts `INSTALLMESSAGE_FILESINUSE` and returns `IDIGNORE`. | High. New DLL project. |
| C | Suppress WixUI FilesInUse | Remove FilesInUse from Dialog table. Risk: error 2803. | Low complexity. May cause 2803. |
| G | `MsiSetExternalUIW` from immediate CA | Install a callback that returns `IDIGNORE` for FilesInUse messages. | Low complexity. **FAILED** — must be called before `MsiInstallProduct`, not mid-session. |
| **H** | **`schtasks` as SYSTEM from immediate CA** | **Create+run a one-shot scheduled task as SYSTEM to rename DLLs before InstallValidate. Only remaining approach that can get SYSTEM access from immediate CA context.** | **Medium complexity. Only untried viable option.** |

## Failed Solution: `MsiSetExternalUI` to Suppress FilesInUse

**FAILED**: `MsiSetExternalUI` must be called before `MsiInstallProduct`, not from
within a CA mid-session. The handler was installed (burn log confirms) but the MSI
dialog engine bypassed it.

## Recommended Solution: `schtasks` as SYSTEM (Approach H)

### How MsiSetExternalUI was supposed to work (FAILED)

`MsiSetExternalUI` sets an external UI callback for the MSI session. An immediate CA
runs in the same process as the MSI engine, so it CAN call `MsiSetExternalUI`. The
callback intercepts `INSTALLMESSAGE_FILESINUSE` and returns non-zero ("handled"). The
MSI engine then skips its internal handler — no dialog shown, no error 2803.

From MSDN:
> If the external user interface handler returns a non-zero result, the message is
> handled. In this case, the internal handler is not called.

### Implementation

In `EarlyRenameForValidate` (or a new immediate CA scheduled before `InstallValidate`):

```cpp
// Callback that suppresses FilesInUse — returns IDIGNORE for file-in-use messages,
// passes everything else to the previous handler.
static INSTALLUI_HANDLERW s_prevHandler = nullptr;
static INT WINAPI SuppressFilesInUseHandler(
    LPVOID pvContext, UINT iMessageType, LPCWSTR szMessage)
{
    UINT mt = iMessageType & 0xFF000000;
    if (mt == INSTALLMESSAGE_FILESINUSE || mt == INSTALLMESSAGE_RMFILESINUSE)
        return IDIGNORE;
    // Pass to previous handler
    if (s_prevHandler)
        return s_prevHandler(pvContext, iMessageType, szMessage);
    return 0;
}

// In EarlyRenameForValidate, when MoveFileExW fails:
s_prevHandler = MsiSetExternalUIW(SuppressFilesInUseHandler,
    INSTALLLOGMODE_FILESINUSE | INSTALLLOGMODE_RMFILESINUSE, nullptr);
```

This intercepts BOTH `INSTALLMESSAGE_FILESINUSE` (legacy) and
`INSTALLMESSAGE_RMFILESINUSE` (RM). InstallValidate sends the message, our handler
returns IDIGNORE, MSI proceeds. The deferred CA (SYSTEM) handles the actual rename later.

### Why this is better than other approaches

- No external tools (schtasks, services)
- No privilege escalation (SeDebugPrivilege, SYSTEM impersonation)
- No dialog definition changes (no risk of error 2803)
- No WiX EmbeddedUI DLL project
- Single function call in existing CA code
- Works regardless of image section / admin permission issues

### Test scenario impact

| Scenario | Impact |
|----------|--------|
| A-D, F, I, Matrix (no lock) | No impact — FilesInUse never triggered |
| E (exclusive lock, expects 1603) | Need to verify: if FilesInUse is suppressed, does InstallFiles still fail? Yes — the file is still locked, InstallFiles can't overwrite → 1603 rollback |
| G1/G2 (permissive lock) | No impact — MoveFileExW succeeds (no image section) |
| H (uninstall + lock, timing) | **Improved** — no dialog stall at all |
| `/qn` mode | No impact — already auto-handles FilesInUse |

The handler should ONLY be installed when MoveFileExW fails (err=5), so it doesn't
affect scenarios where the rename succeeds.

### Implementation steps

1. Revert `DIMEInstallerCA.cpp` to HEAD — remove `ImpersonateSystem`,
   `EnablePrivilege`, `FindProcessByName`, `#include <tlhelp32.h>` added this session
2. Add `SuppressFilesInUseHandler` static callback function (returns `IDIGNORE`
   for `INSTALLMESSAGE_FILESINUSE` / `INSTALLMESSAGE_RMFILESINUSE`, passes all
   other messages to the previous handler)
3. In `EarlyRenameForValidate`, after the rename loop, if any DLL rename failed
   (earlyIdx < number of existing DLLs), call `MsiSetExternalUIW` to install the
   handler. This suppresses the FilesInUse dialog only when needed.
4. Rebuild via `deploy-wix-installer.ps1` from `installer/`
5. Manual test: lock DIME.dll (DIME IME in Notepad), `msiexec /i DIME-64bit.msi`
   uninstall — verify FilesInUse dialog does NOT appear
6. Run `Test-DIMEInstaller.ps1` — verify all A-I + Matrix scenarios pass

### How schtasks approach works

In `EarlyRenameForValidate`, when `MoveFileExW` fails with err=5 (image section):

1. Write a temp batch file: `move "C:\...\DIME.dll" "C:\...\DIME.dll.bak"`
2. `CreateProcessW("schtasks /create /tn DIMEEarlyRename /tr <batch> /ru SYSTEM /sc once /st 00:00 /f")`
3. `CreateProcessW("schtasks /run /tn DIMEEarlyRename")` — triggers immediate execution
4. Wait for task completion (poll or fixed timeout)
5. `CreateProcessW("schtasks /delete /tn DIMEEarlyRename /f")` — cleanup
6. Delete temp batch file
7. If rename succeeded (bak exists), write `early_bak_N` + `CopyFileW(bak, dll)` copy-back

The task runs as SYSTEM, which can rename image-mapped DLLs. All happens before
InstallValidate — no dialog, no error.

### Test scenario impact

| Scenario | Impact |
|----------|--------|
| A-D, F, I, Matrix (no lock) | No impact — MoveFileExW succeeds or DLL not present |
| E (exclusive lock, FileShare.None) | No impact — even SYSTEM can't rename FileShare.None → 1603 |
| G1/G2 (permissive lock) | No impact — MoveFileExW succeeds (FileStream, no image section) |
| H (uninstall + lock) | **Improved** — SYSTEM renames before InstallValidate, no dialog |

### Implementation steps

1. Revert `DIMEInstallerCA.cpp` — remove `MsiSetExternalUI` / `SuppressFilesInUseHandler`
2. Add `RunAsSystemViaSchtasks(cmdLine, logBuf)` helper
3. In `EarlyRenameForValidate`, when err=5: build batch, call helper, check result
4. Rebuild and test

### Files to modify

- `installer/wix/DIMEInstallerCA/DIMEInstallerCA.cpp` — only file changed
- WXS files: untouched

## Deeper Analysis

### Why tests pass but manual test fails

All test scenarios use `msiexec /qn` (quiet/no-UI mode). In `/qn` mode, MSI auto-ignores
`INSTALLMESSAGE_FILESINUSE` — no dialog shown, install proceeds. This is why:

- Test scenarios E, G, H all pass with locked DLLs
- Burn bundle always works (runs MSI in `/quiet` mode)
- User's manual test (`msiexec /i DIME-64bit.msi`, default FULL UI) shows the dialog

### What `MsiRestartManagerControl=Disable` actually does

- Disables the Restart Manager API (RmGetList) — prevents the 60-second stall
- Does NOT disable the legacy file-in-use check (MSI tries exclusive open)
- In `/qn` mode: both RM and legacy results are auto-handled → no dialog
- In full UI mode: legacy check detects lock → shows FilesInUse dialog

### The "lost solution" — burn log forensics

The burn log shows 161 successful renames and 54 failures across history:

| Log Lines | Version | Result | Context |
|-----------|---------|--------|---------|
| 49-673 | 1.3.490/492 | `MoveFileExW err=5` | Manual tests — DLL locked by COM/TSF (LoadLibrary + image section) |
| 311 | 1.3.492 | `renamed` (1 success) | Manual test — DLL happened to not be locked |
| 744-962 | 1.3.492 | `dir rename FAILED err=5` | Folder-rename era — directory rename blocked |
| 1033-3455 | 1.3.492 | `early dlls renamed: 0` | File rename era — DLL not found or locked |
| **4175-5847** | **1.3.478** | **`moved` (ALL succeed)** | **Automated test runs** (`Test-DIMEInstaller.ps1`) |
| 5892-6411 | 1.3.492 | `early dlls renamed: 0` | Manual tests — err=5 again |
| 6922-6971 | 1.3.492 | `renamed` | Deferred CA (SYSTEM) — always succeeds |

**Key finding**: ALL successful renames (lines 4175-5847) are from `Test-DIMEInstaller.ps1`
automated runs (version 1.3.478 = old test version). The test uses `FileStream` with
`FileShare.ReadWriteDelete` — this does NOT create an image section.

**The critical difference**:

| Lock Method | Image Section | FILE_SHARE_DELETE | MoveFileExW (admin) |
|-------------|---------------|-------------------|---------------------|
| `FileStream` (test scenario H) | No | Yes | **Succeeds** ✓ |
| `LoadLibrary` (COM/TSF real world) | **Yes** | Yes | **err=5** ✗ |
| SYSTEM (deferred CA) | Yes | Yes | **Succeeds** ✓ |

The **image section mapping** (SEC_IMAGE created by LoadLibrary) is what blocks MoveFileExW
for non-SYSTEM users on this Windows 11 build (26200). SYSTEM is exempt.

The "lost solution" was never a code fix — it was that the automated test uses FileStream
(no image section), so MoveFileExW always succeeded in tests. Manual testing with DIME as
active IME uses LoadLibrary (image section), which blocks admin MoveFileExW.

### CopyAndPosixReplace — why it failed and how to fix

CopyAndPosixReplace failed with `CopyFileW err=5`. This needs investigation:

- CopyFileW READS the source (should be possible on image-mapped files — admin has
  Read & Execute access) and CREATES the destination (needs Write access to directory)
- The err=5 might be from the DESTINATION (directory DACL), not the source
- If `copy DIME.dll %TEMP%\test.dll` works from elevated cmd, CopyFileW CAN read the file
- If `echo test > "C:\Program Files\DIME\amd64\test.txt"` works, admin CAN write to dir
- POSIX rename (`SetFileInformationByHandle` with `FILE_RENAME_FLAG_POSIX_SEMANTICS`)
  can replace an image-mapped file's directory entry on Win10 1809+ — the old inode
  becomes orphaned, the new inode takes the path

### Deferred CA limitation (confirmed)

Deferred CAs are written to the installation script when encountered in the sequence,
but the script only executes when `InstallFinalize` runs. Moving `InstallInitialize`
before `InstallValidate` and making EarlyRenameForValidate deferred was tried — the CA
executed successfully as SYSTEM but AFTER InstallValidate had already shown the dialog.

## Investigation Results

Tested with DIME active as IME in Notepad (DLL image-mapped via LoadLibrary):

| Command | Result |
|---------|--------|
| `copy DIME.dll %TEMP%\test.dll` | **Succeeds** — admin CAN read image-mapped DLL |
| `echo test > amd64\test.txt` | **Succeeds** — admin CAN write to directory |
| `move DIME.dll DIME.dll.bak` | **Fails err=5** — image section blocks rename |

**Conclusion**: `CopyFileW` err=5 was NOT from reading or directory write — it was likely
from CopyFileW internally attempting to copy security descriptors or alternate data streams
on the image-mapped source file. A manual read/write copy (ReadFile/WriteFile loop) bypasses
this. Then POSIX rename (`SetFileInformationByHandle` with `FILE_RENAME_FLAG_POSIX_SEMANTICS`)
can atomically replace the image-mapped file's directory entry on Win10 1809+.

## Solution: Fix CopyAndPosixReplace to bypass CopyFileW limitation

### Step 1: Revert WXS and CA to committed state

- Remove `<InstallInitialize Sequence="1300" />` from both WXS files
- Revert EarlyRenameForValidate to immediate CA (remove `Execute="deferred"`,
  remove SetProperty for EarlyRenameForValidate)
- Revert CA code to read from `MoveLockedDimeDllUninstall` property (not CustomActionData)
- Restore REMOVE/UPGRADINGPRODUCTCODE checks in CA code

### Step 2: Replace CopyFileW with manual file copy in CopyAndPosixReplace

In `CopyAndPosixReplace`, replace `CopyFileW(src, tmpPath, FALSE)` with a manual copy:

```cpp
// CopyFileW fails on image-mapped files (copies security descriptors that are
// blocked by the image section). Manual ReadFile/WriteFile bypasses this.
HANDLE hSrc = CreateFileW(src, GENERIC_READ,
    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
    nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
HANDLE hDst = CreateFileW(tmpPath, GENERIC_WRITE,
    0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
// ReadFile/WriteFile loop...
```

### Step 3: Make CopyAndPosixReplace a general fallback (not just ERROR_SHARING_VIOLATION)

In `EarlyRenameForValidate`, call CopyAndPosixReplace for ANY MoveFileExW failure,
not just `ERROR_SHARING_VIOLATION`. This handles err=5 (image section) too.

### Step 4: Remove CopyFileW(bak, dll) copy-back

The copy-back puts a file back at the original path, which RM/legacy check detects as
locked. Without copy-back, the original path is empty after MoveFileExW succeeds, or
has a fresh unlocked inode after CopyAndPosixReplace succeeds.

### Step 5: Verify

1. `deploy-wix-installer.ps1` — rebuild
2. Manual test: lock DIME.dll (use as IME in Notepad), run `msiexec /i DIME-64bit.msi`
   to uninstall — FilesInUse dialog should NOT appear
3. `Test-DIMEInstaller.ps1` — all A-I + Matrix scenarios pass
4. Check burn log: should show `POSIX fallback OK` for image-mapped DLLs

## Current File State

- `installer/wix/DIMEInstallerCA/DIMEInstallerCA.cpp` — EarlyRenameForValidate currently
  reads from `CustomActionData` (deferred CA mode). Needs revert + CopyAndPosixReplace fix.
- `installer/wix/DIME-64bit.wxs` — Has `<InstallInitialize Sequence="1300" />` and
  EarlyRenameForValidate as `Execute="deferred"`. Needs revert.
- `installer/wix/DIME-32bit.wxs` — Same as 64bit.
