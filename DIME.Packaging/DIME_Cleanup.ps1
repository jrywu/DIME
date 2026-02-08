# DIME_Cleanup.ps1
# Cleanup script for DIME IME after MSIX uninstall
# This script is deployed to %ProgramData%\DIME\ during registration
# and a Start Menu shortcut is created that survives MSIX uninstall.
#
# Run this script after uninstalling DIME MSIX to clean up:
# 1. COM server registrations (HKLM and HKCU)
# 2. TSF input method profiles and categories
# 3. Deployed DLL files in ProgramData

param(
    [switch]$Silent
)

$ErrorActionPreference = "SilentlyContinue"

# Load Windows Forms for message boxes
Add-Type -AssemblyName System.Windows.Forms

# Check for admin rights
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $isAdmin) {
    if (-not $Silent) {
        $result = [System.Windows.Forms.MessageBox]::Show(
            "DIME 清理程式需要管理員權限才能移除系統登錄項目。`n`n要以管理員身分重新執行嗎？",
            "DIME 清理",
            [System.Windows.Forms.MessageBoxButtons]::YesNo,
            [System.Windows.Forms.MessageBoxIcon]::Question)
        
        if ($result -eq [System.Windows.Forms.DialogResult]::Yes) {
            # Re-launch with elevation
            Start-Process powershell.exe -ArgumentList "-ExecutionPolicy Bypass -NoProfile -File `"$PSCommandPath`"" -Verb RunAs
        }
    }
    exit
}

$DIME_CLSID = "{1DE68A87-FF3B-46A0-8F80-46730B2491B1}"
$cleanupLog = @()

Write-Host "==========================================" -ForegroundColor Cyan
Write-Host "DIME 輸入法清理程式" -ForegroundColor Cyan
Write-Host "==========================================" -ForegroundColor Cyan
Write-Host ""

# 1. Remove COM registration from HKLM (64-bit view)
Write-Host "[1/7] Removing COM registration (64-bit)..." -ForegroundColor Yellow
$comPath64 = "HKLM:\SOFTWARE\Classes\CLSID\$DIME_CLSID"
if (Test-Path $comPath64) {
    Remove-Item -Path $comPath64 -Recurse -Force -ErrorAction SilentlyContinue
    Write-Host "  Removed: $comPath64" -ForegroundColor Green
    $cleanupLog += "COM (64-bit): $comPath64"
} else {
    Write-Host "  Not found: $comPath64" -ForegroundColor Gray
}

# 2. Remove COM registration from HKLM (32-bit view / WOW6432Node)
Write-Host "[2/7] Removing COM registration (32-bit/WOW64)..." -ForegroundColor Yellow
$comPath32 = "HKLM:\SOFTWARE\WOW6432Node\Classes\CLSID\$DIME_CLSID"
if (Test-Path $comPath32) {
    Remove-Item -Path $comPath32 -Recurse -Force -ErrorAction SilentlyContinue
    Write-Host "  Removed: $comPath32" -ForegroundColor Green
    $cleanupLog += "COM (32-bit): $comPath32"
} else {
    Write-Host "  Not found: $comPath32" -ForegroundColor Gray
}

# 3. Remove TSF TIP registration from HKLM
Write-Host "[3/7] Removing TSF TIP registration (HKLM)..." -ForegroundColor Yellow
$tsfTipPathLM = "HKLM:\SOFTWARE\Microsoft\CTF\TIP\$DIME_CLSID"
if (Test-Path $tsfTipPathLM) {
    Remove-Item -Path $tsfTipPathLM -Recurse -Force -ErrorAction SilentlyContinue
    Write-Host "  Removed: $tsfTipPathLM" -ForegroundColor Green
    $cleanupLog += "TSF TIP (64-bit)"
} else {
    Write-Host "  Not found: $tsfTipPathLM" -ForegroundColor Gray
}

# Also check WOW6432Node for TSF
$tsfTipPathLM32 = "HKLM:\SOFTWARE\WOW6432Node\Microsoft\CTF\TIP\$DIME_CLSID"
if (Test-Path $tsfTipPathLM32) {
    Remove-Item -Path $tsfTipPathLM32 -Recurse -Force -ErrorAction SilentlyContinue
    Write-Host "  Removed: $tsfTipPathLM32" -ForegroundColor Green
    $cleanupLog += "TSF TIP (32-bit)"
}

# 4. Remove TSF TIP registration from HKCU
Write-Host "[4/7] Removing TSF TIP registration (HKCU)..." -ForegroundColor Yellow
$tsfTipPathCU = "HKCU:\SOFTWARE\Microsoft\CTF\TIP\$DIME_CLSID"
if (Test-Path $tsfTipPathCU) {
    Remove-Item -Path $tsfTipPathCU -Recurse -Force -ErrorAction SilentlyContinue
    Write-Host "  Removed: $tsfTipPathCU" -ForegroundColor Green
    $cleanupLog += "TSF TIP (使用者)"
} else {
    Write-Host "  Not found: $tsfTipPathCU" -ForegroundColor Gray
}

# 5. Remove TSF Category registrations
Write-Host "[5/7] Removing TSF Category registrations..." -ForegroundColor Yellow
$categoryPaths = @(
    "HKLM:\SOFTWARE\Microsoft\CTF\TIPCategories",
    "HKLM:\SOFTWARE\WOW6432Node\Microsoft\CTF\TIPCategories"
)

foreach ($catBasePath in $categoryPaths) {
    if (Test-Path $catBasePath) {
        # Get all category GUIDs
        Get-ChildItem -Path $catBasePath -ErrorAction SilentlyContinue | ForEach-Object {
            $catGuidPath = $_.PSPath
            $dimePath = Join-Path $catGuidPath $DIME_CLSID
            if (Test-Path $dimePath) {
                Remove-Item -Path $dimePath -Recurse -Force -ErrorAction SilentlyContinue
                Write-Host "  Removed: $dimePath" -ForegroundColor Green
            }
        }
    }
}

# 6. Remove deployed DLLs from ProgramData (only x64 and x86 subfolders, not entire DIME folder)
Write-Host "[6/7] Removing deployed DLLs..." -ForegroundColor Yellow
$deployedBasePath = "$env:ProgramData\DIME"
$x64Path = Join-Path $deployedBasePath "x64"
$x86Path = Join-Path $deployedBasePath "x86"

if (Test-Path $x64Path) {
    Remove-Item -Path $x64Path -Recurse -Force -ErrorAction SilentlyContinue
    Write-Host "  Removed: $x64Path" -ForegroundColor Green
    $cleanupLog += "已部署 DLLs (x64)"
}
if (Test-Path $x86Path) {
    Remove-Item -Path $x86Path -Recurse -Force -ErrorAction SilentlyContinue
    Write-Host "  Removed: $x86Path" -ForegroundColor Green
    $cleanupLog += "已部署 DLLs (x86)"
}
if (-not (Test-Path $x64Path) -and -not (Test-Path $x86Path)) {
    Write-Host "  DLL folders not found (may already be removed)" -ForegroundColor Gray
}

# Also check HKCU COM registration (per-user)
Write-Host ""
Write-Host "[Bonus] Checking HKCU COM registration..." -ForegroundColor Yellow
$comPathCU = "HKCU:\SOFTWARE\Classes\CLSID\$DIME_CLSID"
if (Test-Path $comPathCU) {
    Remove-Item -Path $comPathCU -Recurse -Force -ErrorAction SilentlyContinue
    Write-Host "  Removed: $comPathCU" -ForegroundColor Green
    $cleanupLog += "COM (使用者): $comPathCU"
} else {
    Write-Host "  Not found: $comPathCU" -ForegroundColor Gray
}

# 7. Remove Start Menu shortcut
Write-Host ""
Write-Host "[7/7] Removing Start Menu shortcut..." -ForegroundColor Yellow

# Remove Start Menu shortcut from common Start Menu (ProgramData)
$startMenuPath = Join-Path $env:ProgramData "Microsoft\Windows\Start Menu\Programs"
$shortcutPath = Join-Path $startMenuPath "DIME 清理.lnk"
if (Test-Path $shortcutPath) {
    Remove-Item -Path $shortcutPath -Force -ErrorAction SilentlyContinue
    Write-Host "  Removed shortcut: $shortcutPath" -ForegroundColor Green
    $cleanupLog += "開始功能表捷徑"
} else {
    Write-Host "  Shortcut not found" -ForegroundColor Gray
}

Write-Host ""
Write-Host "==========================================" -ForegroundColor Cyan
Write-Host "清理完成! Cleanup complete!" -ForegroundColor Green
Write-Host "==========================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Note: You may need to restart Windows or sign out/in" -ForegroundColor Yellow
Write-Host "for the IME to be fully removed from the language bar." -ForegroundColor Yellow

# Show completion message box
if (-not $Silent) {
    $message = "DIME 輸入法清理完成!`n`n"
    if ($cleanupLog.Count -gt 0) {
        $message += "已清理項目:`n"
        $message += ($cleanupLog -join "`n")
    } else {
        $message += "未發現需要清理的項目。"
    }
    $message += "`n`n您可能需要重新啟動 Windows 或登出/登入`n以完全移除輸入法。"
    
    [System.Windows.Forms.MessageBox]::Show(
        $message,
        "DIME 清理完成",
        [System.Windows.Forms.MessageBoxButtons]::OK,
        [System.Windows.Forms.MessageBoxIcon]::Information) | Out-Null
}

# Self-cleanup: Remove ProgramData\DIME folder (including this script)
$programDataDIME = Join-Path $env:ProgramData "DIME"
if (Test-Path $programDataDIME) {
    # Schedule deletion after script exits
    $selfCleanupScript = @"
Start-Sleep -Seconds 2
Remove-Item -Path '$programDataDIME' -Recurse -Force -ErrorAction SilentlyContinue
"@
    Start-Process powershell.exe -ArgumentList "-WindowStyle Hidden -ExecutionPolicy Bypass -Command `"$selfCleanupScript`"" -WindowStyle Hidden
    Write-Host ""
    Write-Host "Cleanup files will be removed in 2 seconds..." -ForegroundColor Gray
}
