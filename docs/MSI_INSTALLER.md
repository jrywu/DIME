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
| `CleanupBurnRegistration` | commit | no (SYSTEM) | Detect orphaned Burn ARP after MSI-only uninstall or MajorUpgrade; launch Burn `/uninstall /quiet` detached to self-clean |

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
| 1 | CA: `CleanupBurnARPEarly` | unconditional | no Burn ARP — exits | no Burn ARP — exits | finds Burn ARP (if installed via Burn); deletes registry entry only | finds old-ver Burn ARP; deletes registry only | finds newer-ver Burn ARP; deletes registry only |
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
| 14 | commit | CA: `CleanupBurnRegistration` | `NOT UPGRADINGPRODUCTCODE` | no match — exits | no match — exits | deletes Burn ARP registry + Package Cache⁸ | deletes old-ver Burn ARP + cache⁸ | deletes newer-ver Burn ARP + cache⁸ |

**On failure** at any step after a `MoveLockedDimeDll` rename: the armed rollback CA (`RollbackMoveLockedDimeDllInstall` or `RollbackMoveLockedDimeDllUninstall`) renames every bak back to the original path and deletes `InstallerState`. The commit CA never runs (commit phase not reached), so baks are left as plain files.

¹ `ConfirmDowngrade` has no WXS `Condition` attribute — it always fires and checks internally.  
² `ParseVersionStr(installing) >= ParseVersionStr(installed)` → not a downgrade → returns `ERROR_SUCCESS` immediately.  
³ Burn passes `REBOOT=ReallySuppress` to the MSI; a file that cannot be deleted after the wait would return error 1603, aborting the install. In practice TSF unloads DLLs between input sessions so the lock window is bounded.  
⁴ `ScrubPendingFileRenameOperation` — removes any `PendingFileRenameOperations` entry for this DLL path left by a prior failed install, preventing the OS from deleting the freshly installed DLL on the next reboot.  
⁵ `ConfirmAndRemoveNsisInstall` already cleared the sys32/syswow64 DLLs via NSIS; the `%ProgramFiles%\DIME\` paths do not yet exist on first WiX install.  
⁶ `InstallerState` is empty; glob-scan of known DLL directories finds no orphan baks.  
⁷ 4-step cascade per bak: `DeleteFileW` (immediate if no process holds it) → `FileDispositionInfoEx + POSIX_SEMANTICS` (Win10 1709+: removes directory entry instantly while inode stays alive until last handle closes) → `FileDispositionInfo` delete-pending (older OS: auto-deletes on last handle close) → `MOVEFILE_DELAY_UNTIL_REBOOT` (last resort). Also glob-scans for orphan baks from prior interrupted cycles.  
⁸ `CleanupBurnARPEarly` (UISequence, step 1) deleted the registry entry; `CleanupBurnRegistration` (commit) reads the `BundleCachePath` registry value and deletes the cache folder **in the same scan** — so `CleanupBurnRegistration` must find the registry entry intact to clean the cache. For scenarios where the Early CA ran first on the same entry, the cache folder cleanup falls to `CleanupBurnRegistration`'s glob-scan of the Package Cache directory.

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

## TODO

### High priority — implement now

- [x] **Rewrite `DIMEInstallerCA.cpp`**
  - Removed `#ifdef BUILDING_EXE` / `wWinMain` block (EXE target eliminated)
  - Removed `NotifyDimeDllInUseImpl`, `NotifyDimeDllInUseInstall`, `NotifyDimeDllInUseUninstall`
  - Removed `IsFileLocked`, `IsDimeDllStillPresent`, `CheckDimeDllPendingRemoval`
  - Removed `BuildBakName` (replaced)
  - Added `BuildUniqueBakName(dll, ver, out, cch)` — suffix `{ver}.{GetTickCount64() as 16-char hex}`
  - Added `WriteInstallerState(valueName, path)` / `ReadAllInstallerState()` / `DeleteInstallerState()`
    targeting `HKLM\SOFTWARE\DIME\InstallerState`
  - Rewrote `ConfirmAndRemoveNsisInstall` — silent: rename + copy-back + launch `/S` + write InstallerState
  - Rewrote `MoveLockedDimeDll` — rename only; write bak paths to InstallerState; no mark-delete
  - Rewrote `RollbackMoveLockedDimeDll` — read InstallerState; rename bak → dll; delete key
  - Added `CleanupDimeDllBak` — commit CA; read InstallerState; 4-step delete-pending per bak:
    `DeleteFileW` → `FileDispositionInfoEx` + `POSIX_SEMANTICS` (Win10 1709+, removes dir entry
    immediately) → `FileDispositionInfo` fallback (older OS, auto-deletes on last handle close)
    → `MOVEFILE_DELAY_UNTIL_REBOOT` (last resort); glob-scan stale baks in System32, SysWOW64,
    and ProgramFiles\DIME subdirs; delete key
  - `ConfirmDowngrade`: reads `WIX_UPGRADE_DETECTED`; compares versions via `ParseVersionStr`;
    sets `REINSTALLMODE=amus` and shows Chinese messagebox for downgrades; returns immediately
    for upgrades and fresh installs

