# DIME MSI Installer Test Plan

## Goals

`installer/wix/Test-DIMEInstaller.ps1` is a fully automated PowerShell test harness with two non-negotiable core goals:

1. **Install, upgrade, and downgrade lay down everything completely** — after any fresh install, major upgrade, or forced downgrade, every expected file (arch-correct DLLs, CIN tables), COM registration (64-bit and 32-bit hive), and TSF TIP profile registration (64-bit and 32-bit hive) is present and correct at the correct version, and the MSI ARP entry reflects the newly installed version. Nothing is missing, nothing is stale from the previous version.

2. **Uninstall removes everything entirely** — after any uninstall (whether of a freshly installed, upgraded, or downgraded product), every file, every registry key (COM, TSF, ARP, DIME software hive, InstallerState, Burn side-channel), and every Package Cache artefact is gone. No orphan files, no orphan registry keys, no artefacts from any previously installed version.

**Both goals must be achieved without a reboot.** Every installer invocation asserts exit code 0 and that no `DIME.dll` path appears in `PendingFileRenameOperations`. Exit `1641` (reboot initiated) in any scenario is always a regression. Exit `3010` (reboot required but not initiated) is also a regression *except* for Scenario G3 (CIN locked with `FileShare.Read` only — no `FILE_SHARE_DELETE`), which is a documented OS-level limitation and is treated as WARN rather than FAIL.

The "old" and "new" test installers are built by the test script itself via two calls to `deploy-wix-installer.ps1` with injected version strings (`1.3.477.0` old, `1.3.478.0` new), ensuring the test pipeline is 100% identical to real releases.

---

## Critical Design Fact: Burn ARP Is Always Absent After Any Install

`CleanupBurnARPEarly` is scheduled `Before="FindRelatedProducts"` in the MSI UISequence **with no Condition attribute** — it runs on EVERY MSI invocation, regardless of whether the MSI was launched by Burn or directly.

`CleanupBurnRegistration` is scheduled `Before="InstallFinalize"` in the commit action sequence (`NOT UPGRADINGPRODUCTCODE`) — it runs in the commit phase of every successful install.

Consequence:
- **Burn ARP** (`BundleVersion` present + `Publisher=="Jeremy Wu"` + `DisplayName` contains "DIME" in 32-bit Uninstall hive) is **ALWAYS absent** after any successful install or uninstall, regardless of install method
- **Package Cache folder** is **ALWAYS absent** after any successful install or uninstall
- **Side-channel key** `HKLM\SOFTWARE\DIME\BurnCacheCleanup` is **ALWAYS absent** after commit (read and deleted by `CleanupBurnRegistration`)
- **MSI ARP** (`HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{ProductCode}`) is the **ONLY remaining ARP entry** after install; absent after uninstall

The distinction "installed via Burn → Burn ARP present" vs "installed via MSI → Burn ARP absent" is INCORRECT. Burn ARP is unconditionally removed by the CA on every install, including Burn-initiated installs (Burn writes its ARP at Apply-start; the MSI CA removes it during the MSI commit phase).

There is NO `Assert-BurnArpPresent` function. Only `Assert-BurnArpAbsent` and `Assert-PackageCacheAbsent` exist, embedded directly into `Assert-InstalledClean`.

---

## Build Strategy

`deploy-wix-installer.ps1` supports a parameterised test mode via four optional parameters:

| Parameter | Type | Purpose |
|---|---|---|
| `-ProductVersionOverride` | `[string]` | Skip BuildInfo.h parsing; use this exact version string |
| `-OutputDir` | `[string]` | Write MSI/EXE to this directory instead of `$PSScriptRoot` |
| `-NonInteractive` | `[switch]` | Suppress all `Read-Host` prompts |
| `-SkipChecksumUpdate` | `[switch]` | Skip ZIP + README checksum update step |

The test script calls `deploy-wix-installer.ps1` twice with version overrides `1.3.477.0` (old) and `1.3.478.0` (new), writing outputs to separate temp directories. Same DIME.dll binary but two distinct ProductCode GUIDs (from `Get-DeterministicGuid` with different version seeds), so the MSI engine sees them as different products and all MajorUpgrade/ConfirmDowngrade logic fires normally.

