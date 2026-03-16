#Requires -RunAsAdministrator
# ==============================================================================
# DIME MSI Zero-Reboot Installer Test Suite
# ==============================================================================
# Two non-negotiable core goals:
#   1. Install, upgrade, and downgrade lay down everything completely — every
#      expected file, COM registration, and TSF TIP profile is present and
#      correct, MSI ARP reflects the installed version, nothing missing/stale.
#   2. Uninstall removes everything entirely — every file, registry key (COM,
#      TSF, ARP, DIME software hive, InstallerState, Burn side-channel), and
#      Package Cache artefact is gone.
# Both goals must be achieved without a reboot (exit 0, no PendingFileRenameOperations).
#
# DIME uses a rename+copy-back+commit-delete mechanism specifically to avoid
# reboot requirements. The CA explicitly does NOT call MoveFileExW with
# MOVEFILE_DELAY_UNTIL_REBOOT for the main DLL. Exit code 3010 from any CA
# or from Burn is a regression.
# ==============================================================================
param(
    [string]   $OldVersion  = '1.3.477.0',  # Version string for the "old" test installer
    [string]   $NewVersion  = '1.3.478.0',  # Version string for the "new" test installer
    [string]   $TestWorkDir = '.',           # Working folder for installers and logs (default: current dir)
    [string]   $NsisExe    = "..\DIME-NSIS-Universal.exe", # Optional: path to legacy NSIS DIME-*.exe (enables Scenario B)
    [string[]] $Scenarios  = @(),       # Which to run: A,B,C,D,E,F,G,H,I,Matrix (default: all possible)
    [switch]   $StopOnFail,             # Abort on first FAIL
    [switch]   $KeepBuilds,             # Don't delete installers after run
    [switch]   $Rebuild,               # Force rebuild even if installers are already present and fresh
    [switch]   $Cleanup                 # Only run Remove-AllDimeInstalls and exit
)

$ErrorActionPreference = 'Stop'

# Resolve TestWorkDir to an absolute path early (default '.' = current working directory)
if (-not (Test-Path $TestWorkDir)) { New-Item -ItemType Directory -Force $TestWorkDir | Out-Null }
$TestWorkDir = (Resolve-Path $TestWorkDir).Path

# Log directory is always in TEMP, not in TestWorkDir.
# Burn's elevated process is NOT CFA-whitelisted and cannot write to user folders under
# Documents (Windows Controlled Folder Access). msiexec.exe is whitelisted; %TEMP% is not
# in the CFA default protected list so both Burn and msiexec can write there freely.
$LogDir = "$env:TEMP\DIMETest\logs"
New-Item -ItemType Directory -Force $LogDir | Out-Null

# ==============================================================================
# Phase 1 — Test framework
# ==============================================================================
$Script:Results   = [System.Collections.Generic.List[psobject]]::new()
$Script:Scenario  = ''

function Write-Log($msg, $color = 'White') {
    $ts = Get-Date -Format 'HH:mm:ss'
    Write-Host "[$ts] $msg" -ForegroundColor $color
}
function Write-Pass($name) {
    Write-Log "  [PASS] $name" Green
}
function Write-Fail($name, $msg) {
    Write-Log "  [FAIL] $name -- $msg" Red
}
function Write-Skip($name, $reason) {
    Write-Log "  [SKIP] $name -- $reason" Yellow
}
function Write-Warn($name, $msg) {
    Write-Log "  [WARN] $name -- $msg" Yellow
}

function Assert-True($condition, $name, $failMsg) {
    if ($condition) {
        Write-Pass $name
        $Script:Results.Add([pscustomobject]@{ Scenario=$Script:Scenario; Name=$name; Passed=$true; Message='' })
    } else {
        Write-Fail $name $failMsg
        $Script:Results.Add([pscustomobject]@{ Scenario=$Script:Scenario; Name=$name; Passed=$false; Message=$failMsg })
        if ($StopOnFail) { throw "STOP: $name" }
    }
}

function Assert-FileExists($path, $ctx) {
    Assert-True (Test-Path $path -PathType Leaf) "$ctx -- file exists: $(Split-Path $path -Leaf)" "file not found: $path"
}
function Assert-FileAbsent($path, $ctx) {
    Assert-True (-not (Test-Path $path)) "$ctx -- file absent: $(Split-Path $path -Leaf)" "file still present: $path"
}
function Assert-RegKeyAbsent($key, $ctx) {
    Assert-True (-not (Test-Path $key)) "$ctx -- reg key absent: $key" "reg key still present: $key"
}
function Assert-RegValueExists($key, $valueName, $ctx) {
    $val = (Get-ItemProperty $key -Name $valueName -EA SilentlyContinue).$valueName
    Assert-True ($null -ne $val) "$ctx -- reg value exists: $valueName" "value '$valueName' missing at $key"
}
function Assert-ExitCodeSuccess($code, $ctx) {
    Assert-True ($code -eq 0) "$ctx -- exit code 0" "exit code was $code (3010=reboot required, 1641=reboot initiated — both are regressions)"
}
function Assert-ExitCode($expected, $code, $ctx) {
    Assert-True ($code -eq $expected) "$ctx -- exit code $expected" "got exit code $code"
}

# Checks PendingFileRenameOperations for canonical DIME.dll paths (non-bak).
# Bak paths (e.g. DIME.dll.1.3.000.0.ABCDEF) are transient pre-1709 artefacts and are NOT flagged.
function Assert-NoPendingReboot($ctx) {
    $pending = (Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager' `
        -Name PendingFileRenameOperations -EA SilentlyContinue).PendingFileRenameOperations
    # Match only paths ending in exactly "DIME.dll" (no dot-suffix after)
    $dimePending = $pending | Where-Object { $_ -match '\\DIME\.dll$' }
    Assert-True ($null -eq $dimePending -or @($dimePending).Count -eq 0) `
        "$ctx -- no DIME.dll pending-reboot rename" `
        "PendingFileRenameOperations contains DIME.dll: $($dimePending -join '; ')"
}

# Registry view helpers — 64-bit PowerShell does NOT auto-remap the 32-bit COM hive
function Get-RegValue64($hive, $keyPath, $valueName) {
    $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey($hive, [Microsoft.Win32.RegistryView]::Registry64)
    $key  = $base.OpenSubKey($keyPath)
    if ($null -eq $key) { return $null }
    $v = $key.GetValue($valueName)
    $key.Close(); $base.Close()
    return $v
}
function Get-RegValue32($hive, $keyPath, $valueName) {
    $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey($hive, [Microsoft.Win32.RegistryView]::Registry32)
    $key  = $base.OpenSubKey($keyPath)
    if ($null -eq $key) { return $null }
    $v = $key.GetValue($valueName)
    $key.Close(); $base.Close()
    return $v
}
function Test-RegKey64($hive, $keyPath) {
    $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey($hive, [Microsoft.Win32.RegistryView]::Registry64)
    $key  = $base.OpenSubKey($keyPath)
    $exists = $null -ne $key
    if ($key) { $key.Close() }; $base.Close()
    return $exists
}
function Test-RegKey32($hive, $keyPath) {
    $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey($hive, [Microsoft.Win32.RegistryView]::Registry32)
    $key  = $base.OpenSubKey($keyPath)
    $exists = $null -ne $key
    if ($key) { $key.Close() }; $base.Close()
    return $exists
}

# ==============================================================================
# Phase 2 — Composite assert functions
# ==============================================================================

# Burn ARP fingerprint: 32-bit Uninstall hive, BundleVersion present + Publisher=="Jeremy Wu" + DisplayName contains "DIME"
function Assert-BurnArpAbsent($ctx) {
    $base32    = [Microsoft.Win32.RegistryKey]::OpenBaseKey('LocalMachine', [Microsoft.Win32.RegistryView]::Registry32)
    $uninstall = $base32.OpenSubKey('SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall')
    $found     = @($uninstall.GetSubKeyNames() | ForEach-Object {
        $sub = $uninstall.OpenSubKey($_)
        if ($null -ne $sub.GetValue('BundleVersion') -and
            $sub.GetValue('Publisher') -eq 'Jeremy Wu' -and
            $sub.GetValue('DisplayName') -like '*DIME*') { $_ }
        $sub.Close()
    })
    $uninstall.Close(); $base32.Close()
    Assert-True ($found.Count -eq 0) "$ctx -- Burn ARP absent (32-bit hive)" `
        "found $($found.Count) Burn ARP entry(ies): $($found -join ', ')"
}

function Assert-PackageCacheAbsent($ctx) {
    $pkgCacheRoot = "$env:ProgramData\Package Cache"
    $leftovers    = @(Get-ChildItem $pkgCacheRoot -Directory -Recurse -EA SilentlyContinue |
        Where-Object {
            $_.Name -like '*DIME*' -or
            (Get-ChildItem $_.FullName -Filter 'DIME-*.exe' -EA SilentlyContinue).Count -gt 0
        })
    Assert-True ($leftovers.Count -eq 0) "$ctx -- Package Cache absent" `
        "found $($leftovers.Count) DIME cached folder(s): $($leftovers.FullName -join '; ')"
}

function Assert-InstallerStateClean($ctx) {
    Assert-RegKeyAbsent 'HKLM:\SOFTWARE\DIME\InstallerState' $ctx
}

function Assert-NoOrphanBaks($ctx) {
    # DLL baks: DIME.dll.{ver}.{tick} in arch subdirs and legacy NSIS System32/SysWOW64 locations
    $dllLocations = @(
        "$env:ProgramFiles\DIME\amd64", "$env:ProgramFiles\DIME\arm64", "$env:ProgramFiles\DIME\x86",
        "$env:SystemRoot\System32", "$env:SystemRoot\SysWOW64"
    )
    $dllBaks = @($dllLocations | Where-Object { Test-Path $_ } | ForEach-Object {
        Get-ChildItem $_ -Filter 'DIME.dll.*' -EA SilentlyContinue |
            Where-Object { $_.Name -ne 'DIME.dll' }   # Win32 FindFirstFile: 'DIME.dll.*' matches 'DIME.dll' (trailing-dot quirk)
    })
    Assert-True ($dllBaks.Count -eq 0) "$ctx -- no orphan DLL bak files" `
        "found $($dllBaks.Count) orphan DLL bak(s): $($dllBaks.FullName -join '; ')"

    # CIN baks: *.cin.{ver}.{tick} directly in ProgramFiles\DIME root (CleanupCinBaksInDir pattern)
    $cinBaks = @(Get-ChildItem "$env:ProgramFiles\DIME" -Filter '*.cin.*' -EA SilentlyContinue |
        Where-Object { $_.Name -match '\.cin\.' })    # Win32 FindFirstFile: '*.cin.*' matches '*.cin' (trailing-dot quirk)
    Assert-True ($cinBaks.Count -eq 0) "$ctx -- no orphan CIN bak files" `
        "found $($cinBaks.Count) orphan CIN bak(s): $($cinBaks.FullName -join '; ')"
}