- [x] **Update `DIME-64bit.wxs`** — DLL relocation to ProgramFiles
  - Removed System64Folder and SystemFolder DIME.dll components
  - Added `%ProgramFiles%\DIME\amd64\DIME.dll`, `arm64\DIME.dll`, `x86\DIME.dll` components
  - Updated regsvr32 / unregsvr32 CA paths
  - Removed `NotifyDimeDllInUseInstall` / `NotifyDimeDllInUseUninstall` SetProperty + CA + sequence entries
  - Removed `CheckDimeDllPendingRemoval` SetProperty + CA + sequence entries
  - Added `CleanupDimeDllBakInstall` and `CleanupDimeDllBakUninstall` commit CAs
  - Add `MediaTemplate EmbedCab="yes"` (already done)
  - `MoveLockedDimeDllInstall` scheduled `Before="InstallFiles"` (not before `RemoveExistingProducts` —
    that caused MSI error 2613)

- [x] **Update `DIME-32bit.wxs`** — same changes as DIME-64bit.wxs (single x86 DLL only)
  - Added `<Launch Condition="NOT VersionNT64" .../>` to block wrong-platform installs
  - `MoveLockedDimeDllInstall` scheduled `Before="InstallFiles"`

- [x] **Update `Bundle.wxs`**
  - Removed `<ExePackage>` for DIMEConfirm
  - Removed 1602 exit-code cancel mapping
  - Removed VC++ Redistributable `<ExePackage>` (not needed — DIME uses static CRT)
  - `ARPSYSTEMCOMPONENT` suppresses MSI ARP entry when installed via Burn

- [x] **Update `deploy-wix-installer.ps1`**
  - Staging copies DLLs to `Staging\amd64\`, `Staging\arm64\`, `Staging\x86\`
  - DIMEConfirm build step removed
  - `Start-Service msiserver` added before WiX build commands

- [x] **Switch DIME.dll and DIMESettings.exe to static CRT (`/MT`)**
  - Set **C/C++ → Code Generation → Runtime Library** to `Multi-threaded (/MT)` in
    `DIME.vcxproj` for all Release configurations (x64, ARM64, Win32)
  - Safe for a COM in-process server: no CRT objects cross the DLL boundary
  - Eliminates the VC++ Redistributable as an install prerequisite
  - Verify with `dumpbin /dependents DIME.dll` — `VCRUNTIME140.dll` must not appear

- [x] **Build and test both CA DLLs** (x64 and Win32)

### Medium priority — verify after implementation

- [x] **Test Scenario B** — fresh WiX install over old NSIS installation
  (requires machine with NSIS DIME installed; verify NSIS removes cleanly and
  silently, WiX DLLs land in ProgramFiles)
- [x] **Test Scenario C** — WiX upgrade (install v2 over v1 WiX)
- [x] **Test Scenario D** — uninstall + reinstall same version
- [x] **Test Scenario E** — install failure rollback (kill installer after
  `MoveLockedDimeDll` but before `InstallFiles`; verify DLL is restored)
- [x] **Test Scenario F** — downgrade (install older over newer — rebuild required after `REINSTALLMODE=amus` fix):
  - Click No — verify nothing changes on disk ✅ (confirmed via log)
  - Click Yes — verify messagebox fires ✅; verify DLLs present + correct version ⏳
  - Verify ARP and package cache after downgrade ⏳
- [x] **Test rapid cycling** — uninstall/install three times without rebooting;
  verify no bak naming collision and no orphan files in System32 or ProgramFiles

### Low priority — future

*(none)*
