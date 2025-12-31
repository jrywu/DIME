# Check if DIME was actually installed successfully

Write-Host "`n=== Checking DIME Installation ===" -ForegroundColor Cyan

# Check registry
Write-Host "`n1. Checking Registry..." -ForegroundColor Yellow
$regPath = "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\DIME"
if (Test-Path $regPath) {
    Write-Host "   [OK] Registry entry found" -ForegroundColor Green
    Get-ItemProperty $regPath | Format-List DisplayName, DisplayVersion, InstallLocation, UninstallString
} else {
    Write-Host "   [FAIL] Registry entry NOT found" -ForegroundColor Red
}

# Check DLL file
Write-Host "`n2. Checking System DLL..." -ForegroundColor Yellow
$dllPath = "$env:SystemRoot\System32\DIME.dll"
if (Test-Path $dllPath) {
    Write-Host "   [OK] DIME.dll found at: $dllPath" -ForegroundColor Green
    $dll = Get-Item $dllPath
    Write-Host "   Size: $($dll.Length) bytes"
    Write-Host "   Modified: $($dll.LastWriteTime)"
} else {
    Write-Host "   [FAIL] DIME.dll NOT found" -ForegroundColor Red
}

# Check program files
Write-Host "`n3. Checking Program Files..." -ForegroundColor Yellow
$installDir = "$env:ProgramFiles\DIME"
if (Test-Path $installDir) {
    Write-Host "   [OK] Install directory found at: $installDir" -ForegroundColor Green
    Get-ChildItem $installDir | Format-Table Name, Length, LastWriteTime -AutoSize
} else {
    Write-Host "   [FAIL] Install directory NOT found" -ForegroundColor Red
}

# Check Start Menu
Write-Host "`n4. Checking Start Menu..." -ForegroundColor Yellow
$startMenuPath = "$env:ProgramData\Microsoft\Windows\Start Menu\Programs\DIME"
if (Test-Path $startMenuPath) {
    Write-Host "   [OK] Start Menu folder found" -ForegroundColor Green
    Get-ChildItem $startMenuPath | Format-Table Name -AutoSize
} else {
    Write-Host "   [FAIL] Start Menu folder NOT found" -ForegroundColor Red
}

# Check for log file
Write-Host "`n5. Checking for installer log..." -ForegroundColor Yellow
$logPath = "$env:TEMP\DIME-Install.log"
if (Test-Path $logPath) {
    Write-Host "   [OK] Log file found at: $logPath" -ForegroundColor Green
    Write-Host "`n   Last 20 lines of log:" -ForegroundColor Cyan
    Get-Content $logPath | Select-Object -Last 20
} else {
    Write-Host "   [INFO] No log file found (logging may not be enabled)" -ForegroundColor Gray
}

Write-Host "`n=== Check Complete ===" -ForegroundColor Cyan
