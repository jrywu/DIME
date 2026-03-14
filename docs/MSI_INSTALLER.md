# DIME MSI Installer — Architecture & Mechanism

## Overview

DIME uses a WiX 4 / Burn installer bundle (`DIME-Universal.exe`) that wraps two MSI packages:

| Package | Target | DLL architecture |
|---|---|---|
| `DIME-64bit.msi` | 64-bit Windows (AMD64 + ARM64) | amd64 + arm64 + x86 (WOW64) |
| `DIME-32bit.msi` | 32-bit Windows only | x86 only |

The Burn bundle selects which MSI to install based on OS architecture.

### Key Capabilities

- **No reboot required** on install, upgrade, downgrade, and uninstall — even while DIME.dll or `.cin` table files are held open by running processes
- **Silent NSIS migration** — detects and removes the legacy NSIS installation automatically before the WiX install proceeds
- **Downgrade confirmation** — Yes/No dialog before rolling back to an older version; silent downgrades blocked automatically
- **Architecture-aware** — a single `DIME-Universal.exe` selects the correct MSI and installs AMD64, ARM64, or x86 DLLs based on detected hardware
- **No prerequisites** — DIME binaries use static CRT (`/MT`); no Visual C++ Redistributable needed
- **Clean uninstall** — removes DLLs, `.cin` files, registry entries, Burn ARP entry, and Package Cache

---

## DLL Installation Location

### New WiX installer (v1.3 onward)

DIME.dll is installed under `%ProgramFiles%\DIME\` in per-architecture subdirectories:

```
C:\Program Files\DIME\
    amd64\DIME.dll      registered in 64-bit COM hive; loaded by x64 processes
    arm64\DIME.dll      registered in ARM64 COM hive; loaded by native ARM64 processes
    x86\DIME.dll        registered in 32-bit COM hive via SysWOW64\regsvr32.exe
    DIMESettings.exe
    *.cin               input method table files
```

ProgramFiles is never subject to WOW64 filesystem redirection, so all paths are
unambiguous regardless of whether the installer MSI is 32-bit or 64-bit.

### Legacy NSIS installer (already on user machines)

The NSIS installer placed DLLs in the Windows system directories:

```
C:\Windows\System32\DIME.dll    x64 (or x86 on 32-bit OS)
C:\Windows\SysWOW64\DIME.dll   x86 (on 64-bit OS only)
```

These locations cannot be changed retroactively. The WiX installer handles
migration away from them automatically via `ConfirmAndRemoveNsisInstall`.

---

## TSF Registration

DIME is a Text Services Framework (TSF) in-process COM server. Registration
via `regsvr32` writes the CLSID → path mapping to the registry:

```
HKCR\CLSID\{guid}\InProcServer32  = "%ProgramFiles%\DIME\amd64\DIME.dll"
```

TSF then loads the DLL by path on demand into every text-input process (Notepad,
browsers, etc.). The physical location (System32 vs ProgramFiles) is irrelevant
to TSF — only the registry path matters.

For the x86 arch on a 64-bit OS, `SysWOW64\regsvr32.exe` must be used so the
registration lands in the 32-bit COM hive (`HKCR` under WOW6432Node).

---

## Reboot-Free File Replacement

Windows loads DLLs with `FILE_SHARE_DELETE`, so a file can be **renamed** while in use — but not deleted. DIME uses a scrub + rename + copy-back pattern on every DIME.dll and `.cin` file before `RemoveFiles` / `InstallFiles` runs:

1. **Scrub** — `ScrubPendingFileRenameOperation` removes any stale `PendingFileRenameOperations` entry for the path left by a prior failed install, preventing `MsiSystemRebootPending=1` and the cascade that forces reboot-path handling for all subsequent files
2. **Rename** `original → original.{ver}.{tick}` — old inode moves to the bak; running processes keep their inode reference; the original path is now vacant
3. **Copy-back** `bak → original` — a fresh, unlocked inode is created at the original path; `RemoveFiles` / `InstallFiles` operate on it without a sharing violation
4. **Rollback** (on failure) — `RollbackMoveLockedDimeDll` reads `InstallerState` and renames every bak back to its recorded original path
5. **Commit** (on success) — `CleanupDimeDllBak` marks baks delete-pending via a 4-step cascade:
   - `DeleteFileW` — immediate if no holder
   - `FileDispositionInfoEx` + `POSIX_SEMANTICS` (Win10 1709+) — removes directory entry instantly while inode stays alive until last handle closes
   - `FileDispositionInfo` delete-pending (older OS) — auto-deletes on last handle close; no reboot required
   - `MoveFileExW(MOVEFILE_DELAY_UNTIL_REBOOT)` — last resort only
   - **Scrub live CIN pending-rename entries** — after deleting CIN baks, glob-scans `%ProgramFiles%\DIME\*.cin` for live (non-bak) CIN files and calls `ScrubPendingFileRenameOperation` on each. This protects newly-installed CINs from being deleted on the next reboot in the G3 edge case (CIN locked without `FILE_SHARE_DELETE` causes MSI to schedule `MOVEFILE_DELAY_UNTIL_REBOOT` for the old CIN, which would clobber the freshly written new CIN).

Bak names include `GetTickCount64()` as a 16-character hex suffix, guaranteeing uniqueness across rapid cycles. `InstallerState` stores `src|bak` pairs for all renamed files and is deleted by the commit or rollback CA.

---

## Registry Handoff Key

`HKLM\SOFTWARE\DIME\InstallerState` is a temporary scratch key used to
communicate bak paths between deferred and commit/rollback CAs (which run
in separate MSI engine invocations and cannot share in-memory state).

Values written by `MoveLockedDimeDll` and `ConfirmAndRemoveNsisInstall`, each as a `src|bak` pair:

```
nsis_bak_0  = "C:\Windows\System32\DIME.dll|C:\Windows\System32\DIME.dll.1.2.3.4.1A2B3C4D"
nsis_bak_1  = "C:\Windows\SysWOW64\DIME.dll|C:\Windows\SysWOW64\DIME.dll.1.2.3.4.1A2B3C4D"
wix_bak_0   = "C:\Program Files\DIME\amd64\DIME.dll|C:\Program Files\DIME\amd64\DIME.dll.1.2.3.4.2B3C4D5E"
wix_bak_1   = "C:\Program Files\DIME\x86\DIME.dll|C:\Program Files\DIME\x86\DIME.dll.1.2.3.4.2B3C4D5E"
cin_bak_0   = "C:\Program Files\DIME\Array.cin|C:\Program Files\DIME\Array.cin.1.2.3.4.3C4D5E6F"
```

The `src` component is used by `RollbackMoveLockedDimeDll` to restore any file type to its exact original path. The `bak` component is used by `CleanupDimeDllBak` for deletion. This key is deleted by either CA when it finishes.

---

## NSIS Migration (First-Time WiX Install)

When the WiX installer detects a prior NSIS installation via the registry key
`HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\DIME`, it performs
a fully silent migration before proceeding:

### Migration technique: rename + copy-back

`ConfirmAndRemoveNsisInstall` (immediate CA, user session) performs:

```
For each of System32\DIME.dll and SysWOW64\DIME.dll that exists:

  1. MoveFileExW(dll, bak_unique)
        The old inode moves to the bak name.
        Running processes retain their inode reference — unaffected.

  2. CopyFileW(bak_unique, dll)
        A brand-new unlocked inode is created at the original path.
        No process holds this new file open.
        CopyFileW can read the bak because the OS loader opened it with
        FILE_SHARE_READ — compatible with CopyFileW's read-only open.

  3. WriteInstallerState("nsis_bak_N", bak_unique)