`-SkipCopyArtifacts` is **NOT added** — the test build always copies fresh binaries from `src\Release\` exactly as a real release does.

---

## Key Constants

### MSI / installer
- 64-bit UpgradeCode: `{6F8A4D2E-1B3C-5E7F-9A0B-2D4F6C8E0A1B}` (DIME-64bit.wxs)
- 32-bit UpgradeCode: `{7A9B1C3D-2E4F-6A8B-0C1E-3F5A7B9C1D3F}` (DIME-32bit.wxs)
- DLL install paths: `%ProgramFiles%\DIME\{amd64,arm64,x86}\DIME.dll`
- InstallerState registry: `HKLM\SOFTWARE\DIME\InstallerState`
- InstalledVersion registry: `HKLM\SOFTWARE\DIME` → `InstalledVersion` value
- NSIS ARP key: `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\DIME`
- PendingFileRenameOperations: `HKLM\SYSTEM\CurrentControlSet\Control\Session Manager` → `PendingFileRenameOperations`

### DIME CLSIDs (from Globals.cpp)
- DIME COM CLSID: `{1DE68A87-FF3B-46A0-8F80-46730B2491B1}`
- Dayi profile GUID: `{36851834-92AD-4397-9F50-800384D5C24C}`
- Array profile GUID: `{5DFC1743-638C-4F61-9E76-FCFA9707F450}`
- Phonetic profile GUID: `{26892981-14E3-447B-AF2C-9067CD4A4A8A}`
- Generic profile GUID: `{061BEEA7-FFF9-420C-B3F6-A9047AFD0877}`
- Language ID: `0x00000404` (Traditional Chinese)

### Registry keys written by regsvr32

**COM 64-bit hive** (64-bit regsvr32, amd64 or arm64 DLL):
`HKLM\SOFTWARE\Classes\CLSID\{1DE68A87...}\InProcServer32` (default) = arch DLL path

**COM 32-bit hive** (SysWOW64\regsvr32, x86 DLL; must read via `RegistryView.Registry32`):
`HKLM\SOFTWARE\WOW6432Node\Classes\CLSID\{1DE68A87...}\InProcServer32` (default) = x86 DLL path

**TSF TIP 64-bit hive**:
`HKLM\SOFTWARE\Microsoft\CTF\TIP\{1DE68A87...}\LanguageProfile\0x00000404\{profile-guid}\` x 4

**TSF TIP 32-bit hive** (must read via `RegistryView.Registry32`):
`HKLM\SOFTWARE\WOW6432Node\Microsoft\CTF\TIP\{1DE68A87...}\LanguageProfile\0x00000404\{profile-guid}\` x 4

**MSI ARP entry**: `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{ProductCode}` — `DisplayName` contains "DIME", `Publisher` == "Jeremy Wu"

**Burn ARP**: always absent after any successful install or uninstall (see Critical Design Fact above)

### Architecture-specific assertions
- **AMD64 machine:** `amd64\DIME.dll` present; `arm64\DIME.dll` ABSENT; `x86\DIME.dll` present; 64-bit COM → amd64 path; 32-bit COM → x86 path
- **ARM64 machine:** `amd64\DIME.dll` ABSENT; `arm64\DIME.dll` present; `x86\DIME.dll` present; 64-bit COM → arm64 path; 32-bit COM → x86 path
- **After uninstall:** all DLLs, COM keys, TSF keys ABSENT

### Burn ARP fingerprint (for `Assert-BurnArpAbsent`)
Scan 32-bit Uninstall hive via `RegistryView.Registry32` for any subkey where ALL three hold:
1. `BundleVersion` value is present (non-null)
2. `Publisher` == `"Jeremy Wu"`
3. `DisplayName` contains `"DIME"`

After any successful install or uninstall: expects 0 matching entries.

### Package Cache / side-channel key
- Side-channel key: `HKLM\SOFTWARE\DIME\BurnCacheCleanup` — absent after commit
- Package Cache folder: absent after commit (sync-deleted by `CleanupBurnRegistration`)

### DLL / CIN file locking simulation

**Permissive lock** (G1, G2): `FileShare.ReadWrite | FileShare.Delete` — exactly matches the Windows OS loader handle flags (`FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE`). With this handle: `MoveLockedDimeDll`'s `MoveFileExW` SUCCEEDS; bak is sync-deleted by `CleanupDimeDllBak`; no reboot required. This is also how Notepad, VS Code, and all modern editors open files.

**Restrictive lock** (G3): `FileShare.Read` only — no `FILE_SHARE_DELETE`. Simulates apps using the .NET `FileStream` default, `fopen("r")` in C, or older editors that withhold delete sharing. `MoveFileExW` requires `FILE_SHARE_DELETE` to rename; without it, MSI falls back to `MOVEFILE_DELAY_UNTIL_REBOOT` and exits `3010`. This is an OS-level constraint, unavoidable at the application level.

---

## Script Parameters

| Parameter | Type | Purpose |
|---|---|---|
| `$NsisExe` | `[string]` | Optional path to legacy NSIS DIME-*.exe (enables Scenario B) |
| `$Scenarios` | `[string[]]` | Filter to specific scenarios: A, B, C, D, E, F, G, Matrix (default: all) |
| `$StopOnFail` | `[switch]` | Abort on first FAIL |
| `$KeepBuilds` | `[switch]` | Don't delete temp old/new installers after run |

The deploy script is located at `"$PSScriptRoot\..\..\deploy-wix-installer.ps1"` (two levels up from `installer/wix/`). Native OS architecture is detected from `HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment\PROCESSOR_ARCHITECTURE` (not the WOW64-remapped `$env:PROCESSOR_ARCHITECTURE`).

---

## Result Reporting — Three Tiers

Each assertion records `Passed=$true` (PASS), `Passed=$false` (FAIL), or `Passed=$null` (WARN).

- **PASS** (`$true`): assertion succeeded — green `[PASS]`
- **WARN** (`$null`): known OS-level limitation, not a regression — yellow `[WARN]`; shown in summary but does not set exit code 1
- **FAIL** (`$false`): regression — red `[FAIL]`; shown in "Failed assertions" section; sets exit code 1

Summary line format: `Results: N/T PASS, F FAIL, W WARN`

`-not $_.Passed` is `$true` for both `$false` and `$null` in PowerShell, so the summary must use explicit `-eq $false` / `-eq $null` / `-eq $true` comparisons to distinguish FAILs from WARNs.

---

## Assert Functions

### Framework (`Assert-True`)
All atomic and composite asserts call `Assert-True($condition, $name, $failMsg)` which records the result and optionally throws on `$StopOnFail`.

### Atomic asserts
- `Assert-FileExists($path, $ctx)` / `Assert-FileAbsent($path, $ctx)`
- `Assert-RegKeyExists($key, $ctx)` / `Assert-RegKeyAbsent($key, $ctx)`
- `Assert-RegValueExists($key, $valueName, $ctx)`
- `Assert-ExitCodeSuccess($code, $ctx)` — asserts `$code -eq 0`; error message names both `3010` (reboot required) and `1641` (reboot initiated) as regressions

### `Assert-NoPendingReboot($ctx)`
Checks `PendingFileRenameOperations` for any entry matching `DIME\.dll$` (canonical path, not bak). Bak entries like `DIME.dll.1.3.477.0.ABCDEF` are transient during the commit window and are not checked here.

### Registry view helpers
64-bit PowerShell does NOT auto-remap the 32-bit COM hive. `Get-RegValue64` / `Get-RegValue32` and `Test-RegKey64` / `Test-RegKey32` open the correct hive view explicitly via `Microsoft.Win32.RegistryKey.OpenBaseKey($hive, $view)`.

### `Assert-InstalledClean($expectedVer, $ctx)`
Arch-aware composite assert. Checks in order:
1. DLL file presence: amd64 or arm64 (native arch only), x86 always present
2. 64-bit COM `InProcServer32` pointing to arch-correct DLL
3. 32-bit COM `InProcServer32` (via Registry32) pointing to x86 DLL
4. TSF TIP 64-bit profiles x 4 GUIDs
5. TSF TIP 32-bit profiles x 4 GUIDs (via Registry32)
6. MSI ARP: exactly 1 entry (`BundleVersion` absent, `Publisher == "Jeremy Wu"`)
7. Burn ARP: always absent (`Assert-BurnArpAbsent`)
8. Package Cache: always absent (`Assert-PackageCacheAbsent`)
9. Side-channel key `SOFTWARE\DIME\BurnCacheCleanup` absent
10. `InstalledVersion` value matches `$expectedVer`
11. `InstallerState` key absent
12. No orphan bak files (`Assert-NoOrphanBaks`)

### `Assert-UninstalledClean($ctx)`
Checks:
- All three DLLs absent (amd64, arm64, x86)
- 64-bit and 32-bit COM keys absent
- 64-bit and 32-bit TSF TIP keys absent
- MSI ARP: 0 matching entries
- Burn ARP absent; Package Cache absent; side-channel key absent
- `SOFTWARE\DIME\InstalledVersion` absent; `InstallerState` absent
- No orphan baks
- `%ProgramFiles%\DIME` directory entirely absent (`CleanupDimeDllBak` removes arch subdirs then the parent)

### `Assert-BurnArpAbsent($ctx)`
Scans 32-bit Uninstall hive via `RegistryView.Registry32`; asserts 0 entries matching the Burn ARP fingerprint (BundleVersion present + Publisher "Jeremy Wu" + DisplayName contains "DIME").

### `Assert-PackageCacheAbsent($ctx)`
Scans `%ProgramData%\Package Cache` recursively for any subdirectory named `*DIME*` or containing a `DIME-*.exe`. Asserts 0 matches.

### `Assert-InstallerStateClean($ctx)`
Asserts `HKLM\SOFTWARE\DIME\InstallerState` is absent.

### `Assert-NoOrphanBaks($ctx)`
- DLL baks (`DIME.dll.*`): checked in `%ProgramFiles%\DIME\{amd64,arm64,x86}`, `%SystemRoot%\System32`, `%SystemRoot%\SysWOW64`
- CIN baks (`*.cin.*`): checked in `%ProgramFiles%\DIME` root only (`CleanupCinBaksInDir` uses `*.cin.*` in that directory)

---

## Installer Helpers

All five helpers include `/norestart` to suppress `InitiateSystemShutdown` (prevents the test machine from rebooting). Each writes a timestamped log to `%TEMP%\DIMETest\logs\`.

- `Install-ViaBurn($exePath)` — `DIME-Universal.exe /install /quiet /norestart /log ...`
- `Install-ViaMsi($msiPath)` — `msiexec /i ... /qn /norestart /l*v ...`
- `Repair-ViaMsi($productCode)` — `msiexec /fvecmus ... /qn /norestart /l*v ...`
- `Uninstall-ViaBurn($exePath)` — `DIME-Universal.exe /uninstall /quiet /norestart /log ...`
- `Uninstall-ViaMsiProductCode($productCode)` — `msiexec /x ... /qn /norestart /l*v ...`

`Remove-AllDimeInstalls` enumerates both 64-bit and 32-bit Uninstall hives for all product codes matching either UpgradeCode, uninstalls each, then force-removes `HKLM\SOFTWARE\DIME` and any orphan DLL baks.

---

## Scenario A — Fresh Install

Two sub-tests, both assert the same post-install state:

- **A1**: Install via Burn (`DIME-Universal.exe /install /quiet`). `Assert-InstalledClean` verifies Burn ARP absent and Package Cache absent — the CA runs unconditionally regardless of install method.
- **A2**: Install directly via MSI. Same `Assert-InstalledClean` composite assert.

Both assert exit code 0 and `Assert-NoPendingReboot`.

---

## Scenario B — NSIS Migration

Skipped if `-NsisExe` not provided.

1. Install legacy NSIS silently (`/S`); verify NSIS ARP key and `System32\DIME.dll` present
2. Install new version via Burn; `ConfirmAndRemoveNsisInstall` CA handles migration
3. Assert: exit 0, `Assert-InstalledClean`, NSIS ARP key absent, `System32\DIME.dll` absent, `Assert-NoPendingReboot`

---

## Scenario C — Major Upgrade (4 combos)

For each of {Burn→Burn, Burn→MSI, MSI→Burn, MSI→MSI}:

1. Install old version (method A); `Assert-InstalledClean($oldVer)`; `Assert-NoPendingReboot`
2. Install new version (method B); assert exit 0; `Assert-InstalledClean($newVer)`; `Assert-NoPendingReboot`; `Assert-NoOrphanBaks`

Burn ARP is always absent after both steps regardless of method. `ConfirmDowngrade` fires on `WIX_UPGRADE_DETECTED` but returns immediately (upgrade direction → no-op).

---

## Scenario D — Uninstall + Reinstall

For each of {Burn, MSI}:

**Single cycle**: install → `Assert-InstalledClean` → uninstall → `Assert-UninstalledClean` → reinstall → `Assert-InstalledClean` → `Assert-NoOrphanBaks`

**Rapid cycling x3**: install → uninstall, repeated 3 times without pause. Specifically targets the unique-tick-suffix bak naming (`GetTickCount64()` as 16-char hex suffix). After 3 cycles: `Assert-UninstalledClean` + `Assert-NoOrphanBaks`. If the tick mechanism breaks, baks would collide and a cycle would fail.

---

## Scenario E — Rollback (File Obstruction)

Goal: verify MSI rolls back completely when `InstallFiles` fails. A read-only placeholder file is created at the arch-correct DLL path before invoking the installer. `InstallFiles` hits `ERROR_ACCESS_DENIED` (exit 1603) and triggers automatic rollback. A `try/finally` block removes the placeholder regardless of outcome so no state leaks into subsequent scenarios.

**E1 — Fresh-install rollback**: system must be entirely clean after rollback. Expected exit code: 1603. `Assert-UninstalledClean` + `Assert-NoPendingReboot`.

**E2 — Upgrade rollback**: old version installed first; new version install fails at `InstallFiles`. `RollbackMoveLockedDimeDll` reads `InstallerState` (`wix_bak_N` and `cin_bak_N` entries) and renames each bak back to the original source path. Expected exit code: 1603. `Assert-InstalledClean($oldVer)` — old DLLs restored from baks, not merely new version absent. `Assert-NoPendingReboot` + `Assert-NoOrphanBaks`.

Notes:
- `ScrubPendingFileRenameOperation` is exercised by the E1 recovery install: a failed install may leave a delete-pending entry for `DIME.dll`; the recovery install's `MoveLockedDimeDll` scrubs it before renaming. `Assert-NoPendingReboot` after the recovery install confirms the scrub worked.
- CA-level rollback of `MoveLockedDimeDll` itself failing is not tested (requires `WixFailWhenDeferred`-style test CA in a separate build) — documented as a known gap.

---

## Scenario F — Downgrade

**F1 — Downgrade blocked** (Burn quiet install of old over new): `ConfirmDowngrade` detects downgrade, UILevel ≤ 2 → returns `ERROR_INSTALL_USEREXIT` (1602). Expected exit code: 1602. `Assert-InstalledClean($newVer)` — new version unchanged. `Assert-NoPendingReboot`.

**F2 — Downgrade forced** (MSI direct with `REINSTALLMODE=amus REINSTALL=ALL`): bypasses the messagebox; `REINSTALLMODE=amus` forces `CostFinalize` to overwrite higher-versioned on-disk files. For each of {Burn, MSI} as the prior install method: assert exit 0, `Assert-InstalledClean($oldVer)`, `Assert-NoPendingReboot`, `Assert-NoOrphanBaks`.

---

## Scenario G — Locked DLL and CIN Upgrade

### G1 — DLL locked with permissive sharing (expect zero reboot)

1. Install old via MSI
2. Open arch-correct DLL with `FileShare.ReadWrite | FileShare.Delete` — matches OS loader flags
3. Install new via MSI (hold handle across child process)
4. Close handle
5. Assert exit 0, DLL present at original path, `Assert-NoPendingReboot`, `Assert-NoOrphanBaks`, `Assert-InstalledClean($newVer)`

`MoveLockedDimeDll` succeeds because `FILE_SHARE_DELETE` is granted. Commit CA sync-deletes the bak. No reboot.

### G2 — CIN locked with permissive sharing (expect no reboot)

1. Install old via MSI
2. Open `Array.cin` with `FileShare.ReadWrite | FileShare.Delete`
3. Install new via MSI (hold handle across child process):
   - `MoveLockedDimeDll` glob-scans `*.cin` in INSTALLDIR; for each: scrub stale pending entry → `MoveFileExW(.cin → .cin.bak)` succeeds (handle shares DELETE) → `CopyFileW(.cin.bak → .cin)` creates fresh unlocked inode → write `InstallerState` entry
   - `InstallFiles` writes new `.cin` to the fresh inode — no lock contention
   - `CleanupDimeDllBak` (commit): marks `.cin.bak` delete-pending; runs `CleanupCinBaksInDir`
4. Close handle
5. Assert exit 0, `Assert-NoPendingReboot`, `Assert-NoOrphanBaks`, `Assert-InstalledClean($newVer)`

### G3 — CIN locked with restrictive sharing (regression canary)

Simulates apps using the .NET `FileStream` default or C `fopen("r")`, which grant `FILE_SHARE_READ` only — no `FILE_SHARE_DELETE`. Real-world apps using `FileShare.ReadWrite|Delete` (Notepad, VS Code, OS loader) are already covered by G2.

1. Install old via MSI
2. Open `Array.cin` with `FileShare.Read` only — `MoveFileExW` cannot rename without `FILE_SHARE_DELETE`
3. Install new via MSI (hold handle across child process)
4. Close handle
5. Result classification:
   - Exit `1641`: **FAIL** — reboot was initiated; `/norestart` should always suppress `InitiateSystemShutdown`
   - Exit `3010`: **WARN** — known OS limitation; `MoveFileExW` rename fails without `FILE_SHARE_DELETE`; MSI falls back to `MOVEFILE_DELAY_UNTIL_REBOOT`; exit code is decided before commit CAs run, making it mathematically unavoidable
   - Exit `0`: PASS
6. After any outcome: check if any `.cin` path remains in `PendingFileRenameOperations` → WARN with `G3-CinPendingReplacement` if found (scrub in `CleanupDimeDllBak` may have missed it)

`CleanupDimeDllBak` glob-scans `%ProgramFiles%\DIME\*.cin` for live (non-bak) CIN files and calls `ScrubPendingFileRenameOperation` on each, protecting newly-installed CINs from being deleted on the next reboot. The scrub runs in the commit phase — after the exit code is already set — so it cannot change the `3010` result but it does protect data integrity.

---

## Test Matrix (Same-Version Repair + Cross-Method Uninstall)

4 installer combos x {repair, uninstall} = 8 sub-tests.

Expected state after any install or repair — Burn ARP and Package Cache are always absent regardless of method:

| Install → Operate Method | Exit Code | MSI ARP | Burn ARP | Package Cache | Reboot |
|---|---|---|---|---|---|
| Burn → Burn | 0 | Present | **Always absent** | **Always absent** | None |
| Burn → MSI | 0 | Present | **Always absent** | **Always absent** | None |
| MSI → Burn | 0 or WARN* | Present | **Always absent** | **Always absent** | None |
| MSI → MSI | 0 | Present | **Always absent** | **Always absent** | None |

**\* MSI→Burn repair known limitation**: Windows Installer SecureRepair records the source filename at install time. When a product was installed from a version-suffixed MSI (e.g. `DIME-64bit-1.3.478.0.msi`) but repaired via Burn (which provides `DIME-64bit.msi` as its payload filename), `SECREPAIR` rejects the filename mismatch with exit code `1603`. This combo is treated as **WARN** in the Matrix repair sub-test. Cross-method *uninstall* (the more important operation) is unaffected and is asserted as PASS.

**Repair sub-test steps**: `Remove-AllDimeInstalls` → install (method A) → `Assert-InstalledClean($ver)` → repair (method B) → assert exit code → `Assert-InstalledClean($ver)` → `Assert-NoPendingReboot` → `Remove-AllDimeInstalls`

**Uninstall sub-test steps**: `Remove-AllDimeInstalls` → install (method A) → `Assert-InstalledClean($ver)` → uninstall (method B) → assert exit 0 → `Assert-UninstalledClean` → `Assert-NoPendingReboot`

---

## Zero-Reboot Policy

Every install/uninstall step asserts:
1. **Exit code 0** — not `3010`, not `1618` (busy), not anything else
2. **`Assert-NoPendingReboot`** — `PendingFileRenameOperations` must not contain any canonical `DIME.dll` path (non-bak); bak entries like `DIME.dll.1.3.477.0.ABCDEF` are transient during the commit window and not checked

Design rationale: DIME uses a rename + copy-back + commit-delete mechanism specifically to avoid deferred file operations. The CA explicitly does NOT call `MoveFileExW(MOVEFILE_DELAY_UNTIL_REBOOT)` for the main DLL. Exit `1641` (reboot initiated) is always a regression — `/norestart` must suppress `InitiateSystemShutdown`. Exit `3010` is a regression everywhere except G3, where it is treated as WARN. `CleanupDimeDllBak` scrubs `PendingFileRenameOperations` for newly-installed `.cin` files after the G3 scenario to prevent post-reboot corruption.

---

## ARP Assertion Summary

After any **successful install** (Burn-launched or MSI-direct):

| ARP / artefact | Expected state | Reason |
|---|---|---|
| MSI ARP `Uninstall\{ProductCode}` | **Present** | WiX wrote it; `BundleVersion` absent |
| Burn ARP (32-bit hive, fingerprinted) | **Always absent** | `CleanupBurnARPEarly` UISequence, no Condition |
| Package Cache folder | **Always absent** | `CleanupBurnRegistration` sync-deletes in commit |
| Side-channel `SOFTWARE\DIME\BurnCacheCleanup` | **Always absent** | Read+deleted by commit CA |

After any **successful uninstall**: MSI ARP, Burn ARP, and Package Cache all absent.

**`Assert-BurnArpPresent` does not exist** — Burn ARP is never present after any installer returns.

---

## CA Coverage Map

| CA function | Sequence | Context | Exercised by | Assertion |
|---|---|---|---|---|
| `CleanupBurnARPEarly` | UISequence, unconditional | User/Burn | All scenarios (fires on every MSI invocation) | `Assert-BurnArpAbsent` |
| `ConfirmDowngrade` (upgrade path) | UISequence, immediate | User | Scenario C (upgrade direction → silent SUCCESS) | Exit code 0 |
| `ConfirmDowngrade` (downgrade/block) | UISequence, immediate | User | Scenario F1 via Burn quiet (UILevel≤2 → 1602) | `Assert-ExitCode 1602` |
| `ConfirmDowngrade` (downgrade/allow + REINSTALLMODE=amus) | UISequence, immediate | User | Scenario F2 via MSI | `Assert-InstalledClean($oldVer)` |
| `ConfirmAndRemoveNsisInstall` | UISequence, immediate | User | Scenario B | NSIS ARP absent; `System32\DIME.dll` absent |
| `MoveLockedDimeDll` (DLL rename+copy-back) | Deferred | SYSTEM | Scenario G1 | DLL present at original path; no baks |
| `MoveLockedDimeDll` (CIN rename+copy-back) | Deferred | SYSTEM | Scenario G2 | `Assert-NoOrphanBaks` |
| `MoveLockedDimeDll` (ScrubPendingFileRenameOperation) | Deferred | SYSTEM | Scenario E1 recovery install | `Assert-NoPendingReboot` |
| `RollbackMoveLockedDimeDll` | Rollback | SYSTEM | Scenario E2 (InstallFiles fails after MoveLockedDimeDll ran) | `Assert-InstalledClean($oldVer)` — old DLLs restored |
| `CleanupDimeDllBak` (delete baks from InstallerState) | Commit | SYSTEM | Scenario G1, G2 | `Assert-NoOrphanBaks` |
| `CleanupDimeDllBak` (orphan glob-scan DLL baks) | Commit | SYSTEM | Scenario D rapid-cycling x3 | `Assert-NoOrphanBaks` |
| `CleanupDimeDllBak` (orphan glob-scan CIN baks) | Commit | SYSTEM | Scenario G2 + D | `Assert-NoOrphanBaks` (`*.cin.*` pattern) |
| `CleanupDimeDllBak` (scrub live CIN pending-rename entries) | Commit | SYSTEM | Scenario G3 (CIN locked without `FILE_SHARE_DELETE`) | WARN if `.cin` still in `PendingFileRenameOperations` after scrub |
| `CleanupDimeDllBak` (RemoveDirectoryW arch subdirs + DIME dir) | Commit | SYSTEM | Scenario D uninstall | `Assert-UninstalledClean` → `ProgramFiles\DIME` absent |
| `CleanupBurnRegistration` (ARP + Package Cache) | Commit | SYSTEM | All scenarios | `Assert-BurnArpAbsent`, `Assert-PackageCacheAbsent` |
| `CleanupBurnRegistration` (side-channel folder read) | Commit | SYSTEM | All Burn-initiated scenarios | Side-channel key absent |

---

## Relevant Files

- `installer/wix/DIME-64bit.wxs` — UpgradeCode `{6F8A4D2E-1B3C-5E7F-9A0B-2D4F6C8E0A1B}`; `CleanupBurnARPEarly` has no Condition attribute
- `installer/wix/DIME-32bit.wxs` — UpgradeCode `{7A9B1C3D-2E4F-6A8B-0C1E-3F5A7B9C1D3F}`
- `installer/wix/Bundle.wxs` — no BundleUpgradeCode; `ARPSYSTEMCOMPONENT=1` hides MSI ARP when Burn installs; MSI overrides with `ARPSYSTEMCOMPONENT=""` when installed directly
- `installer/wix/DIMEInstallerCA/DIMEInstallerCA.cpp` — all 7 exported CAs
- `installer/wix/Test-DIMEInstaller.ps1` — the test harness (implemented per this plan)
- `installer/deploy-wix-installer.ps1` — `Get-DeterministicGuid`, `Set-MsiPackageCode`; supports `-ProductVersionOverride`, `-OutputDir`, `-NonInteractive`, `-SkipChecksumUpdate`
- `src/Globals.cpp` — CLSIDs and profile GUIDs
- `src/Register.cpp` — `RegisterServer`, `RegisterProfiles`, `RegisterCategories`
- `docs/MSI_INSTALLER.md` — canonical reference for CA execution chain and verified scenario matrix

---

## Key Decisions

- Scenario E (rollback) uses a read-only placeholder at the DLL path to trigger `InstallFiles` failure (exit 1603), exercising `RollbackMoveLockedDimeDll` in E2
- Both test installers are built via the real deploy pipeline with injected version strings; same binary, different ProductCode GUIDs → MajorUpgrade fires normally
- Downgrade "Yes" path bypasses the messagebox by installing the old MSI directly with `REINSTALLMODE=amus REINSTALL=ALL`
- Downgrade "No" path verified via Burn quiet install (returns 1602 from `ConfirmDowngrade`)
- Burn ARP scan uses DisplayName/Publisher/BundleVersion fingerprint — no hardcoded Bundle GUID needed
- `Assert-NoPendingReboot` does NOT fail on bak-path entries; only fails on the canonical `DIME.dll` path
- Each msiexec/Burn invocation writes a log to `%TEMP%\DIMETest\logs\`; CA logs are shown on any failure
- Rapid-cycling x3 in Scenario D targets the unique-tick-suffix bak naming; a collision would cause a cycle to fail

---

## Known Gaps and Limitations

1. **G3 / `FileShare.Read` lock** — `MoveFileExW` requires `FILE_SHARE_DELETE`; without it MSI falls back to `MOVEFILE_DELAY_UNTIL_REBOOT` and exits `3010`. OS-level constraint; unavoidable. `3010` is WARN; `1641` is still FAIL. `CleanupDimeDllBak` scrubs live `.cin` entries from `PendingFileRenameOperations` to prevent post-reboot corruption.
2. **Matrix MSI→Burn repair** — SecureRepair filename mismatch (`DIME-64bit-1.3.478.0.msi` vs `DIME-64bit.msi`); exits `1603`. Treated as WARN. Cross-method uninstall unaffected.
3. **Burn "No" path** — `/quiet` downgrade → 1602 may behave differently if UILevel=2 is enforced by Group Policy.
4. **Pre-Windows 10 1709** — the bak file may be marked in `PendingFileRenameOperations` until the PowerShell handle closes. `Assert-NoOrphanBaks` is called after `$handle.Close()` for exactly this reason.
5. **CA-level rollback of `MoveLockedDimeDll` itself failing** — requires `WixFailWhenDeferred`-style test CA in a separate build; not tested.
6. **`Get-InstalledProductCodes`** — must scan both Registry64 and Registry32 views; on a 64-bit machine with a 32-bit MSI installed, the ProductCode appears in the WOW6432Node hive.