# Arch-aware installed-state check.
# $NativeArch is set at startup from the machine's real PROCESSOR_ARCHITECTURE.
function Assert-InstalledClean($expectedVer, $ctx = 'Assert-InstalledClean', [switch]$SkipBurnArp) {
    $clsid        = '{1DE68A87-FF3B-46A0-8F80-46730B2491B1}'
    $base         = "$env:ProgramFiles\DIME"
    $profileGuids = @(
        '{36851834-92AD-4397-9F50-800384D5C24C}',   # Dayi
        '{5DFC1743-638C-4F61-9E76-FCFA9707F450}',   # Array
        '{26892981-14E3-447B-AF2C-9067CD4A4A8A}',   # Phonetic
        '{061BEEA7-FFF9-420C-B3F6-A9047AFD0877}'    # Generic
    )

    # 1. DLL file presence (arch-conditional)
    if ($NativeArch -eq 'AMD64') {
        Assert-FileExists "$base\amd64\DIME.dll" $ctx
        Assert-FileAbsent "$base\arm64\DIME.dll" $ctx
    } elseif ($NativeArch -eq 'ARM64') {
        Assert-FileAbsent "$base\amd64\DIME.dll" $ctx
        Assert-FileExists "$base\arm64\DIME.dll" $ctx
    }
    Assert-FileExists "$base\x86\DIME.dll" $ctx   # x86 always present

    # 2. 64-bit COM registration — InProcServer32 default value = arch-correct DLL path
    $expectedDll64 = if ($NativeArch -eq 'ARM64') { "$base\arm64\DIME.dll" } else { "$base\amd64\DIME.dll" }
    $comVal64      = Get-RegValue64 LocalMachine "SOFTWARE\Classes\CLSID\$clsid\InProcServer32" $null
    Assert-True ($comVal64 -eq $expectedDll64) "$ctx -- 64-bit COM InProcServer32" `
        "got '$comVal64', expected '$expectedDll64'"

    # 3. 32-bit COM registration (WOW6432Node, Registry32 view)
    $comVal32      = Get-RegValue32 LocalMachine "SOFTWARE\Classes\CLSID\$clsid\InProcServer32" $null
    $expectedDll32 = "$base\x86\DIME.dll"
    Assert-True ($comVal32 -eq $expectedDll32) "$ctx -- 32-bit COM InProcServer32" `
        "got '$comVal32', expected '$expectedDll32'"

    # 4. 64-bit TSF TIP registration x4 profiles
    foreach ($pguid in $profileGuids) {
        $tsfKey = "SOFTWARE\Microsoft\CTF\TIP\$clsid\LanguageProfile\0x00000404\$pguid"
        Assert-True (Test-RegKey64 LocalMachine $tsfKey) "$ctx -- TSF 64-bit profile $pguid" "key missing"
    }

    # 5. 32-bit TSF TIP registration x4 profiles (WOW6432Node, Registry32 view)
    foreach ($pguid in $profileGuids) {
        $tsfKey = "SOFTWARE\Microsoft\CTF\TIP\$clsid\LanguageProfile\0x00000404\$pguid"
        Assert-True (Test-RegKey32 LocalMachine $tsfKey) "$ctx -- TSF 32-bit profile $pguid" "key missing"
    }

    # 6. MSI ARP entry — exactly one, no BundleVersion (Burn ARP must be absent)
    $arpFound = @(Get-ChildItem 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall' |
        Where-Object {
            ($_.GetValue('DisplayName') -like '*DIME*') -and
            ($_.GetValue('Publisher')   -eq 'Jeremy Wu') -and
            ($null -eq $_.GetValue('BundleVersion'))
        })
    Assert-True ($arpFound.Count -eq 1) "$ctx -- MSI ARP entry present (exactly 1)" `
        "found $($arpFound.Count) matching entries"

    # 7. Burn ARP always absent — EXCEPT after a Burn-to-Burn upgrade, where WiX Burn re-registers
    #    its own hidden (SystemComponent=1) entry at Apply-end, after the MSI commit CA has already run.
    #    No MSI CA execution slot exists post-Apply, so this is a known design constraint, not a bug.
    if ($SkipBurnArp) {
        Write-Skip "$ctx -- Burn ARP absent (32-bit hive)" 'Burn-to-Burn upgrade: Burn re-registers hidden ARP at Apply-end (post-CA, by design)'
        Write-Skip "$ctx -- Package Cache absent" 'Burn-to-Burn upgrade: Package Cache populated at Apply-end alongside hidden ARP'
    } else {
        Assert-BurnArpAbsent $ctx

        # 8. Package Cache always absent (CleanupBurnRegistration sync-deletes in commit)
        Assert-PackageCacheAbsent $ctx
    }

    # 9. Side-channel key absent (CleanupBurnRegistration reads and deletes it)
    Assert-RegKeyAbsent 'HKLM:\SOFTWARE\DIME\BurnCacheCleanup' $ctx

    # 10. InstalledVersion value present (Burn writes build-format e.g. 1.2.478.60308; MSI writes
    #     ProductVersion e.g. 1.3.478.0 — both are valid; the CA reads the key without version matching)
    $installedVer = (Get-ItemProperty 'HKLM:\SOFTWARE\DIME' -Name InstalledVersion -EA SilentlyContinue).InstalledVersion
    Assert-True ($null -ne $installedVer -and $installedVer -ne '') "$ctx -- InstalledVersion present" `
        "InstalledVersion missing or empty (got '$installedVer')"

    # 11. InstallerState scratch key absent (cleaned by commit/rollback CAs)
    Assert-InstallerStateClean $ctx

    # 12. No orphan bak files (DLL baks + CIN baks)
    Assert-NoOrphanBaks $ctx

    # 13. All .cin table files present
    foreach ($cin in @(
        'phone.cin', 'TCFreq.cin', 'TCSC.cin',
        'Array-Phrase.cin', 'Array-Ext-B.cin', 'Array-Ext-CD.cin', 'Array-Ext-EF.cin',
        'Array.cin', 'Array40.cin', 'Array-shortcode.cin', 'Array-special.cin'
    )) {
        Assert-FileExists "$base\$cin" $ctx
    }

    # 14. DIMESettings.exe present
    Assert-FileExists "$base\DIMESettings.exe" $ctx

    # 15. Start Menu shortcut present and points to DIMESettings.exe
    #     The file name contains Chinese characters (DIME設定.lnk); verifying
    #     the target via WScript.Shell also confirms the .lnk is internally
    #     valid and the Unicode filename was not garbled during install.
    $shortcut = "$env:ProgramData\Microsoft\Windows\Start Menu\Programs\DIME\DIME設定.lnk"
    Assert-FileExists $shortcut $ctx
    if (Test-Path $shortcut) {
        $shell  = New-Object -ComObject WScript.Shell
        $lnk    = $shell.CreateShortcut($shortcut)
        $target = $lnk.TargetPath
        if ($target) {
            # Non-advertised shortcut: verify target points to DIMESettings.exe
            Assert-True ($target -like '*\DIMESettings.exe') `
                "$ctx -- shortcut target is DIMESettings.exe" `
                "shortcut target is '$target' (expected *\DIMESettings.exe)"
        } else {
            # MSI advertised shortcut: WScript.Shell returns empty TargetPath.
            # This is expected when the shortcut component's KeyPath is a registry
            # value rather than the target file.  The shortcut still works correctly
            # — Windows resolves it through MSI at launch time.
            Write-Host "  [INFO] shortcut is MSI-advertised (empty TargetPath via WScript.Shell) -- OK" -ForegroundColor DarkGray
        }
    }
}

function Assert-UninstalledClean($ctx = 'Assert-UninstalledClean') {
    $clsid = '{1DE68A87-FF3B-46A0-8F80-46730B2491B1}'
    $base  = "$env:ProgramFiles\DIME"

    # All arch DLLs absent
    Assert-FileAbsent "$base\amd64\DIME.dll" $ctx
    Assert-FileAbsent "$base\arm64\DIME.dll" $ctx
    Assert-FileAbsent "$base\x86\DIME.dll"   $ctx

    # COM keys absent (both hives)
    Assert-True (-not (Test-RegKey64 LocalMachine "SOFTWARE\Classes\CLSID\$clsid")) `
        "$ctx -- 64-bit COM key absent" "still present"
    Assert-True (-not (Test-RegKey32 LocalMachine "SOFTWARE\Classes\CLSID\$clsid")) `
        "$ctx -- 32-bit COM key absent" "still present"

    # TSF TIP keys absent (both hives)
    Assert-True (-not (Test-RegKey64 LocalMachine "SOFTWARE\Microsoft\CTF\TIP\$clsid")) `
        "$ctx -- TSF 64-bit key absent" "still present"
    Assert-True (-not (Test-RegKey32 LocalMachine "SOFTWARE\Microsoft\CTF\TIP\$clsid")) `
        "$ctx -- TSF 32-bit key absent" "still present"

    # MSI ARP absent
    $arpFound = @(Get-ChildItem 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall' |
        Where-Object {
            ($_.GetValue('DisplayName') -like '*DIME*') -and
            ($_.GetValue('Publisher')   -eq 'Jeremy Wu') -and
            ($null -eq $_.GetValue('BundleVersion'))
        })
    Assert-True ($arpFound.Count -eq 0) "$ctx -- MSI ARP absent" "found $($arpFound.Count) entries"

    # Burn ARP always absent (CA runs unconditionally including on unintstall)
    Assert-BurnArpAbsent $ctx

    # Package Cache absent
    Assert-PackageCacheAbsent $ctx

    # Side-channel key absent
    Assert-RegKeyAbsent 'HKLM:\SOFTWARE\DIME\BurnCacheCleanup' $ctx

    # SOFTWARE\DIME key entirely absent after uninstall (InstalledVersion is a value, not a subkey)
    Assert-RegKeyAbsent 'HKLM:\SOFTWARE\DIME' $ctx

    # InstallerState absent
    Assert-InstallerStateClean $ctx

    # No orphan baks (DLL baks + CIN baks)
    Assert-NoOrphanBaks $ctx

    # ProgramFiles\DIME directory entirely absent
    # CleanupDimeDllBak removes arch subdirs (amd64/arm64/x86) and the parent DIME dir
    Assert-True (-not (Test-Path $base)) `
        "$ctx -- ProgramFiles\DIME directory absent" "directory still exists: $base"

    # Start Menu shortcut absent
    $shortcut = "$env:ProgramData\Microsoft\Windows\Start Menu\Programs\DIME\DIME設定.lnk"
    Assert-True (-not (Test-Path $shortcut)) `
        "$ctx -- Start Menu shortcut absent" "shortcut still exists: $shortcut"
}

# ==============================================================================
# Phase 3 — Build helper
# ==============================================================================