4. Launch uninst.exe /S and wait (up to 2 minutes)
```

NSIS then:
- `regsvr32 /u /s DIME.dll` — TSF deregistration succeeds on the unlocked copy
- `Delete DIME.dll` — succeeds immediately (no process holds the new inode)
- Falls through to `lbContinueUninstall`
- Removes registry keys, `$INSTDIR`, shortcuts — full cleanup
- Exits silently (no `lbNeedReboot` path reached)

The baks from the old inode remain at their unique paths, loaded by running
processes, and are cleaned up by `CleanupDimeDllBak` in the commit phase.

---

## Custom Action DLL (`DIMEInstallerCA.dll`)

A single DLL embedded in both `DIME-64bit.msi` and `DIME-32bit.msi` as a Binary resource.
No CRT dependency — uses only `kernel32`, `user32`, `advapi32`, `msi.lib`.

### Exports

| Export | Execute | Impersonate | Purpose |
|---|---|---|---|
| `ConfirmAndRemoveNsisInstall` | immediate | yes (user) | NSIS detection, rename, copy-back, silent uninst |
| `ConfirmDowngrade` | immediate | yes (user) | Reads `WIX_UPGRADE_DETECTED`; compares `ProductVersion` vs installed version; for downgrades sets `REINSTALLMODE=amus` and shows a Yes/No messagebox; for upgrades returns immediately; returns `ERROR_INSTALL_USEREXIT` on No or silent downgrade |
| `MoveLockedDimeDll` | deferred | no (SYSTEM) | Rename DLLs away; write bak paths to registry |
| `RollbackMoveLockedDimeDll` | rollback | no (SYSTEM) | Restore baks to original paths on failure |
| `CleanupDimeDllBak` | commit | no (SYSTEM) | Mark baks delete-pending; glob-scan orphan baks; remove empty dirs |
| `CleanupBurnRegistration` | commit | no (SYSTEM) | Remove orphaned Burn ARP entries and Package Cache after uninstall or upgrade; remove stale bundle GUIDs from `SOFTWARE\Classes\Installer\Dependencies\{ProductCode}\Dependents` (stale GUIDs cause Burn error 2803 on future uninstall). `ProductCode` is supplied via `CustomActionData` by the setter CA `SetCleanupBurnRegistrationData` — commit CAs cannot read arbitrary session properties via `MsiGetPropertyW`; only `CustomActionData` is reliably available |

Deferred/rollback/commit CAs use `Impersonate="no"` (SYSTEM token) — a UAC-filtered user token cannot write to `%ProgramFiles%`. `ConfirmAndRemoveNsisInstall` uses `Execute="immediate"` to run in the user session (for UI access) before elevation. For the full CA-by-CA execution order see the [Scenario Comparison table](#scenario-comparison--execution-chain).

### `ConfirmDowngrade` — decision logic

`ConfirmDowngrade` fires unconditionally. It compares versions via `ParseVersionStr` (maps `"major.minor.build.rev"` to a `ULONGLONG`, 16 bits per field) and takes the following action:

| Case | Action |
|---|---|
| `WIX_UPGRADE_DETECTED` empty | Fresh install — proceed immediately |
| Installing version ≥ installed | Upgrade — proceed immediately |
| Downgrade, `UILevel ≤ 2` (silent) | Return `ERROR_INSTALL_USEREXIT` — blocked |
| Downgrade, interactive | Set `REINSTALLMODE=amus`; show Yes/No dialog |

`REINSTALLMODE=amus` forces `CostFinalize` to overwrite higher-versioned on-disk files; without it, `InstallFiles` would skip the older DLLs after `RemoveExistingProducts` deletes the newer ones. It is set in the UISequence so it transfers to the elevated server session before any deferred CAs run.

### CustomActionData format

```
{ProductVersion}|{PF}\DIME\amd64\DIME.dll|{PF}\DIME\arm64\DIME.dll|{PF}\DIME\x86\DIME.dll
```

For `DIME-32bit.msi` (32-bit Windows only, single DLL):
```
{ProductVersion}|{PF}\DIME\x86\DIME.dll
```

For `CleanupBurnRegistration` (set by `SetCleanupBurnRegistrationData`):
```
{ProductCode}
```

---

## Burn Bundle (`DIMEInstallerCA.exe`)

`DIME-Universal.exe` is a WiX Burn bundle. It:

1. Detects OS architecture
2. Installs the appropriate MSI package (`DIME-64bit.msi` or `DIME-32bit.msi`)
3. Passes `REBOOT=ReallySuppress` to all MSI packages — **no reboot prompts**

DIME.dll and DIMESettings.exe are built with the static CRT (`/MT`), so no
Visual C++ Redistributable package is required. The Burn bundle does not need
to install any runtime prerequisites.

> **Important**: Burn passes `REBOOT=ReallySuppress` and `/norestart` to all MSI
> packages — no reboot prompts or automatic reboots are initiated. If the MSI engine
> sets the reboot-pending flag (e.g. via `MoveFileExW(MOVEFILE_DELAY_UNTIL_REBOOT)`),
> the installer exits `3010` (reboot required but not initiated) rather than `1641`
> (reboot initiated). All CAs are designed to avoid scheduling deferred file operations;
> `3010` is only expected in the G3 edge case (CIN locked without `FILE_SHARE_DELETE`).

---

## Scenario Comparison — Execution Chain

Happy-path action-by-action chain for each scenario, in execution order.
Standard WiX/MSI engine actions are **bold**; Custom Actions are unformatted.
`skip` = condition false, action not executed. `—` = not applicable to this scenario/pass.

### UISequence (user session, pre-elevation)

| # | Action | Condition | Fresh install | NSIS migration | Uninstall | Upgrade | Downgrade (Yes) |
|---|---|---|---|---|---|---|---|
| 1 | CA: `CleanupBurnARPEarly` | unconditional | no Burn ARP — exits¹⁰ | no Burn ARP — exits¹⁰ | finds Burn ARP (if installed via Burn); deletes registry entry only¹⁰ | finds old-ver Burn ARP; deletes registry only¹⁰ | finds newer-ver Burn ARP; deletes registry only¹⁰ |
| 2 | **FindRelatedProducts** | — | `WIX_UPGRADE_DETECTED` = *(empty)* | *(empty)* | *(empty)* | set to old ProductCode | set to newer ProductCode |
| 3 | CA: `ConfirmDowngrade` | unconditional¹ | returns immediately (no `WIX_UPGRADE_DETECTED`) | returns immediately (no `WIX_UPGRADE_DETECTED`) | returns immediately (no `WIX_UPGRADE_DETECTED`) | returns immediately (upgrade direction²) | sets `REINSTALLMODE=amus`; messagebox → user clicks Yes |

### ExecuteSequence (elevated; deferred/rollback/commit run as SYSTEM)

Rows marked **(sub-pass)** belong to the old product's own execute sequence, triggered inside `RemoveExistingProducts` with `UPGRADINGPRODUCTCODE` set on that sub-invocation.

| # | Type | Action | Condition | Fresh install | NSIS migration | Uninstall | Upgrade | Downgrade (Yes) |
|---|---|---|---|---|---|---|---|---|
| 1 | immediate | CA: `ConfirmAndRemoveNsisInstall` | `NOT Installed`<br>`AND NOT UPGRADINGPRODUCTCODE`<br>`AND NOT WIX_UPGRADE_DETECTED` | NSIS key absent — skips | **renames sys32/syswow64 DLLs → bak; copy-back unlocked inode**; runs `uninst.exe /S`; waits ≤2 min | skip (`Installed` = true) | skip (`WIX_UPGRADE_DETECTED` set) | skip |
| 2 | standard | **InstallInitialize** | — | ✓ | ✓ | ✓ | ✓ | ✓ |
| 3 | standard | **RemoveExistingProducts** *(afterInstallInitialize)* | major upgrade only | N/A | N/A | N/A | triggers old-product sub-pass | triggers newer-product sub-pass |
| 3a | **(sub-pass)** deferred | CA: `UnregisterXxxDll` | `REMOVE AND NOT UPGRADINGPRODUCTCODE` | — | — | — | condition false (`UPGRADINGPRODUCTCODE` is set in sub-pass) — does not run | condition false — does not run |
| 3b | **(sub-pass)** deferred | CA: `MoveLockedDimeDllUninstall` | `REMOVE AND NOT UPGRADINGPRODUCTCODE` | — | — | — | condition false — does not run | condition false — does not run |
| 3c | **(sub-pass)** standard | **RemoveFiles** | — | — | — | — | deletes old-ver DLLs at original path; if locked: waits until freed³ | deletes newer-ver DLLs; if locked: waits³ |
| 3d | **(sub-pass)** commit | CA: `CleanupDimeDllBakUninstall` | `REMOVE AND NOT UPGRADINGPRODUCTCODE` | — | — | — | condition false — does not run | condition false — does not run |
| 3e | **(sub-pass)** commit | CA: `CleanupBurnRegistration` | `NOT UPGRADINGPRODUCTCODE` | — | — | — | condition false — does not run | condition false — does not run |
| 4 | deferred | CA: `UnregisterXxxDll` *(Before=**RemoveFiles**)* | `REMOVE AND NOT UPGRADINGPRODUCTCODE` | skip | skip | `regsvr32 /u` on live DLL at original path | skip | skip |
| 5 | rollback | CA: `RollbackMoveLockedDimeDllUninstall` | `REMOVE AND NOT UPGRADINGPRODUCTCODE` | skip | skip | armed: renames bak → original path on failure | skip | skip |
| 6 | deferred | CA: `MoveLockedDimeDllUninstall` *(After=CA:`UnregisterX86Dll`, Before=**RemoveFiles**)* | `REMOVE AND NOT UPGRADINGPRODUCTCODE` | skip | skip | scrub stale pending⁴; **rename → bak; copy-back fresh inode** | skip | skip |
| 7 | standard | **RemoveFiles** | — | nothing to remove | nothing to remove | deletes copy-back at original path cleanly (unlocked) | nothing for new product's components | nothing |
| 8 | rollback | CA: `RollbackMoveLockedDimeDllInstall` | `NOT Installed OR UPGRADINGPRODUCTCODE` | skip | skip | skip | armed | armed |
| 9 | deferred | CA: `MoveLockedDimeDllInstall` *(Before=**InstallFiles**)* | `NOT Installed OR UPGRADINGPRODUCTCODE` | DLL absent — internal skip | PF DLLs absent — internal skip⁵ | skip | if DLL removed by 3c: absent — internal skip; if 3c left locked DLL: scrub⁴; **rename → bak; copy-back** | if DLL removed by 3c: absent — internal skip; if 3c left locked DLL: scrub⁴; **rename → bak; copy-back** |
| 10 | standard | **InstallFiles** | — | places DLL | places DLL | nothing to install | places new-ver DLL (overwrites any copy-back from step 9) | places old-ver DLL (forced by `REINSTALLMODE=amus`) |
| 11 | deferred | CA: `RegisterXxxDll` *(After=**InstallFiles**)* | `NOT REMOVE` | `regsvr32` | `regsvr32` | skip | `regsvr32` | `regsvr32` |
| 12 | standard | **InstallFinalize** | — | ✓ | ✓ | ✓ | ✓ | ✓ |
| 13 | commit | CA: `CleanupDimeDllBakInstall` / CA: `CleanupDimeDllBakUninstall` | `NOT REMOVE` / `REMOVE AND NOT UPGRADINGPRODUCTCODE` | no-op⁶ | marks NSIS baks delete-pending⁷ | marks WiX bak delete-pending⁷ | marks old-ver bak delete-pending⁷ (if step 9 renamed one) | marks newer-ver bak delete-pending⁷ (if step 9 renamed one) |
| 13.5 | immediate | CA: `SetCleanupBurnRegistrationData` *(Before=CA:`CleanupBurnRegistration`)* | `NOT UPGRADINGPRODUCTCODE` | sets `CustomActionData` = `[ProductCode]` | same | same | same | same |
| 14 | commit | CA: `CleanupBurnRegistration` | `NOT UPGRADINGPRODUCTCODE` | no match — exits; no stale Dependents | no match — exits; no stale Dependents | deletes Burn ARP registry + Package Cache⁸; removes stale bundle GUIDs from `{ProductCode}\Dependents`⁹ | deletes old-ver Burn ARP + cache⁸; removes stale bundle GUIDs from `{ProductCode}\Dependents`⁹ | deletes newer-ver Burn ARP + cache⁸; removes stale bundle GUIDs from `{ProductCode}\Dependents`⁹ |

**On failure** at any step after a `MoveLockedDimeDll` rename: the armed rollback CA (`RollbackMoveLockedDimeDllInstall` or `RollbackMoveLockedDimeDllUninstall`) renames every bak back to the original path and deletes `InstallerState`. The commit CA never runs (commit phase not reached), so baks are left as plain files.

¹ `ConfirmDowngrade` has no WXS `Condition` attribute — it always fires and checks internally.  
² `ParseVersionStr(installing) >= ParseVersionStr(installed)` → not a downgrade → returns `ERROR_SUCCESS` immediately.  
³ Burn passes `REBOOT=ReallySuppress` to the MSI; a file that cannot be deleted after the wait would return error 1603, aborting the install. In practice TSF unloads DLLs between input sessions so the lock window is bounded.  
⁴ `ScrubPendingFileRenameOperation` — removes any `PendingFileRenameOperations` entry for this DLL path left by a prior failed install, preventing the OS from deleting the freshly installed DLL on the next reboot.  
⁵ `ConfirmAndRemoveNsisInstall` already cleared the sys32/syswow64 DLLs via NSIS; the `%ProgramFiles%\DIME\` paths do not yet exist on first WiX install.  
⁶ `InstallerState` is empty; glob-scan of known DLL directories finds no orphan baks.  
⁷ 4-step cascade per bak: `DeleteFileW` (immediate if no process holds it) → `FileDispositionInfoEx + POSIX_SEMANTICS` (Win10 1709+: removes directory entry instantly while inode stays alive until last handle closes) → `FileDispositionInfo` delete-pending (older OS: auto-deletes on last handle close) → `MOVEFILE_DELAY_UNTIL_REBOOT` (last resort). Also glob-scans for orphan baks from prior interrupted cycles.  
⁸ `CleanupBurnARPEarly` (UISequence, step 1) deleted the registry entry; `CleanupBurnRegistration` (commit) reads the `BundleCachePath` registry value and deletes the cache folder **in the same scan** — so `CleanupBurnRegistration` must find the registry entry intact to clean the cache. For scenarios where the Early CA ran first on the same entry, the cache folder cleanup falls to `CleanupBurnRegistration`'s glob-scan of the Package Cache directory.  
⁹ `CleanupBurnRegistration` reads `ProductCode` from `CustomActionData` (supplied by `SetCleanupBurnRegistrationData`; commit CAs cannot read arbitrary session properties via `MsiGetPropertyW` — only `CustomActionData` is reliably available in that context). It opens `SOFTWARE\Classes\Installer\Dependencies\{ProductCode}\Dependents` and removes every sub-key whose GUID is not the current bundle GUID (`CurrentBundleId` from `SOFTWARE\DIME\BurnCacheCleanup`, written by `CleanupBurnARPEarly`¹⁰). This prevents Burn error 2803 when a stale bundle GUID remains in the Dependents key after a reinstall or upgrade.  
¹⁰ `CleanupBurnARPEarly` also saves `BURN_BUNDLE_ID` (readable in UISequence immediate context) to `HKLM\SOFTWARE\DIME\BurnCacheCleanup\CurrentBundleId`. This GUID identifies the active (new) bundle and is used by `CleanupBurnRegistration`'s stale-Dependent scan (footnote ⁹) to determine which entry to preserve.

---

## Verified Test Matrix

All scenarios below have been tested on Windows 11 x64 with v1.3.478 builds.
✅ = correct / clean  ❌ = not updated (expected for repair)  — = N/A (uninstalled)

### Same-version maintenance (running installer against already-installed same version)

_"Installed by"_ = how the current installation was done.  
_"User runs"_ = which installer the user double-clicks.

| Installed by | User runs | Action | DIME.dll updated | Visible ARP | Package Cache | Notes |
|---|---|---|:---:|---|---|---|
| Burn | Burn | Repair | ❌ | Burn ✅ | Burn + MSI ✅ | `INSTALLEDBYBURN=1` → `CleanupBurnRegistration` no-ops; Burn still owns ARP |
| Burn | Burn | Uninstall | — | Empty ✅ | Empty ✅ | Burn self-cleans ARP and cache |
| Burn | MSI | Repair | ❌ | Burn ✅ | Burn + MSI ✅ | MSI re-verifies files; no ARP change; Burn still owns visible ARP |
| Burn | MSI | Uninstall | — | Empty ✅ | Empty ✅ | `CleanupBurnRegistration` → Burn `/uninstall /quiet` detached; Burn cleans ARP + cache after mutex released |
| MSI | MSI | Repair | ❌ | Hidden ✅ | MSI ✅ | No Burn involved; MSI ARP has `SystemComponent=1` (invisible to user) |
| MSI | MSI | Uninstall | — | Empty ✅ | Empty ✅ | No Burn ARP to clean; `CleanupBurnRegistration` exits early (no match) |
| MSI | Burn | Repair | ❌ | Burn ✅ | Burn + MSI ✅ | Burn writes its visible ARP; MSI ARP gains `SystemComponent=1` via Burn's `ARPSYSTEMCOMPONENT` variable |
| MSI | Burn | Uninstall | — | Empty ✅ | Empty ✅ | Burn self-cleans |

**Package Cache — "Burn + MSI" means two entries:**
- `{MSI-ProductCode}\DIME-64bit.msi` (~3.5 MB) — Burn's cached payload
- `{Bundle-ProductCode}\DIME-Universal.exe` (~1 MB) — Windows Installer's cached source for the bundle EXE

### MajorUpgrade (new version over old)

| Old installer | New installer | DIME.dll updated | Visible ARP | Package Cache | Notes |
|---|---|:---:|---|---|---|
| Burn | Burn | ✅ | Burn (new) ✅ | Burn + MSI (new) ✅ | `INSTALLEDBYBURN=1` → CA no-ops; Burn updates its own ARP in-place |
| Burn | MSI | ✅ | Hidden ✅ | MSI (new) ✅ | `WIX_UPGRADE_DETECTED` → `CleanupBurnRegistration` → old Burn `/uninstall /quiet` detached; old Burn cache cleaned after mutex |
| MSI | Burn | ✅ | Burn (new) ✅ | Burn + MSI (new) ✅ | Old MSI removed by `RemoveExistingProducts`; Burn writes new visible ARP; `INSTALLEDBYBURN=1` prevents CA double-fire |
| MSI | MSI | ✅ | Hidden ✅ | MSI (new) ✅ | `WIX_UPGRADE_DETECTED` triggers CA; no Burn ARP exists; CA finds no `BundleUpgradeCode` match and exits cleanly |

### Downgrade (old version over new — user-confirmed)

⚠️ Messagebox confirmed working. DLL presence after downgrade pending retest after `REINSTALLMODE=amus` fix.

| Old installer | New installer | User confirms? | DIME.dll updated | Visible ARP | Package Cache | Notes |
|---|---|:---:|:---:|---|---|---|
| Burn | Burn | Yes | ⏳ | Burn (old) ⏳ | Burn + MSI (old) ⏳ | `ConfirmDowngrade` messagebox ✅; `REINSTALLMODE=amus` set ✅; DLL presence needs retest |
| Burn | MSI | Yes | ⏳ | Hidden ⏳ | MSI (old) ⏳ | Same |
| MSI | MSI | Yes | ⏳ | Hidden ⏳ | MSI (old) ⏳ | MSI engine removes newer; CA finds no Burn ARP; DLL presence needs retest |
| MSI | Burn | Yes | ⏳ | Burn (old) ⏳ | Burn + MSI (old) ⏳ | Same; Burn writes new visible ARP |
| any | any | No | ❌ | Unchanged ✅ | Unchanged ✅ | `ConfirmDowngrade` returns 1602 (user cancel); nothing touched |

*Visible ARP "Hidden" = MSI ARP exists with `SystemComponent=1`; not shown in Settings → Apps.*  
*⏳ = Mechanism implemented and partially verified; full DLL-presence test pending rebuild after `REINSTALLMODE=amus` fix.*

---

## CRT Linkage — No Redistributable Required

All DIME binaries (`DIME.dll`, `DIMESettings.exe`) are built with the **static
CRT** (`/MT` — Multi-threaded, not DLL). The Visual C++ runtime is linked
directly into each binary rather than loaded from `VCRUNTIME140.dll` /
`MSVCP140.dll` at runtime.

### Why `/MT` is safe for DIME

DIME.dll is a COM in-process server. Callers communicate exclusively through COM
interfaces (`IUnknown*`, `HRESULT`, `BSTR`, `ITfXxx*`). No CRT-managed objects
(heap pointers, `FILE*`, `std::string`) cross the DLL boundary, so the
separate-heap constraint of `/MT` does not apply.

### Benefits

- No VC++ Redistributable prerequisite — `DIME-64bit.msi`, `DIME-32bit.msi`, and `DIME-Universal.exe`
  install on any Windows 10/11 machine without additional runtime packages
- `DIME-Universal.exe` (Burn bundle) does not need a VC++ Redistributable
  `<ExePackage>` at all
- Eliminates runtime version mismatch issues entirely

### Verification

After building, confirm no CRT DLL dependency:

```powershell
dumpbin /dependents src\Release\x64\DIME.dll
```

`VCRUNTIME140.dll` and `MSVCP140.dll` must **not** appear in the output.

### Visual Studio setting

**Project Properties → C/C++ → Code Generation → Runtime Library**:
set to `Multi-threaded (/MT)` for all Release configurations (x64, ARM64, Win32).

---

## Build Targets

| Project | Output | Purpose |
|---|---|---|
| `DIMEInstallerCA.vcxproj` (x64) | `bin\x64\DIMEInstallerCA_x64.dll` | Embedded in `DIME-64bit.msi` |
| `DIMEInstallerCA.vcxproj` (Win32) | `bin\x86\DIMEInstallerCA_x86.dll` | Embedded in `DIME-32bit.msi` |

Both are built from the same `DIMEInstallerCA.cpp` source.

Build command:
```powershell
$msbuild = "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
& $msbuild installer\wix\DIMEInstallerCA\DIMEInstallerCA.vcxproj /p:Configuration=Release /p:Platform=x64 /t:Rebuild
& $msbuild installer\wix\DIMEInstallerCA\DIMEInstallerCA.vcxproj /p:Configuration=Release /p:Platform=Win32 /t:Rebuild
```

Full installer build (requires DIME binaries in `src\Release\`):
```powershell
.\installer\deploy-wix-installer.ps1
```

---

## Tests

All installer scenarios are covered by an automated PowerShell test suite:
`installer\wix\Test-DIMEInstaller.ps1`

### Test Script Overview

The script requires Administrator rights and accepts these key parameters:

| Parameter | Default | Purpose |
|---|---|---|
| `-OldVersion` | `1.3.477.0` | Version string for the "old" installer |
| `-NewVersion` | `1.3.478.0` | Version string for the "new" installer |
| `-TestWorkDir` | `.` | Working folder for built installers and logs |
| `-NsisExe` | `..\DIME-NSIS-Universal.exe` | Optional: path to legacy NSIS installer (enables Scenario B) |
| `-Scenarios` | all | Subset to run: `A,B,C,D,E,F,G,H,I,Matrix` |
| `-StopOnFail` | off | Abort on first FAIL |
| `-Rebuild` | off | Force rebuild even if cached installers are fresh |
| `-Cleanup` | off | Remove all DIME installs and exit |

**Build phase:** the script calls `deploy-wix-installer.ps1` with `-ProductVersionOverride` to build both the old and new installer pairs (`DIME-Universal-{ver}.exe` + `DIME-64bit-{ver}.msi`) using the real production build pipeline. A source-staleness check (comparing `.wxs`, `deploy-wix-installer.ps1`, `DIMEInstallerCA.cpp`, and `.h` file timestamps against the cached MSI) forces a rebuild when any source is newer than the cached output.

**Framework:** each test calls `Assert-True`, `Assert-FileExists`, `Assert-FileAbsent`, `Assert-RegKeyAbsent`, etc. Results are accumulated in `$Script:Results` and printed as a `PASS/FAIL/WARN` summary at the end. `-StopOnFail` aborts on the first failure.

**Install methods tested:** every scenario that involves installing or uninstalling exercises both `Install-ViaBurn` (Burn bundle, `/install /quiet /norestart`) and `Install-ViaMsi` (msiexec, `/qn /norestart`) unless noted.

**Cleanup:** `Remove-AllDimeInstalls` runs before and after each scenario. It uninstalls all DIME MSI products by deterministic `ProductCode`, sweeps orphan MSI ARP entries, sweeps Burn bundle ARP entries and Package Cache, removes COM/TSF registry keys from both 64-bit and 32-bit hives, and removes any orphan `Dependents` GUIDs whose bundle is no longer in the ARP hive.

**Timing regression detection:** `Assert-Duration` (120 s limit) catches any operation that stalls — e.g. a missing `MsiRestartManagerControl=Disable` causes a ~60 s RmGetList stall inside InstallValidate.

### Assertions — Installed State

After every successful install, `Assert-InstalledClean` verifies:

1. Arch-correct DLL files present under `%ProgramFiles%\DIME\amd64\` (AMD64) or `arm64\` (ARM64), plus `x86\` always
2. 64-bit COM `InProcServer32` points to the arch-correct DLL (`Registry64` view)
3. 32-bit COM `InProcServer32` points to `x86\DIME.dll` (`Registry32`/WOW6432Node view)
4. Four 64-bit TSF TIP language profiles present (Dayi, Array, Phonetic, Generic)
5. Four 32-bit TSF TIP language profiles present (WOW6432Node view)
6. MSI ARP entry — exactly one, no `BundleVersion` (Burn ARP absent)
7. Burn ARP absent (skipped for Burn-to-Burn upgrade — Burn re-registers post-CA by design)
8. Package Cache absent (skipped for Burn-to-Burn upgrade)
9. Side-channel key `HKLM\SOFTWARE\DIME\BurnCacheCleanup` absent
10. `InstalledVersion` value present under `HKLM\SOFTWARE\DIME`
11. `InstallerState` scratch key absent
12. No orphan `.bak` files in `%ProgramFiles%\DIME\` subdirectories
13. All eleven `.cin` table files present
14. `DIMESettings.exe` present
15. Start Menu shortcut `DIME設定.lnk` present with correct target

### Assertions — Uninstalled State

`Assert-UninstalledClean` verifies all DLL files, COM keys (both hives), TSF TIP keys (both hives), MSI ARP, Burn ARP, Package Cache, side-channel key, `HKLM\SOFTWARE\DIME` (entire key), `InstallerState`, orphan baks, the `%ProgramFiles%\DIME` directory, and the Start Menu shortcut are all absent.

### Scenarios

| Scenario | Description | Sub-cases |
|---|---|---|
| **A** | **Fresh install** — no prior DIME present | A1: via Burn bundle; A2: via MSI directly |
| **B** | **NSIS migration** — WiX install over legacy NSIS | B1: Burn bundle triggers `ConfirmAndRemoveNsisInstall`; verifies NSIS ARP key and `System32\DIME.dll` removed |
| **C** | **Major upgrade** — all 4 install-method combos (old→new) | BurnToBurn, BurnToMSI, MSIToBurn, MSIToMSI; Burn-to-Burn skips Burn ARP / Package Cache assertions (Burn re-registers post-CA by design) |
| **D** | **Uninstall + reinstall + rapid cycling** | Single cycle (Burn and MSI); reinstall after uninstall; 3× rapid install/uninstall loop to stress-test `BuildUniqueBakName` tick suffix |
| **E** | **Install failure rollback** — exclusive DLL lock forces MSI exit 1603 | E-MsiRestartManagerControl: Property table sanity check (no install); E1: fresh-install rollback with `DisableAutomaticApplicationShutdown=1` policy; E2: upgrade rollback; verifies rollback leaves no files/keys |
| **F** | **Downgrade** | F1: `/quiet` blocks downgrade (exit 1602); F2×2: downgrade via MSI with `DIME_DOWNGRADE_APPROVED=1`, for both Burn and MSI original install |
| **G** | **Locked DLL and CIN during upgrade** | G1: DLL held with `FILE_SHARE_READ\|WRITE\|DELETE` (loader flags) — rename+copy-back succeeds; G2: CIN held similarly — same mechanism; G3: CIN held without `FILE_SHARE_DELETE` — canary; warns if exit 3010 (known OS limitation) |
| **H** | **Uninstall with DLL locked (RM regression)** | All 4 install×uninstall combos with DLL open in permissive sharing; `Assert-Duration` (120 s limit) catches `MsiRestartManagerControl` missing, which causes ~60 s RmGetList stall |
| **I** | **Stale Dependent injection (error 2803 regression)** | Pre-injects fake GUID `{DEAD0000-...}` into `{ProductCode}\Dependents`; install must clean it via `CleanupBurnRegistration`; Burn uninstall then succeeds; 2 combos: Burn install and MSI install |
| **Matrix** | **Same-version repair + cross-method uninstall** | All 4 install×maintenance combos; MSI-to-Burn cross-repair warns on expected 1603 (SecureRepair filename mismatch — known limitation); cross-method uninstall fully asserted |

### Test Results

Final run (v1.3.478.0 vs v1.3.477.0, Windows 11 x64): **118/118 PASS**

Notable issues encountered and resolved during development:

| Issue | Root cause | Fix |
|---|---|---|
| E1/E2 rollback: child lock-holder killed by RM | Server-side RM session (msiserver, Session 0) ignores `MsiRestartManagerControl=Disable` in /qn | Set `DisableAutomaticApplicationShutdown=1` machine policy around E1/E2; restore in `finally` |
| E1/E2 rollback: `Remove-Item` fails — file still present | OS needs time to release kernel handle after `Kill()` | Add `WaitForExit(3000)` after `Kill()` before `Remove-Item` |
| G1/G2/G3: test script silently killed by RM | Lock held in test runner process itself — RM killed it | Move lock to child `powershell.exe` process via `Start-ExclusiveLockChild` |
| I: stale GUID still present after install | `CleanupBurnRegistration` called `MsiGetPropertyW("ProductCode")` which returns empty in commit CA context | Pass `[ProductCode]` via `CustomActionData` using a setter CA `SetCleanupBurnRegistrationData`; read `CustomActionData` in the C++ |