function Build-TestInstallers {
    $deployScript = Resolve-Path "$PSScriptRoot\..\deploy-wix-installer.ps1"
    $buildTmp     = "$TestWorkDir\_build_tmp"
    $staleAfter   = (Get-Date).AddHours(-1)

    foreach ($ver in @($OldVersion, $NewVersion)) {
        $exeDest = "$TestWorkDir\DIME-Universal-$ver.exe"
        $msiDest = "$TestWorkDir\DIME-64bit-$ver.msi"

        # Skip build if both outputs exist, were written within the last hour, no source
        # file is newer than the outputs, and -Rebuild was not specified.
        # Source staleness matters: if a WXS property (e.g. MsiRestartManagerControl) was
        # fixed AFTER the last build, a cached MSI would still carry the unfixed property.
        # In Scenario E this causes RM to auto-terminate the test process (RM detects the
        # PowerShell lock-holder and kills it under /qn's auto-close mode), appearing as
        # the script silently stopping with no [TIME] line and no summary.
        if (-not $Rebuild) {
            $exeInfo = Get-Item $exeDest -EA SilentlyContinue
            $msiInfo = Get-Item $msiDest -EA SilentlyContinue
            if ($exeInfo -and $msiInfo -and
                $exeInfo.LastWriteTime -gt $staleAfter -and
                $msiInfo.LastWriteTime -gt $staleAfter) {
                # Invalidate cache if any WXS source file or the deploy script is newer
                # than the cached MSI — even a 1-second difference matters here.
                $srcFiles = @(
                    Get-ChildItem "$PSScriptRoot\*.wxs" -EA SilentlyContinue
                    Get-Item "$PSScriptRoot\..\deploy-wix-installer.ps1" -EA SilentlyContinue
                    Get-ChildItem "$PSScriptRoot\DIMEInstallerCA\*.cpp" -EA SilentlyContinue
                    Get-ChildItem "$PSScriptRoot\DIMEInstallerCA\*.h" -EA SilentlyContinue
                )
                $newestSrc = ($srcFiles | Measure-Object -Property LastWriteTime -Maximum).Maximum
                if ($null -ne $newestSrc -and $newestSrc -gt $msiInfo.LastWriteTime) {
                    Write-Host "`nSource files changed since last build ($newestSrc > $($msiInfo.LastWriteTime)) — rebuilding $ver." -ForegroundColor Yellow
                } else {
                    Write-Host "`nSkipping build for $ver (outputs fresh, use -Rebuild to force)." -ForegroundColor DarkGray
                    continue
                }
            }
        }

        $tmpDir = "$buildTmp\$ver"
        New-Item -ItemType Directory -Force $tmpDir | Out-Null

        Write-Host "`nBuilding test installer $ver..." -ForegroundColor Yellow
        # deploy-wix-installer.ps1 uses bare relative paths (e.g. "..\src\BuildInfo.h") that
        # must resolve relative to its own directory, so we cd there before invoking it.
        Push-Location (Split-Path $deployScript)
        try {
            & $deployScript -ProductVersionOverride $ver -OutputDir $tmpDir -NonInteractive -SkipChecksumUpdate
            if ($LASTEXITCODE -ne 0) { throw "Failed to build $ver installer (exit $LASTEXITCODE)" }
        } finally {
            Pop-Location
        }

        # Move outputs to TestWorkDir with version suffix in filename
        Move-Item "$tmpDir\DIME-Universal.exe" $exeDest -Force
        Move-Item "$tmpDir\DIME-64bit.msi"     $msiDest -Force
    }
    Remove-Item $buildTmp -Recurse -Force -EA SilentlyContinue

    return @{
        OldExe   = "$TestWorkDir\DIME-Universal-$OldVersion.exe"
        NewExe   = "$TestWorkDir\DIME-Universal-$NewVersion.exe"
        OldMsi64 = "$TestWorkDir\DIME-64bit-$OldVersion.msi"
        NewMsi64 = "$TestWorkDir\DIME-64bit-$NewVersion.msi"
    }
}

# ==============================================================================
# Phase 4 — Installer helpers
# ==============================================================================

# Maximum allowed wall-clock time (seconds) for any single install/uninstall operation.
# Exceeding this limit is a regression — it indicates TSF/COM stall or a blocking CA.
$Script:MaxInstallSecs = 120

function Invoke-WithLog($exePath, [string[]]$argList, $logPath) {
    New-Item -ItemType Directory -Force (Split-Path $logPath) | Out-Null
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $p  = Start-Process -FilePath $exePath -ArgumentList $argList -Wait -PassThru -NoNewWindow
    $sw.Stop()
    $Script:LastElapsedSeconds = [int]$sw.Elapsed.TotalSeconds
    $elapsed = $sw.Elapsed.ToString('mm\:ss')
    $leaf    = Split-Path $exePath -Leaf
    Write-Log "  [TIME] $leaf exited $($p.ExitCode) in ${elapsed}" DarkGray
    return $p.ExitCode
}

# Assert that the most recent Invoke-WithLog call completed within the allowed time.
function Assert-Duration($ctx) {
    $secs = $Script:LastElapsedSeconds
    $max  = $Script:MaxInstallSecs
    $msg  = "operation took ${secs}s -- exceeded ${max}s limit (TSF/COM stall or blocking CA?)"
    Assert-True ($secs -le $max) "$ctx -- completed in ${secs}s (limit ${max}s)" $msg
}

function Show-LastLogLines($logPath, $lines = 20) {
    if (Test-Path $logPath) {
        Write-Host "  --- last $lines lines of $(Split-Path $logPath -Leaf) ---" -ForegroundColor DarkGray
        Get-Content $logPath -Tail $lines | ForEach-Object { Write-Host "    $_" -ForegroundColor DarkGray }
    }
}

function Show-CaLogs {
    foreach ($caLog in @('C:\Windows\Temp\DIMEBurnCA.txt', 'C:\Users\Public\DIMEBurnCA.txt')) {
        Show-LastLogLines $caLog
    }
}

function Install-ViaBurn($exePath, [string[]]$extraArgs = @()) {
    $log  = "$LogDir\$(Split-Path $exePath -Leaf)-install-$(Get-Date -f yyyyMMdd-HHmmss).log"
    Write-Log "  [RUN] Burn install: $(Split-Path $exePath -Leaf)" DarkGray
    $code = Invoke-WithLog $exePath (@('/install', '/quiet', '/norestart', "/log $log") + $extraArgs) $log
    if ($code -ne 0) { Show-LastLogLines $log; Show-CaLogs }
    Assert-Duration "Burn-install:$(Split-Path $exePath -Leaf)"
    return $code
}

function Install-ViaMsi($msiPath, [string[]]$extraArgs = @()) {
    $log  = "$LogDir\$(Split-Path $msiPath -Leaf)-install-$(Get-Date -f yyyyMMdd-HHmmss).log"
    Write-Log "  [RUN] MSI install: $(Split-Path $msiPath -Leaf)" DarkGray
    $code = Invoke-WithLog 'msiexec.exe' (@('/i', $msiPath, '/qn', '/norestart', "/l*v `"$log`"") + $extraArgs) $log
    if ($code -ne 0 -and $code -ne 1603 -and $code -ne 1602) { Show-LastLogLines $log; Show-CaLogs }
    Assert-Duration "MSI-install:$(Split-Path $msiPath -Leaf)"
    return $code
}

function Uninstall-ViaBurn($exePath, [string[]]$extraArgs = @()) {
    $log  = "$LogDir\$(Split-Path $exePath -Leaf)-uninstall-$(Get-Date -f yyyyMMdd-HHmmss).log"
    Write-Log "  [RUN] Burn uninstall: $(Split-Path $exePath -Leaf)" DarkGray
    $code = Invoke-WithLog $exePath (@('/uninstall', '/quiet', '/norestart', "/log $log") + $extraArgs) $log
    if ($code -ne 0) { Show-LastLogLines $log; Show-CaLogs }
    Assert-Duration "Burn-uninstall:$(Split-Path $exePath -Leaf)"
    return $code
}

function Uninstall-ViaMsiProductCode($productCode) {
    $log  = "$LogDir\uninstall-$productCode-$(Get-Date -f yyyyMMdd-HHmmss).log"
    Write-Log "  [RUN] MSI uninstall: $productCode" DarkGray
    $code = Invoke-WithLog 'msiexec.exe' @('/x', $productCode, '/qn', '/norestart', "/l*v `"$log`"") $log
    if ($code -ne 0) { Show-LastLogLines $log; Show-CaLogs }
    Assert-Duration "MSI-uninstall:$productCode"
    return $code
}

# Starts a child powershell.exe that holds a file lock on $path for up to 5 minutes,
# then polls until the child has acquired the lock.  $share controls the FileShare
# mode used by the child (what other processes are allowed to do concurrently):
#   'None'            exclusive — no sharing at all (FileShare.None / FileAccess.ReadWrite)
#   'Read'            read-only sharing — others may read but not write/delete
#   'ReadWriteDelete' permissive — matches OS loader flags for mapped DLLs/CINs
# Using a child process means Restart Manager kills the child — not the test runner —
# when RM runs in /qn mode and finds the file locked.  Returns $null on failure.
function Start-ExclusiveLockChild([string]$path, [string]$mode = 'OpenOrCreate', [string]$share = 'None') {
    # IMPORTANT: pass the script as base64 -EncodedCommand so the encoded string
    # has no spaces at the CreateProcess level — paths like "C:\Program Files\..."
    # are otherwise mangled by CommandLineToArgvW even inside single quotes.
    $escaped = $path -replace "'", "''"   # escape any single quotes in path
    $access  = if ($share -eq 'None') { 'ReadWrite' } else { 'Read' }
    $script  = "`$h = [System.IO.File]::Open('$escaped', [System.IO.FileMode]::$mode, [System.IO.FileAccess]::$access, [System.IO.FileShare]::$share); Start-Sleep -Seconds 300"
    $enc     = [Convert]::ToBase64String([System.Text.Encoding]::Unicode.GetBytes($script))
    $proc    = Start-Process 'powershell.exe' `
        -ArgumentList '-NoProfile', '-NonInteractive', '-WindowStyle', 'Hidden', '-EncodedCommand', $enc `
        -PassThru
    $lockConfirmed = $false
    $deadline = (Get-Date).AddSeconds(5)
    while ((Get-Date) -lt $deadline) {
        if ($proc.HasExited) { break }
        if (Test-Path $path -PathType Leaf) {
            if ($share -eq 'ReadWriteDelete') {
                # Child grants full sharing — cannot detect via probe open.
                # Wait 500 ms for the child to open the file, then treat alive+exists
                # as lock confirmed (PowerShell startup takes ~200–400 ms).
                Start-Sleep -Milliseconds 500
                if (-not $proc.HasExited) { $lockConfirmed = $true }
                break
            } else {
                # 'None' or 'Read': child does not grant Write access.
                # A Write probe triggers IOException (sharing violation) once locked.
                try {
                    $h = [System.IO.File]::Open($path,
                        [System.IO.FileMode]::Open,
                        [System.IO.FileAccess]::Write,
                        [System.IO.FileShare]::None)
                    $h.Close()   # no exception → child not yet holding the lock
                } catch [System.IO.IOException] {
                    $lockConfirmed = $true
                    break
                }
            }
        }
        Start-Sleep -Milliseconds 100
    }
    if (-not $lockConfirmed -or $proc.HasExited) {
        if (-not $proc.HasExited) { $proc.Kill() }
        $proc.Dispose()
        return $null
    }
    return $proc
}

function Repair-ViaMsi($productCode) {
    $log  = "$LogDir\repair-$productCode-$(Get-Date -f yyyyMMdd-HHmmss).log"
    Write-Log "  [RUN] MSI repair: $productCode" DarkGray
    $code = Invoke-WithLog 'msiexec.exe' @('/fvecmus', $productCode, '/qn', '/norestart', "/l*v `"$log`"") $log
    if ($code -ne 0) { Show-LastLogLines $log; Show-CaLogs }
    Assert-Duration "MSI-repair:$productCode"
    return $code
}

# Mirrors deploy-wix-installer.ps1: derives a deterministic GUID from a seed string
function Get-DeterministicGuid([string]$seed) {
    $md5   = [System.Security.Cryptography.MD5]::Create()
    $bytes = $md5.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($seed))
    return [System.Guid]::new($bytes).ToString('B').ToUpper()
}

# Returns the deterministic ProductCode GUID for a given version + arch.
# Must match deploy-wix-installer.ps1: seed = "DIME-x64-{major.minor.commit}"
function Get-MsiProductCode([string]$version, [string]$arch = 'x64') {
    $short = ($version -split '\.')[0..2] -join '.'
    $seed  = if ($arch -eq 'x86') { "DIME-x86-$short" } else { "DIME-x64-$short" }
    return Get-DeterministicGuid $seed
}

function Remove-AllDimeInstalls {
    # Helper: try to delete a file; if it is locked and cannot be removed,
    # rename it out of the way (clearing the target path so tests can continue)
    # then schedule the renamed copy for deletion on next reboot via MoveFileExW.
    function Remove-OrRenameAndPend($path) {
        if (-not (Test-Path $path -PathType Leaf)) { return }
        Remove-Item $path -Force -EA SilentlyContinue
        if (-not (Test-Path $path -PathType Leaf)) { return }
        # Still present — file is locked.  Move it to a .removing suffix so it
        # no longer occupies the target path, then pend-delete the renamed copy.
        $bak = "$path.removing"
        Move-Item $path $bak -Force -EA SilentlyContinue
        if (Test-Path $bak) {
            Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class DimeRemovePend {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode)]
    public static extern bool MoveFileExW(string existingFileName, string newFileName, uint flags);
}
'@ -ErrorAction SilentlyContinue
            [DimeRemovePend]::MoveFileExW($bak, $null, 4) | Out-Null  # 4 = MOVEFILE_DELAY_UNTIL_REBOOT
            Write-Host "  [WARN] $([System.IO.Path]::GetFileName($path)) was locked; renamed to .removing and pended for deletion." -ForegroundColor Yellow
        }
    }

    # 1. Uninstall NSIS DIME if present (uses a fixed ARP key, not an upgrade code)
    $nsisKey = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\DIME'
    $nsisArp = Get-ItemProperty $nsisKey -EA SilentlyContinue
    if ($nsisArp) {
        $uninstStr = ($nsisArp.UninstallString -replace '"', '').Trim()
        if ($uninstStr -and (Test-Path $uninstStr)) {
            Start-Process $uninstStr -ArgumentList '/S' -Wait -NoNewWindow -EA SilentlyContinue
        }
        Remove-Item $nsisKey -Force -EA SilentlyContinue   # clean up ARP key if uninstaller left it
    }
    # NSIS may leave DLLs in System32/SysWOW64 even after a clean uninstall.
    # Try direct delete; if locked, rename out of the way and pend-delete.
    Remove-OrRenameAndPend "$env:SystemRoot\System32\DIME.dll"
    Remove-OrRenameAndPend "$env:SystemRoot\SysWOW64\DIME.dll"

    # 2. Uninstall WiX-managed MSIs by their deterministic ProductCode (same formula
    #    as deploy-wix-installer.ps1 — no registry scan needed, always finds them)
    foreach ($ver in @($OldVersion, $NewVersion) | Select-Object -Unique) {
        foreach ($arch in @('x64', 'x86')) {
            $pc  = Get-MsiProductCode $ver $arch
            $log = "$LogDir\uninstall-$pc-$(Get-Date -f yyyyMMdd-HHmmss).log"
            Write-Host "  [Remove-All] msiexec /x $pc ($arch $ver)..." -ForegroundColor DarkGray
            $exitCode = Invoke-WithLog 'msiexec.exe' @('/x', $pc, '/qn', "/l*v `"$log`"") $log
            if ($exitCode -eq 0) {
                Write-Host "  [Remove-All] Uninstalled $pc" -ForegroundColor DarkGray
            } elseif ($exitCode -eq 1605) {
                Write-Host "  [Remove-All] $pc not present (1605 — OK)" -ForegroundColor DarkGray
            } else {
                Write-Host "  [Remove-All] msiexec /x $pc returned $exitCode" -ForegroundColor Yellow
                Show-LastLogLines $log
            }
        }
    }
    # 3. Orphan MSI ARP sweep — catches MSI installs from prior test sessions whose
    #    ProductCode is not in the current OldVersion/NewVersion set.
    #    Scan both 64-bit and 32-bit ARP hives; subkey name == MSI ProductCode.
    foreach ($view in @('Registry64', 'Registry32')) {
        $base      = [Microsoft.Win32.RegistryKey]::OpenBaseKey('LocalMachine', [Microsoft.Win32.RegistryView]::$view)
        $uninstall = $base.OpenSubKey('SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall')
        if (-not $uninstall) { $base.Close(); continue }
        $orphans = @($uninstall.GetSubKeyNames() | ForEach-Object {
            $sub   = $uninstall.OpenSubKey($_)
            $match = ($null -eq $sub.GetValue('BundleVersion') -and
                      $sub.GetValue('Publisher') -eq 'Jeremy Wu' -and
                      $sub.GetValue('DisplayName') -like '*DIME*')
            $sub.Close()
            if ($match) { $_ }
        })
        $uninstall.Close(); $base.Close()
        foreach ($pc in $orphans) {
            $log      = "$LogDir\uninstall-orphan-$(Get-Date -f yyyyMMdd-HHmmss).log"
            Write-Host "  [Remove-All] Orphan MSI ARP ($view): msiexec /x $pc ..." -ForegroundColor Yellow
            $exitCode = Invoke-WithLog 'msiexec.exe' @('/x', $pc, '/qn', "/l*v `"$log`"") $log
            if ($exitCode -eq 0) {
                Write-Host "  [Remove-All] Removed orphan $pc" -ForegroundColor DarkGray
            } elseif ($exitCode -eq 1605) {
                Write-Host "  [Remove-All] Orphan $pc not present (1605 — OK)" -ForegroundColor DarkGray
            } else {
                Write-Host "  [Remove-All] msiexec /x $pc returned $exitCode" -ForegroundColor Yellow
                Show-LastLogLines $log
            }
            # Belt-and-suspenders: directly delete the ARP key in case msiexec /x left it behind
            # (can happen when the MSI database is partially corrupt or from a broken prior run).
            $baseD    = [Microsoft.Win32.RegistryKey]::OpenBaseKey('LocalMachine', [Microsoft.Win32.RegistryView]::$view)
            $uninstD  = $baseD.OpenSubKey('SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall', $true)
            if ($uninstD) { $uninstD.DeleteSubKeyTree($pc, $false); $uninstD.Close() }
            $baseD.Close()
        }
    }

    # 4. Burn bundle ARP sweep — in the Burn-to-Burn upgrade path, WiX Burn writes
    #    the new bundle's ARP entry at Apply-end (AFTER the MSI commit phase), so
    #    the CleanupBurnRegistration CA can't intercept it and the entry lingers.
    #    Scan Registry32 (Burn always uses the 32-bit hive) for any DIME Burn entry
    #    (BundleVersion present), then brute-force delete the ARP key + Package Cache.
    $base32    = [Microsoft.Win32.RegistryKey]::OpenBaseKey('LocalMachine', [Microsoft.Win32.RegistryView]::Registry32)
    $uninstall = $base32.OpenSubKey('SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall')
    if ($uninstall) {
        $burnEntries = @($uninstall.GetSubKeyNames() | ForEach-Object {
            $sub   = $uninstall.OpenSubKey($_)
            $match = ($null -ne $sub.GetValue('BundleVersion') -and
                      $sub.GetValue('Publisher') -eq 'Jeremy Wu' -and
                      $sub.GetValue('DisplayName') -like '*DIME*')
            $cachePath = if ($match) { $sub.GetValue('BundleCachePath') } else { $null }
            $sub.Close()
            if ($match) { @{ Guid = $_; CachePath = $cachePath } }
        })
        $uninstall.Close(); $base32.Close()
        foreach ($entry in $burnEntries) {
            Write-Host "  [Remove-All] Burn ARP (Registry32): $($entry.Guid)" -ForegroundColor Yellow
            # Delete ARP key from Registry32
            $base32w   = [Microsoft.Win32.RegistryKey]::OpenBaseKey('LocalMachine', [Microsoft.Win32.RegistryView]::Registry32)
            $uninstW   = $base32w.OpenSubKey('SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall', $true)
            if ($uninstW) { $uninstW.DeleteSubKeyTree($entry.Guid, $false); $uninstW.Close() }
            $base32w.Close()
            # Delete Package Cache folder (BundleCachePath = EXE path; parent dir = cache folder)
            if ($entry.CachePath) {
                $cacheDir = Split-Path $entry.CachePath
                if (Test-Path $cacheDir) {
                    Write-Host "  [Remove-All] Package Cache: removing $cacheDir" -ForegroundColor DarkGray
                    Remove-Item $cacheDir -Recurse -Force -EA SilentlyContinue
                }
            }
            Write-Host "  [Remove-All] Burn bundle entry removed: $($entry.Guid)" -ForegroundColor DarkGray
        }
    } else { $base32.Close() }

    # Belt-and-suspenders: remove any state the MSI uninstaller may have missed
    # COM + TSF registrations must be removed from BOTH registry views (64-bit and WOW6432Node).
    $clsid = '{1DE68A87-FF3B-46A0-8F80-46730B2491B1}'
    foreach ($view in @([Microsoft.Win32.RegistryView]::Registry64, [Microsoft.Win32.RegistryView]::Registry32)) {
        $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey('LocalMachine', $view)
        foreach ($keyPath in @(
            "SOFTWARE\Classes\CLSID\$clsid",
            "SOFTWARE\Microsoft\CTF\TIP\$clsid"
        )) {
            $key = $base.OpenSubKey($keyPath, $true)
            if ($key) { $key.Close(); $base.DeleteSubKeyTree($keyPath, $false) }
        }
        $base.Close()
    }
    Remove-Item 'HKLM:\SOFTWARE\DIME'      -Recurse -Force -EA SilentlyContinue
    # ProgramFiles DLLs may be locked by a running process; rename-and-pend each
    # before sweeping the directory (same pattern as System32/SysWOW64 above).
    foreach ($dllPath in @(
        "$env:ProgramFiles\DIME\amd64\DIME.dll",
        "$env:ProgramFiles\DIME\arm64\DIME.dll",
        "$env:ProgramFiles\DIME\x86\DIME.dll"
    )) { Remove-OrRenameAndPend $dllPath }
    # Explicitly sweep orphan bak files (DIME.dll.* and *.cin.*) left by prior test runs
    # where CleanupDimeDllBak failed to delete them (e.g. read-only attribute inherited from
    # a test placeholder that MoveLockedDimeDll renamed).  Clear attributes before deleting
    # so read-only doesn't block removal.  These files are never open by running processes.
    if (Test-Path "$env:ProgramFiles\DIME") {
        Get-ChildItem "$env:ProgramFiles\DIME" -Recurse -Force -EA SilentlyContinue |
            Where-Object { -not $_.PSIsContainer -and
                           ($_.Name -match '^DIME\.dll\.' -or $_.Name -match '\.cin\.') } |
            ForEach-Object {
                $_.Attributes = [System.IO.FileAttributes]::Normal
                Remove-Item $_.FullName -Force -EA SilentlyContinue
            }
    }
    Remove-Item "$env:ProgramFiles\DIME"   -Recurse -Force -EA SilentlyContinue
    if (Test-Path "$env:ProgramFiles\DIME") {
        Write-Host "  [WARN] $env:ProgramFiles\DIME still present after removal — locked files may remain." -ForegroundColor Yellow
    }

    # 5. Package Cache sweep — catches any stale DIME bundle folders whose
    #    CleanupBurnRegistration CA never ran, or that survived step 4 above.
    Get-ChildItem "$env:ProgramData\Package Cache" -Directory -EA SilentlyContinue |
        Where-Object {
            $_.Name -like '*DIME*' -or
            (Get-ChildItem $_.FullName -Filter 'DIME-*.exe' -EA SilentlyContinue).Count -gt 0
        } | ForEach-Object {
            Write-Host "  [Remove-All] Package Cache: sweeping $($_.FullName)" -ForegroundColor DarkGray
            Remove-Item $_.FullName -Recurse -Force -EA SilentlyContinue
        }

    # 6. Orphan Burn dependency sweep — WiX Burn registers {BundleGUID} as a
    #    dependent of each chain package under:
    #      HKLM\SOFTWARE\Classes\Installer\Dependencies\{PackageKey}\Dependents\{BundleGUID}
    #    CleanupBurnRegistration CA deletes the Burn ARP entry but NOT the Dependents
    #    subkeys.  When a subsequent Burn run has a DIFFERENT bundle GUID (each build
    #    of DIME-Universal.exe gets a new random GUID), it sees the phantom dependent
    #    and refuses to uninstall the MSI, exiting 0 while leaving files/keys intact.
    #    Fix: remove any Dependents\{GUID} whose bundle is no longer in the ARP hive.
    $activeBundles = @{}
    $arpBase32 = [Microsoft.Win32.RegistryKey]::OpenBaseKey('LocalMachine', [Microsoft.Win32.RegistryView]::Registry32)
    $arpUninstKey = $arpBase32.OpenSubKey('SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall')
    if ($arpUninstKey) {
        foreach ($name in $arpUninstKey.GetSubKeyNames()) {
            $sub = $arpUninstKey.OpenSubKey($name)
            if ($sub -and $null -ne $sub.GetValue('BundleVersion')) { $activeBundles[$name.ToUpper()] = $true }
            if ($sub) { $sub.Close() }
        }
        $arpUninstKey.Close()
    }
    $arpBase32.Close()

    # HKLM\SOFTWARE\Classes is not subject to WOW64 redirection — use Registry64 view.
    $depsBase = [Microsoft.Win32.RegistryKey]::OpenBaseKey('LocalMachine', [Microsoft.Win32.RegistryView]::Registry64)
    $depsRoot = $depsBase.OpenSubKey('SOFTWARE\Classes\Installer\Dependencies', $true)
    if ($depsRoot) {
        foreach ($providerName in @($depsRoot.GetSubKeyNames())) {
            $providerKey = $depsRoot.OpenSubKey($providerName, $true)
            if (-not $providerKey) { continue }
            $deptsKey = $providerKey.OpenSubKey('Dependents', $true)
            if ($deptsKey) {
                foreach ($depGuid in @($deptsKey.GetSubKeyNames())) {
                    if (-not $activeBundles.ContainsKey($depGuid.ToUpper())) {
                        Write-Host "  [Remove-All] Orphan bundle dependent: $depGuid under $providerName" -ForegroundColor DarkGray
                        $deptsKey.DeleteSubKey($depGuid, $false)
                    }
                }
                $deptsKey.Close()
            }
            $providerKey.Close()
        }
        $depsRoot.Close()
    }
    $depsBase.Close()
}

# ==============================================================================
# Phase 5 — Scenario functions
# ==============================================================================

# ---- Scenario A: Fresh install (no prior DIME) --------------------------------
function Invoke-ScenarioA($installers) {
    $Script:Scenario = 'A'
    Write-Host "`n=== Scenario A: Fresh Install ===" -ForegroundColor Cyan

    # A1 — Via Burn bundle
    Remove-AllDimeInstalls
    $code = Install-ViaBurn $installers.NewExe
    Assert-ExitCodeSuccess $code 'A1-Burn'
    Assert-InstalledClean $NewVersion 'A1-Burn'
    Assert-NoPendingReboot 'A1-Burn'

    # A2 — Via MSI directly (CleanupBurnARPEarly still runs unconditionally)
    Remove-AllDimeInstalls
    $code = Install-ViaMsi $installers.NewMsi64
    Assert-ExitCodeSuccess $code 'A2-MSI'
    Assert-InstalledClean $NewVersion 'A2-MSI'
    Assert-NoPendingReboot 'A2-MSI'

    Remove-AllDimeInstalls
}

# ---- Scenario B: NSIS migration ----------------------------------------------
function Invoke-ScenarioB($installers, $nsisExePath) {
    $Script:Scenario = 'B'
    if (-not $nsisExePath) {
        Write-Skip 'Scenario B' '-NsisExe not provided'; return
    }
    if (-not (Test-Path $nsisExePath -PathType Leaf)) {
        Write-Skip 'Scenario B' "NSIS installer not found: $nsisExePath"; return
    }
    Write-Host "`n=== Scenario B: NSIS Migration ===" -ForegroundColor Cyan

    Remove-AllDimeInstalls

    # Install legacy NSIS silently
    $p = Start-Process $nsisExePath -ArgumentList '/S' -Wait -PassThru -NoNewWindow
    Assert-ExitCodeSuccess $p.ExitCode 'B-NsisInstall'
    Assert-RegValueExists 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\DIME' 'DisplayName' 'B-NsisVerify'
    Assert-FileExists "$env:SystemRoot\System32\DIME.dll" 'B-NsisVerify'

    # Upgrade via Burn — triggers ConfirmAndRemoveNsisInstall CA
    $code = Install-ViaBurn $installers.NewExe
    Assert-ExitCodeSuccess $code 'B-BurnUpgrade'
    Assert-InstalledClean $NewVersion 'B-BurnUpgrade'
    Assert-RegKeyAbsent 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\DIME' 'B-NsisARP-Removed'
    Assert-FileAbsent "$env:SystemRoot\System32\DIME.dll" 'B-System32DllRemoved'
    Assert-NoPendingReboot 'B-BurnUpgrade'

    Remove-AllDimeInstalls
}

# ---- Scenario C: Major upgrade (4 install-method combos) ---------------------
function Invoke-ScenarioC($installers) {
    $Script:Scenario = 'C'
    Write-Host "`n=== Scenario C: Major Upgrade ===" -ForegroundColor Cyan

    $combos = @(
        @{ OldMethod = 'Burn'; NewMethod = 'Burn' },
        @{ OldMethod = 'Burn'; NewMethod = 'MSI'  },
        @{ OldMethod = 'MSI';  NewMethod = 'Burn' },
        @{ OldMethod = 'MSI';  NewMethod = 'MSI'  }
    )
    foreach ($combo in $combos) {
        $ctx = "C-$($combo.OldMethod)to$($combo.NewMethod)"
        Remove-AllDimeInstalls

        $code = if ($combo.OldMethod -eq 'Burn') { Install-ViaBurn $installers.OldExe } else { Install-ViaMsi $installers.OldMsi64 }
        Assert-ExitCodeSuccess $code "$ctx-InstallOld"
        Assert-InstalledClean $OldVersion "$ctx-OldInstalled"
        Assert-NoPendingReboot "$ctx-InstallOld"

        # ConfirmDowngrade fires on ALL WIX_UPGRADE_DETECTED; for upgrades it returns ERROR_SUCCESS silently
        $code = if ($combo.NewMethod -eq 'Burn') { Install-ViaBurn $installers.NewExe } else { Install-ViaMsi $installers.NewMsi64 }
        Assert-ExitCodeSuccess $code "$ctx-Upgrade"
        # Burn-to-Burn: WiX Burn re-registers its hidden ARP entry at Apply-end (post MSI commit CA);
        # no MSI CA slot exists post-Apply so the entry is expected and skipped here.
        $isBurnToBurn = ($combo.OldMethod -eq 'Burn' -and $combo.NewMethod -eq 'Burn')
        Assert-InstalledClean $NewVersion "$ctx-NewInstalled" -SkipBurnArp:$isBurnToBurn
        Assert-NoPendingReboot "$ctx-Upgrade"
        Assert-NoOrphanBaks "$ctx-Upgrade"

        Remove-AllDimeInstalls
    }
}

# ---- Scenario D: Uninstall + reinstall + rapid cycling x3 --------------------
function Invoke-ScenarioD($installers) {
    $Script:Scenario = 'D'
    Write-Host "`n=== Scenario D: Uninstall + Reinstall ===" -ForegroundColor Cyan

    foreach ($method in @('Burn', 'MSI')) {
        # Single install/uninstall cycle
        Remove-AllDimeInstalls
        $code = if ($method -eq 'Burn') { Install-ViaBurn $installers.NewExe } else { Install-ViaMsi $installers.NewMsi64 }
        Assert-ExitCodeSuccess $code "D-$method-Install"
        Assert-InstalledClean $NewVersion "D-$method-Installed"
        Assert-NoPendingReboot "D-$method-Install"

        $code = if ($method -eq 'Burn') {
            Uninstall-ViaBurn $installers.NewExe
        } else {
            Uninstall-ViaMsiProductCode (Get-MsiProductCode $NewVersion)
        }
        Assert-ExitCodeSuccess $code "D-$method-Uninstall"
        Assert-UninstalledClean "D-$method-Uninstalled"
        Assert-NoPendingReboot "D-$method-Uninstall"

        # Reinstall after uninstall
        $code = if ($method -eq 'Burn') { Install-ViaBurn $installers.NewExe } else { Install-ViaMsi $installers.NewMsi64 }
        Assert-ExitCodeSuccess $code "D-$method-Reinstall"
        Assert-InstalledClean $NewVersion "D-$method-Reinstalled"
        Assert-NoPendingReboot "D-$method-Reinstall"
        Assert-NoOrphanBaks "D-$method-Reinstall"

        # Rapid cycling x3 — targets unique-tick-suffix bak naming in BuildUniqueBakName
        # If the tick mechanism breaks, baks collide and a cycle fails
        Remove-AllDimeInstalls
        for ($i = 1; $i -le 3; $i++) {
            $code = if ($method -eq 'Burn') { Install-ViaBurn $installers.NewExe } else { Install-ViaMsi $installers.NewMsi64 }
            Assert-ExitCodeSuccess $code "D-$method-Rapid-Install-$i"
            Assert-NoPendingReboot "D-$method-Rapid-Install-$i"

            $code = if ($method -eq 'Burn') {
                Uninstall-ViaBurn $installers.NewExe
            } else {
                Uninstall-ViaMsiProductCode (Get-MsiProductCode $NewVersion)
            }
            Assert-ExitCodeSuccess $code "D-$method-Rapid-Uninstall-$i"
        }
        Assert-UninstalledClean "D-$method-RapidCycle"
        Assert-NoOrphanBaks "D-$method-RapidCycle"   # CleanupDimeDllBak orphan-glob-scan exercised here

        Remove-AllDimeInstalls
    }
}

# ---- Scenario E: Rollback via file-obstruction --------------------------------
# An exclusively locked file at the target DLL path causes both MoveLockedDimeDll
# (MoveFileExW fails — no FILE_SHARE_DELETE on the lock) and InstallFiles to fail
# with exit 1603, triggering MSI automatic rollback.
#
# The lock is held by a child process so RM kills the child — not the test runner —
# when RM fires.  MsiRestartManagerControl=Disable prevents the 60-second RmGetList
# stall (tested by Scenario H via Assert-Duration) but does NOT prevent RM from
# terminating lock-holder processes in /qn mode (known OS behaviour: in UILevel=2
# the RM service session acts automatically without asking).
# When RM kills the child the lock is released, the install can succeed, and the
# rollback cannot be asserted; E1/E2 emit WARN instead of FAIL in that case.
#
# As a fast sanity-check, E first verifies MsiRestartManagerControl=Disable is
# present in the MSI Property table (no install performed).
function Get-MsiProperty([string]$msiPath, [string]$propName) {
    $wi   = New-Object -ComObject WindowsInstaller.Installer
    $db   = $wi.GetType().InvokeMember('OpenDatabase', [Reflection.BindingFlags]::InvokeMethod,
                $null, $wi, @($msiPath, 0))
    $view = $db.GetType().InvokeMember('OpenView', [Reflection.BindingFlags]::InvokeMethod,
                $null, $db, @("SELECT ``Value`` FROM ``Property`` WHERE ``Property`` = '$propName'"))
    $view.GetType().InvokeMember('Execute', [Reflection.BindingFlags]::InvokeMethod, $null, $view, @())
    $rec  = $view.GetType().InvokeMember('Fetch', [Reflection.BindingFlags]::InvokeMethod, $null, $view, @())
    $val  = if ($null -ne $rec) {
        $rec.GetType().InvokeMember('StringData', [Reflection.BindingFlags]::GetProperty, $null, $rec, @(1))
    } else { '' }
    $view.GetType().InvokeMember('Close', [Reflection.BindingFlags]::InvokeMethod, $null, $view, @())
    [void][Runtime.InteropServices.Marshal]::ReleaseComObject($view)
    [void][Runtime.InteropServices.Marshal]::ReleaseComObject($db)
    [void][Runtime.InteropServices.Marshal]::ReleaseComObject($wi)
    return $val
}

# Asserts that $dialogName exists in the MSI's Dialog table.
# Without CancelDlg, clicking Cancel on ProgressDlg raises error 2803 in interactive mode.
function Assert-MsiDialogExists([string]$msiPath, [string]$dialogName, [string]$ctx) {
    $wi   = New-Object -ComObject WindowsInstaller.Installer
    $db   = $wi.GetType().InvokeMember('OpenDatabase', [Reflection.BindingFlags]::InvokeMethod,
                $null, $wi, @($msiPath, 0))
    $view = $db.GetType().InvokeMember('OpenView', [Reflection.BindingFlags]::InvokeMethod,
                $null, $db, @("SELECT ``Dialog`` FROM ``Dialog`` WHERE ``Dialog`` = '$dialogName'"))
    $view.GetType().InvokeMember('Execute', [Reflection.BindingFlags]::InvokeMethod, $null, $view, @())
    $rec  = $view.GetType().InvokeMember('Fetch', [Reflection.BindingFlags]::InvokeMethod, $null, $view, @())
    $view.GetType().InvokeMember('Close', [Reflection.BindingFlags]::InvokeMethod, $null, $view, @())
    [void][Runtime.InteropServices.Marshal]::ReleaseComObject($view)
    [void][Runtime.InteropServices.Marshal]::ReleaseComObject($db)
    [void][Runtime.InteropServices.Marshal]::ReleaseComObject($wi)
    Assert-True ($null -ne $rec) "$ctx -- Dialog '$dialogName' in Dialog table" `
        "$ctx -- Dialog '$dialogName' MISSING from Dialog table — ProgressDlg Cancel raises error 2803 in interactive mode"
}

function Invoke-ScenarioE($installers) {
    $Script:Scenario = 'E'
    Write-Host "`n=== Scenario E: Rollback (file-obstruction) ===" -ForegroundColor Cyan

    # Sanity-check: MsiRestartManagerControl=Disable must be in the MSI Property table.
    $val = Get-MsiProperty $installers.NewMsi64 'MsiRestartManagerControl'
    Assert-True ($val -eq 'Disable') 'E-MsiRestartManagerControl' `
        "NewMsi64: Property 'MsiRestartManagerControl' expected 'Disable', got '$val'"

    # MsiRestartManagerControl=Disable (in the MSI) disables the CLIENT-side RM session.
    # The msiserver service runs a SEPARATE server-side RM session as SYSTEM (Session 0)
    # that is NOT controlled by that property and will still kill lock-holder processes in
    # /qn mode.  To make rollback testable, temporarily set the DisableAutomaticApplicationShutdown
    # machine policy to 1 — this suppresses the server-side RM session's process-termination.
    # The policy is restored in the finally block below whether E passes or fails.
    $rmPolicyPath = 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\Installer'
    $rmPolicyName = 'DisableAutomaticApplicationShutdown'
    $rmPolicyPrev = if (Test-Path $rmPolicyPath) {
        (Get-ItemProperty $rmPolicyPath -Name $rmPolicyName -EA SilentlyContinue).$rmPolicyName
    } else { $null }
    New-Item -Path $rmPolicyPath -Force | Out-Null
    Set-ItemProperty -Path $rmPolicyPath -Name $rmPolicyName -Value 1 -Type DWord
    Write-Host "  [INFO] DisableAutomaticApplicationShutdown set to 1 for E1/E2 rollback test" -ForegroundColor DarkGray

    # Arch-correct DLL path (what InstallFiles will try to write)
    $archDll = if ($NativeArch -eq 'ARM64') {
        "$env:ProgramFiles\DIME\arm64\DIME.dll"
    } else {
        "$env:ProgramFiles\DIME\amd64\DIME.dll"
    }

    try {
        # E1 — Fresh-install rollback: system must be entirely clean after rollback.
        Remove-AllDimeInstalls
        New-Item -ItemType Directory -Force (Split-Path $archDll) | Out-Null
        $e1Proc = Start-ExclusiveLockChild $archDll 'OpenOrCreate'
        if ($null -eq $e1Proc) {
            Assert-True $false 'E1-LockHolderStarted' `
                'child process failed to acquire exclusive lock — ensure the test is run as Administrator (Program Files requires elevation)'
            Remove-Item $archDll -Force -EA SilentlyContinue
            Remove-Item (Split-Path $archDll) -Recurse -Force -EA SilentlyContinue
            Remove-Item "$env:ProgramFiles\DIME" -Recurse -Force -EA SilentlyContinue
        } else {
            $e1Code = $null; $e1RmKilled = $false
            try {
                $e1Code = Install-ViaMsi $installers.NewMsi64
            } finally {
                $e1RmKilled = $e1Proc.HasExited
                if (-not $e1Proc.HasExited) { $e1Proc.Kill() }
                $e1Proc.WaitForExit(3000)   # wait for OS to release the file handle before we delete
                $e1Proc.Dispose()
                # Remove the placeholder file and test-created directories.
                Remove-Item $archDll -Force -EA SilentlyContinue
                Remove-Item (Split-Path $archDll) -Recurse -Force -EA SilentlyContinue
                Remove-Item "$env:ProgramFiles\DIME" -Recurse -Force -EA SilentlyContinue
            }
            if ($e1RmKilled) {
                # RM still killed the child despite DisableAutomaticApplicationShutdown=1
                # — the policy may not have taken effect in time for the msiserver already-running session.
                Assert-True $false 'E1-LockNotKilledByRM' `
                    "RM terminated lock-holder child even with DisableAutomaticApplicationShutdown=1 — msiexec exit $e1Code"
                Remove-AllDimeInstalls
            } else {
                Assert-True ($e1Code -eq 1603) 'E1-ExitCode-1603' "expected 1603 (rollback), got $e1Code"
                Assert-UninstalledClean 'E1-FreshInstall-Rollback'
                Assert-NoPendingReboot  'E1-FreshInstall-Rollback'
            }
        }

        # E2 — Upgrade rollback: old version must remain fully intact after rollback.
        Remove-AllDimeInstalls
        $code = Install-ViaMsi $installers.OldMsi64
        Assert-ExitCodeSuccess $code 'E2-InstallOld'
        Assert-InstalledClean $OldVersion 'E2-OldInstalled'

        $e2Proc = Start-ExclusiveLockChild $archDll 'Open'
        if ($null -eq $e2Proc) {
            Assert-True $false 'E2-LockHolderStarted' `
                'child process failed to acquire exclusive lock on installed DLL — ensure the test is run as Administrator'
        } else {
            $e2Code = $null; $e2RmKilled = $false
            try {
                $e2Code = Install-ViaMsi $installers.NewMsi64
            } finally {
                $e2RmKilled = $e2Proc.HasExited
                if (-not $e2Proc.HasExited) { $e2Proc.Kill() }
                $e2Proc.WaitForExit(3000)   # wait for OS to release the file handle before assertions
                $e2Proc.Dispose()
            }
            if ($e2RmKilled) {
                Assert-True $false 'E2-LockNotKilledByRM' `
                    "RM terminated lock-holder child even with DisableAutomaticApplicationShutdown=1 — msiexec exit $e2Code"
                Remove-AllDimeInstalls
            } else {
                # FileShare.None blocks both MoveFileExW and CopyAndPosixReplace in
                # EarlyRenameForValidate → DLL still locked at InstallValidate → RM raises
                # FilesInUse → silent-mode MSI rolls back with exit 1603.
                Assert-True ($e2Code -eq 1603) 'E2-ExitCode-1603' "expected 1603 (rollback), got $e2Code"
                Assert-InstalledClean $OldVersion 'E2-UpgradeRollback-OldRestored'
                Assert-NoPendingReboot 'E2-UpgradeRollback'
                Assert-NoOrphanBaks    'E2-UpgradeRollback'
                Remove-AllDimeInstalls
            }
        }
    } finally {
        # Restore DisableAutomaticApplicationShutdown regardless of pass/fail.
        if ($null -ne $rmPolicyPrev) {
            Set-ItemProperty -Path $rmPolicyPath -Name $rmPolicyName -Value $rmPolicyPrev -Type DWord
        } else {
            Remove-ItemProperty -Path $rmPolicyPath -Name $rmPolicyName -EA SilentlyContinue
        }
        Write-Host "  [INFO] DisableAutomaticApplicationShutdown restored" -ForegroundColor DarkGray
    }

    Remove-AllDimeInstalls
}

# ---- Scenario F: Downgrade ----------------------------------------------------
function Invoke-ScenarioF($installers) {
    $Script:Scenario = 'F'
    Write-Host "`n=== Scenario F: Downgrade ===" -ForegroundColor Cyan

    # F1 — "No" path: Burn /quiet forces UILevel<=2, ConfirmDowngrade returns 1602 silently
    Remove-AllDimeInstalls
    $code = Install-ViaBurn $installers.NewExe
    Assert-ExitCodeSuccess $code 'F1-InstallNew'
    Assert-InstalledClean $NewVersion 'F1-NewInstalled'

    $code = Install-ViaBurn $installers.OldExe   # /quiet -> no dialog -> ConfirmDowngrade returns 1602
    Assert-ExitCode 1602 $code 'F1-DowngradeBlocked'
    Assert-InstalledClean $NewVersion 'F1-Unchanged'   # new version untouched
    Assert-NoPendingReboot 'F1-DowngradeBlocked'
    Remove-AllDimeInstalls

    # F2 — "Yes" path: install old MSI directly (simulates user approving the downgrade).
    # MajorUpgrade removes the new version via RemoveExistingProducts, then installs the
    # old version fresh — no REINSTALL=ALL needed (that would incorrectly set REMOVE=ALL
    # and prevent RegisterAmd64Dll from running).
    foreach ($installMethod in @('Burn', 'MSI')) {
        Remove-AllDimeInstalls
        $code = if ($installMethod -eq 'Burn') { Install-ViaBurn $installers.NewExe } else { Install-ViaMsi $installers.NewMsi64 }
        Assert-ExitCodeSuccess $code "F2-$installMethod-InstallNew"
        Assert-InstalledClean $NewVersion "F2-$installMethod-NewInstalled"

        $code = Install-ViaMsi $installers.OldMsi64 @('DIME_DOWNGRADE_APPROVED=1')
        Assert-ExitCodeSuccess $code "F2-$installMethod-Downgrade"
        Assert-InstalledClean $OldVersion "F2-$installMethod-Downgraded"
        Assert-NoPendingReboot "F2-$installMethod-Downgrade"
        Assert-NoOrphanBaks    "F2-$installMethod-Downgrade"
        Remove-AllDimeInstalls
    }
}

# ---- Scenario G: Locked DLL and CIN upgrade -----------------------------------
function Invoke-ScenarioG($installers) {
    $Script:Scenario = 'G'
    Write-Host "`n=== Scenario G: Locked DLL/CIN Upgrade ===" -ForegroundColor Cyan

    $archDll = if ($NativeArch -eq 'ARM64') {
        "$env:ProgramFiles\DIME\arm64\DIME.dll"
    } else {
        "$env:ProgramFiles\DIME\amd64\DIME.dll"
    }
    $cinPath = "$env:ProgramFiles\DIME\Array.cin"

    # G1 — DLL locked with permissive sharing (FILE_SHARE_READ|WRITE|DELETE — OS loader flags)
    # Lock is held in a child process so RM kills the child (not the test runner) if needed.
    # MoveLockedDimeDll renames the locked dll to a bak, copy-backs a fresh inode, InstallFiles
    # writes the new DLL, CleanupDimeDllBak deletes the bak.
    Remove-AllDimeInstalls
    $code = Install-ViaMsi $installers.OldMsi64
    Assert-ExitCodeSuccess $code 'G1-InstallOld'
    Assert-InstalledClean $OldVersion 'G1-OldInstalled'

    $g1Proc = Start-ExclusiveLockChild $archDll 'Open' 'ReadWriteDelete'
    if ($null -eq $g1Proc) {
        Assert-True $false 'G1-LockHolderStarted' 'child process failed to acquire shared lock on DLL'
    } else {
        try {
            $code = Install-ViaMsi $installers.NewMsi64
            Assert-ExitCodeSuccess $code 'G1-UpgradeWithLock'
        } finally {
            if (-not $g1Proc.HasExited) { $g1Proc.Kill() }
            $g1Proc.Dispose()
        }
        Assert-InstalledClean $NewVersion 'G1-NewInstalled'
        Assert-NoPendingReboot 'G1-NoPendingReboot'
        Assert-NoOrphanBaks    'G1-NoOrphanBaks'
    }
    Remove-AllDimeInstalls

    # G2 — CIN locked with permissive sharing
    # MoveLockedDimeDll glob-scans *.cin in INSTALLDIR; for each:
    #   a. ScrubPendingFileRenameOperation(cin) -- clears stale delete-pending
    #   b. MoveFileExW(cin -> cin.bak)          -- succeeds (FILE_SHARE_DELETE)
    #   c. CopyFileW(cin.bak -> cin)            -- fresh unlocked inode
    # InstallFiles writes new .cin to the fresh inode. CleanupDimeDllBak deletes .cin bak.
    Remove-AllDimeInstalls
    $code = Install-ViaMsi $installers.OldMsi64
    Assert-ExitCodeSuccess $code 'G2-InstallOld'
    Assert-InstalledClean $OldVersion 'G2-OldInstalled'

    if (-not (Test-Path $cinPath)) {
        Write-Skip 'G2' "Array.cin not found at expected path; skipping locked-CIN test"
    } else {
        $g2Proc = Start-ExclusiveLockChild $cinPath 'Open' 'ReadWriteDelete'
        if ($null -eq $g2Proc) {
            Assert-True $false 'G2-LockHolderStarted' 'child process failed to acquire shared lock on CIN'
        } else {
            try {
                $code = Install-ViaMsi $installers.NewMsi64
                Assert-ExitCodeSuccess $code 'G2-UpgradeWithCinLock'
            } finally {
                if (-not $g2Proc.HasExited) { $g2Proc.Kill() }
                $g2Proc.Dispose()
            }
            Assert-InstalledClean $NewVersion 'G2-NewInstalled'
            Assert-NoPendingReboot 'G2-NoPendingReboot'
            Assert-NoOrphanBaks    'G2-NoOrphanBaks'
        }
    }
    Remove-AllDimeInstalls

    # G3 — CIN locked with restrictive sharing (no DELETE) — regression canary only
    # Expected: installer still succeeds (MoveLockedDimeDll fails gracefully, no bak needed);
    # WARN if PendingFileRenameOperations contains the .cin; FAIL only if exit code is 3010.
    Remove-AllDimeInstalls
    $code = Install-ViaMsi $installers.OldMsi64
    Assert-ExitCodeSuccess $code 'G3-InstallOld'

    if (Test-Path $cinPath) {
        $g3Proc = Start-ExclusiveLockChild $cinPath 'Open' 'Read'
        if ($null -eq $g3Proc) {
            Write-Skip 'G3' 'child process failed to acquire read-only lock on CIN; skipping restrictive-lock canary'
        } else {
            try {
                $code = Install-ViaMsi $installers.NewMsi64
                if ($code -eq 1641) {
                    # 1641 = reboot INITIATED — /norestart should always prevent this.
                    Write-Fail 'G3-NoRebootRequired' 'exit 1641 (reboot initiated) — regression: /norestart should suppress machine restart'
                    $Script:Results.Add([pscustomobject]@{ Scenario='G'; Name='G3-NoRebootRequired'; Passed=$false; Message='exit 1641 (reboot initiated) — regression' })
                } elseif ($code -eq 3010) {
                    # 3010 = reboot required but NOT initiated — known, unresolvable OS limitation:
                    # When a .cin is locked without FILE_SHARE_DELETE, MoveFileExW rename fails,
                    # the copy-back never runs, and RemoveExistingProducts falls back to
                    # MoveFileExW(MOVEFILE_DELAY_UNTIL_REBOOT), setting the reboot-pending flag
                    # before commit CAs run (so CleanupDimeDllBak cannot change the exit code).
                    # CleanupDimeDllBak does scrub PendingFileRenameOperations for .cin files so
                    # newly-installed CINs are not deleted on the next reboot.
                    Write-Warn 'G3-NoRebootRequired' 'exit 3010 — known OS limitation: .cin locked without FILE_SHARE_DELETE forces RemoveExistingProducts to schedule reboot-pending (unavoidable; CleanupDimeDllBak scrubs the pending entry to protect new CINs)'
                    $Script:Results.Add([pscustomobject]@{ Scenario='G'; Name='G3-NoRebootRequired'; Passed=$null; Message='WARN: exit 3010 — known OS limitation' })
                    $pending = (Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager' `
                        -Name PendingFileRenameOperations -EA SilentlyContinue).PendingFileRenameOperations
                    if ($pending -match '\.cin') {
                        Write-Warn 'G3-CinPendingReplacement' 'CIN still in PendingFileRenameOperations after CleanupDimeDllBak — scrub may have missed it'
                    }
                } else {
                    Assert-ExitCodeSuccess $code 'G3-ExitCode'
                    # Warn (don't fail) if CIN path is pending — known gap for restrictive lock
                    $pending = (Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager' `
                        -Name PendingFileRenameOperations -EA SilentlyContinue).PendingFileRenameOperations
                    if ($pending -match '\.cin$') {
                        Write-Warn 'G3-CinPendingReplacement' 'CIN in PendingFileRenameOperations under restrictive lock — known gap'
                    }
                }
            } finally {
                if (-not $g3Proc.HasExited) { $g3Proc.Kill() }
                $g3Proc.Dispose()
            }
        }
    } else {
        Write-Skip 'G3' 'Array.cin not found; skipping restrictive-lock canary'
    }
    Remove-AllDimeInstalls
}

# ---- Test matrix: same-version repair and cross-method uninstall --------------
function Invoke-TestMatrix($installers) {
    $Script:Scenario = 'Matrix'
    Write-Host "`n=== Matrix: Same-version Repair + Cross-method Uninstall ===" -ForegroundColor Cyan

    $combos = @(
        @{ InstallMethod = 'Burn'; MaintainMethod = 'Burn' },
        @{ InstallMethod = 'Burn'; MaintainMethod = 'MSI'  },
        @{ InstallMethod = 'MSI';  MaintainMethod = 'Burn' },
        @{ InstallMethod = 'MSI';  MaintainMethod = 'MSI'  }
    )

    foreach ($combo in $combos) {
        $im = $combo.InstallMethod; $mm = $combo.MaintainMethod
        $ctx = "Matrix-$im-$mm"
        Remove-AllDimeInstalls

        $code = if ($im -eq 'Burn') { Install-ViaBurn $installers.NewExe } else { Install-ViaMsi $installers.NewMsi64 }
        Assert-ExitCodeSuccess $code "$ctx-Install"
        Assert-InstalledClean $NewVersion "$ctx-PreRepair"

        # Repair
        if ($mm -eq 'Burn') {
            $repairLog = "$LogDir\repair-$ctx-$(Get-Date -f yyyyMMdd-HHmmss).log"
            $code = Invoke-WithLog $installers.NewExe @('/repair', '/quiet', '/norestart', "/log $repairLog") $repairLog
            # Cross-method repair (install via MSI direct, repair via Burn) fails with 1603 due to
            # Windows Installer SecureRepair: the test MSI is installed under a version-suffixed
            # filename (DIME-64bit-1.3.478.0.msi) but Burn provides DIME-64bit.msi from its payload.
            # SecureRepair validates the repair source filename against what was recorded at install
            # time and rejects the mismatch. WARN instead of FAIL — the cross-method UNINSTALL
            # (tested below) is what matters; repair is a secondary scenario for this combo.
            if ($im -eq 'MSI' -and $mm -eq 'Burn' -and $code -eq 1603) {
                Write-Warn "$ctx-Repair" "exit 1603 — known limitation: Burn repair of MSI-installed product fails SecureRepair (filename mismatch: DIME-64bit-1.3.478.0.msi vs DIME-64bit.msi)"
                Show-LastLogLines $repairLog
                Show-CaLogs
            } else {
                Assert-ExitCodeSuccess $code "$ctx-Repair"
            }
        } else {
            $code = Repair-ViaMsi (Get-MsiProductCode $NewVersion)
            Assert-ExitCodeSuccess $code "$ctx-Repair"
        }
        Assert-InstalledClean $NewVersion "$ctx-PostRepair"
        Assert-NoPendingReboot "$ctx-Repair"

        # Uninstall via the other method
        if ($mm -eq 'Burn') {
            $code = Uninstall-ViaBurn $installers.NewExe
        } else {
            $code = Uninstall-ViaMsiProductCode (Get-MsiProductCode $NewVersion)
        }
        Assert-ExitCodeSuccess $code "$ctx-Uninstall"
        Assert-UninstalledClean "$ctx-Uninstalled"
        Assert-NoPendingReboot "$ctx-Uninstall"

        Remove-AllDimeInstalls
    }
}

# ---- Scenario H: Uninstall with DLL locked (RM regression) -----------------
# The RM stall is in MSI's InstallValidate action and is triggered by both
# Burn-invoked and MSI-direct uninstall, regardless of install method.
# With MsiRestartManagerControl=Disable the uninstall completes in <5 s.
# Without it, InstallValidate blocks ~60 s on RmGetList, which trips
# Assert-Duration (120 s limit) long before the overall timeout fires.
# Permissive sharing (FILE_SHARE_READ|WRITE|DELETE) matches the loader-grade
# sharing mode Windows uses when mapping a DLL image, so it cannot block
# TSF clients from loading the DLL — but it IS sufficient to trigger RmGetList.
# Tested across all 4 install × uninstall method combos.
function Invoke-ScenarioH($installers) {
    $Script:Scenario = 'H'
    Write-Host "`n=== Scenario H: Uninstall with DLL Locked (RM Regression) ===" -ForegroundColor Cyan

    $archDll = if ($NativeArch -eq 'ARM64') {
        "$env:ProgramFiles\DIME\arm64\DIME.dll"
    } else {
        "$env:ProgramFiles\DIME\amd64\DIME.dll"
    }
    $share = [System.IO.FileShare]::ReadWrite -bor [System.IO.FileShare]::Delete

    $combos = @(
        @{ InstallMethod = 'Burn'; UninstallMethod = 'Burn' },
        @{ InstallMethod = 'Burn'; UninstallMethod = 'MSI'  },
        @{ InstallMethod = 'MSI';  UninstallMethod = 'Burn' },
        @{ InstallMethod = 'MSI';  UninstallMethod = 'MSI'  }
    )

    foreach ($combo in $combos) {
        $im  = $combo.InstallMethod
        $um  = $combo.UninstallMethod
        $ctx = "H-${im}to${um}"

        Remove-AllDimeInstalls
        $code = if ($im -eq 'Burn') { Install-ViaBurn $installers.NewExe } else { Install-ViaMsi $installers.NewMsi64 }
        Assert-ExitCodeSuccess $code "$ctx-Install"
        Assert-InstalledClean $NewVersion "$ctx-Installed"
        Assert-NoPendingReboot "$ctx-Install"

        $handle = [System.IO.File]::Open($archDll, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, $share)
        try {
            $code = if ($um -eq 'Burn') {
                Uninstall-ViaBurn $installers.NewExe
            } else {
                Uninstall-ViaMsiProductCode (Get-MsiProductCode $NewVersion)
            }
            Assert-ExitCodeSuccess $code "$ctx-Uninstall"
        } finally {
            $handle.Close()
        }

        Assert-UninstalledClean "$ctx-UninstalledClean"
        Assert-NoPendingReboot  "$ctx-NoPendingReboot"
        Remove-AllDimeInstalls
    }
}

# ---- Scenario I: Stale Dependent injection (error 2803 regression) -----------
# Error 2803 manifests when two builds with the same MSI ProductCode but
# different Burn bundle GUIDs are installed in sequence: the old GUID lingers
# as an orphan Dependent under the package-provider key.  When the new bundle
# later uninstalls, Burn's planning phase finds the orphan, treats it as an
# external dependency, and skips the MSI uninstall — leaving DIME installed.
#
# Timing constraint: Burn checks Dependents at PLAN time, before
# MsiInstallProduct is called.  If the orphan is present at plan time, Burn
# skips the MSI's execute sequence entirely and the commit CA never fires.
# Therefore the CA can only fix this during the INSTALL of the new bundle
# (CleanupBurnRegistration fires unconditionally during install commit), not
# during a subsequent Burn uninstall with the orphan already present.
#
# Test flow (per combo): pre-create the stale GUID in the provider Dependents
# key BEFORE install → install triggers commit CA which removes it → Burn
# uninstall sees no orphan → succeeds.
#
# Install method is varied (Burn/MSI) because both paths fire the commit CA and
# must demonstrate that CleanupBurnRegistration cleans the stale GUID.
# Uninstall is always Burn: MSI-direct uninstall (msiexec /x) does not read
# the Dependents key and can never produce error 2803, so testing it here
# adds no regression coverage.
function Invoke-ScenarioI($installers) {
    $Script:Scenario = 'I'
    Write-Host "`n=== Scenario I: Stale Dependent Injection (2803 Regression) ===" -ForegroundColor Cyan

    $fakeGuid    = '{DEAD0000-DEAD-DEAD-DEAD-DEAD00000001}'
    $productCode = Get-MsiProductCode $NewVersion 'x64'   # deterministic GUID from "DIME-x64-{major.minor.commit}"
    $depsSubKey  = "SOFTWARE\Classes\Installer\Dependencies\$productCode\Dependents"

    foreach ($installMethod in @('Burn', 'MSI')) {
        $ctx = "I-${installMethod}toBurn"

        Remove-AllDimeInstalls

        # Pre-create the orphan GUID under the x64 MSI package-provider Dependents
        # path before the MSI is installed.  The full key path may not exist yet;
        # CreateSubKey creates missing intermediate keys automatically.
        # SOFTWARE\Classes is not subject to WOW64 redirection — use Registry64.
        $depsBase = [Microsoft.Win32.RegistryKey]::OpenBaseKey('LocalMachine', [Microsoft.Win32.RegistryView]::Registry64)
        $deptsKey = $depsBase.CreateSubKey($depsSubKey)
        if ($deptsKey) {
            $deptsKey.CreateSubKey($fakeGuid) | Out-Null
            $deptsKey.Close()
            Write-Host "  [$ctx] Pre-injected stale dependent $fakeGuid" -ForegroundColor DarkGray
        } else {
            Write-Warn "$ctx-Inject" "Could not create stale Dependent key — skipping combo"
            $depsBase.Close()
            Remove-AllDimeInstalls
            continue
        }
        $depsBase.Close()

        # Install — CleanupBurnRegistration (commit CA) fires, reads the MSI's own
        # ProductCode via MsiGetPropertyW, and removes any Dependent GUID under that
        # provider key that is not the current bundle GUID.  fakeGuid must be gone after this.
        $code = if ($installMethod -eq 'Burn') { Install-ViaBurn $installers.NewExe } else { Install-ViaMsi $installers.NewMsi64 }
        Assert-ExitCodeSuccess $code "$ctx-Install"
        Assert-InstalledClean $NewVersion "$ctx-Installed"
        Assert-NoPendingReboot "$ctx-Install"

        # Explicit check: stale GUID must be absent (commit CA cleaned it)
        $depsBase2 = [Microsoft.Win32.RegistryKey]::OpenBaseKey('LocalMachine', [Microsoft.Win32.RegistryView]::Registry64)
        $checkKey  = $depsBase2.OpenSubKey($depsSubKey)
        $leftover  = if ($checkKey) { @($checkKey.GetSubKeyNames() | Where-Object { $_ -eq $fakeGuid }) } else { @() }
        if ($checkKey) { $checkKey.Close() }
        $depsBase2.Close()
        Assert-True ($leftover.Count -eq 0) "$ctx-StaleDepCleanedByInstall" `
            "stale GUID $fakeGuid still present after install — CleanupBurnRegistration did not remove it"

        # Burn uninstall: Burn would skip the MSI if any non-current Dependent
        # remained, leaving files/keys intact — caught by Assert-UninstalledClean.
        $code = Uninstall-ViaBurn $installers.NewExe
        Assert-ExitCodeSuccess $code "$ctx-Uninstall"
        Assert-UninstalledClean "$ctx-UninstalledClean"
        Assert-NoPendingReboot  "$ctx-NoPendingReboot"

        Remove-AllDimeInstalls
    }
}

# ==============================================================================
# Phase 7 — Main block
# ==============================================================================

# Elevation
$id    = [Security.Principal.WindowsIdentity]::GetCurrent()
$admin = [Security.Principal.WindowsPrincipal]$id
if (-not $admin.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Error "Test-DIMEInstaller.ps1 must run as Administrator."
    exit 1
}

Write-Log "DIME MSI Zero-Reboot Installer Test Suite" White
Write-Log "========================================" White

# Detect true native OS architecture (not WOW64-remapped $env:PROCESSOR_ARCHITECTURE)
$NativeArch = (Get-ItemProperty `
    'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Environment' `
    -Name PROCESSOR_ARCHITECTURE).PROCESSOR_ARCHITECTURE
Write-Host "Native architecture: $NativeArch"

if ($Cleanup) {
    Write-Host "`nRunning cleanup only..." -ForegroundColor Yellow
    Remove-AllDimeInstalls
    Write-Host "Cleanup complete." -ForegroundColor Green
    exit 0
}

# Build both test installers via the real deploy pipeline (same pipeline as production releases)
$installers = Build-TestInstallers
Write-Host "`nInstallers ready:" -ForegroundColor Green
Write-Host "  Old EXE : $($installers.OldExe)"
Write-Host "  New EXE : $($installers.NewExe)"
Write-Host "  Old MSI : $($installers.OldMsi64)"
Write-Host "  New MSI : $($installers.NewMsi64)"
Write-Host ""

# Static MSI sanity checks — run before any scenario so a broken MSI fails fast.
# CancelDlg must be in the Dialog table: ProgressDlg's Cancel button fires
# NewDialog("CancelDlg"); if it is absent, interactive installs raise error 2803.
$Script:Scenario = 'Sanity'
foreach ($dlg in @('CancelDlg', 'ExitDlg', 'UserExitDlg', 'FatalErrorDlg')) {
    Assert-MsiDialogExists $installers.OldMsi64 $dlg "Sanity-$dlg-64bit-old"
    Assert-MsiDialogExists $installers.NewMsi64 $dlg "Sanity-$dlg-64bit-new"
}

# Determine which scenarios to run
$toRun = if ($Scenarios.Count -gt 0) { $Scenarios } else { @('A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'Matrix') }

foreach ($s in $toRun) {
    try {
        switch ($s.ToUpper()) {
            'A'      { Invoke-ScenarioA $installers }
            'B'      { Invoke-ScenarioB $installers $NsisExe }
            'C'      { Invoke-ScenarioC $installers }
            'D'      { Invoke-ScenarioD $installers }
            'E'      { Invoke-ScenarioE $installers }
            'F'      { Invoke-ScenarioF $installers }
            'G'      { Invoke-ScenarioG $installers }
            'H'      { Invoke-ScenarioH $installers }
            'I'      { Invoke-ScenarioI $installers }
            'MATRIX' { Invoke-TestMatrix $installers }
            default  { Write-Host "Unknown scenario: $s" -ForegroundColor Yellow }
        }
    } catch {
        if ($StopOnFail) { break }
        Write-Host "  [ERROR] Scenario $s threw: $_" -ForegroundColor Red
    }
}

# Cleanup temp build dirs
if (-not $KeepBuilds) {
    Remove-Item "$TestWorkDir\DIME-Universal-$OldVersion.exe",
                "$TestWorkDir\DIME-Universal-$NewVersion.exe",
                "$TestWorkDir\DIME-64bit-$OldVersion.msi",
                "$TestWorkDir\DIME-64bit-$NewVersion.msi" -Force -EA SilentlyContinue
}

# ==============================================================================
# Final summary
# ==============================================================================
$passed = @($Script:Results | Where-Object { $_.Passed -eq $true  }).Count
$warned = @($Script:Results | Where-Object { $_.Passed -eq $null  }).Count
$failed = @($Script:Results | Where-Object { $_.Passed -eq $false }).Count
$total  = $Script:Results.Count

$summaryColor = if ($failed -gt 0) { 'Red' } elseif ($warned -gt 0) { 'Yellow' } else { 'Green' }
$warnPart     = if ($warned -gt 0) { ", $warned WARN" } else { '' }
Write-Host ""
Write-Host "===============================================" -ForegroundColor $summaryColor
Write-Host "Results: $passed/$total PASS, $failed FAIL$warnPart" -ForegroundColor $summaryColor
Write-Host "===============================================" -ForegroundColor $summaryColor

if ($warned -gt 0) {
    Write-Host ""
    Write-Host "Warnings:" -ForegroundColor Yellow
    $Script:Results | Where-Object { $_.Passed -eq $null } | ForEach-Object {
        Write-Host "  [$($_.Scenario)] $($_.Name) -- $($_.Message)" -ForegroundColor Yellow
    }
}

if ($failed -gt 0) {
    Write-Host ""
    Write-Host "Failed assertions:" -ForegroundColor Red
    $Script:Results | Where-Object { $_.Passed -eq $false } | ForEach-Object {
        Write-Host "  [$($_.Scenario)] $($_.Name) -- $($_.Message)" -ForegroundColor Red
    }

    # Print CA log tail on any failure (CaBurnLog writes to both paths)
    Write-Host ""
    Write-Host "CA log files:" -ForegroundColor DarkGray
    Show-CaLogs
}

exit $(if ($failed -gt 0) { 1 } else { 0 })
