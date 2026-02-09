# ==============================================================================
# DIME Input Method Framework - Installer Deployment Script
# ==============================================================================
# This script:
# 1. Copies build artifacts to Installer directory
# 2. Builds the NSIS installer
# 3. Creates ZIP archive from the installer
# 4. Calculates SHA-256 checksums for both .exe and .zip
# 5. Updates README.md with the checksums
# ==============================================================================

$ErrorActionPreference = "Stop"

Write-Host "===============================================================================" -ForegroundColor Cyan
Write-Host "DIME Installer Deployment" -ForegroundColor Cyan
Write-Host "===============================================================================" -ForegroundColor Cyan
Write-Host ""

# ==============================================================================
# Step 1: Create directory structure
# ==============================================================================
Write-Host "Creating directory structure..." -ForegroundColor Yellow

$directories = @(
    "..\Installer\system32.x86",
    "..\Installer\system32.x64",
    "..\Installer\system32.arm64"
)

foreach ($dir in $directories) {
    if (-not (Test-Path $dir)) {
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
        Write-Host "  Created: $dir" -ForegroundColor Green
    } else {
        Write-Host "  Exists: $dir" -ForegroundColor Gray
    }
}

# ==============================================================================
# Step 2: Copy build artifacts
# ==============================================================================
Write-Host ""
Write-Host "Copying build artifacts..." -ForegroundColor Yellow

$filesToCopy = @(
    @{Source = "..\src\Release\DIMESettings.exe"; Dest = "..\Installer\"; Desc = "Settings application"},
    @{Source = "..\src\Release\x64\DIME.dll"; Dest = "..\Installer\system32.x64\"; Desc = "x64 DLL"},
    @{Source = "..\src\Release\arm64ec\DIME.dll"; Dest = "..\Installer\system32.arm64\"; Desc = "ARM64 DLL"},
    @{Source = "..\src\Release\Win32\DIME.dll"; Dest = "..\Installer\system32.x86\"; Desc = "x86 DLL"}
)

foreach ($file in $filesToCopy) {
    if (Test-Path $file.Source) {
        Copy-Item -Path $file.Source -Destination $file.Dest -Force
        Write-Host "  Copied: $($file.Desc)" -ForegroundColor Green
    } else {
        Write-Host "  WARNING: $($file.Source) not found!" -ForegroundColor Red
    }
}

# ==============================================================================
# Step 3: Build NSIS installer
# ==============================================================================
Write-Host ""
Write-Host "Building NSIS installer..." -ForegroundColor Yellow

# Check for NSIS in common locations
$nsisPath = $null
$nsisPaths = @(
    "C:\Program Files (x86)\NSIS\makensis.exe",
    "C:\Program Files\NSIS\makensis.exe"
)

foreach ($path in $nsisPaths) {
    if (Test-Path $path) {
        $nsisPath = $path
        break
    }
}

if (-not $nsisPath) {
    Write-Host "  ERROR: NSIS not found in standard locations!" -ForegroundColor Red
    Write-Host "  Please install NSIS from https://nsis.sourceforge.io/" -ForegroundColor Red
    Read-Host "Press Enter to exit"
    exit 1
}

Write-Host "  Using NSIS: $nsisPath" -ForegroundColor Gray

# Run NSIS
try {
    & $nsisPath "DIME-x86armUniversal.nsi"
    Write-Host "  Installer built successfully!" -ForegroundColor Green
} catch {
    Write-Host "  ERROR: Failed to build installer!" -ForegroundColor Red
    Write-Host "  $($_.Exception.Message)" -ForegroundColor Red
    Read-Host "Press Enter to exit"
    exit 1
}

Write-Host ""
Write-Host "===============================================================================" -ForegroundColor Cyan
Write-Host "Deployment and Installer packaging done!!" -ForegroundColor Cyan
Write-Host "===============================================================================" -ForegroundColor Cyan

# ==============================================================================
# Step 4: Create ZIP archive
# ==============================================================================
Write-Host ""
Write-Host "===============================================================================" -ForegroundColor Cyan
Write-Host "Creating ZIP archive..." -ForegroundColor Cyan
Write-Host "===============================================================================" -ForegroundColor Cyan
Write-Host ""

$installerFile = "DIME-x86armUniversal.exe"
$zipFile = "DIME-x86armUniversal.zip"

if (-not (Test-Path $installerFile)) {
    Write-Host "ERROR: $installerFile not found!" -ForegroundColor Red
    Read-Host "Press Enter to exit"
    exit 1
}

# Remove existing zip if present
if (Test-Path $zipFile) {
    Remove-Item $zipFile -Force
    Write-Host "  Removed existing $zipFile" -ForegroundColor Gray
}

# Create ZIP archive
Compress-Archive -Path $installerFile -DestinationPath $zipFile -Force
Write-Host "  Created: $zipFile" -ForegroundColor Green
Write-Host ""

# ==============================================================================
# Step 5: Generate SHA-256 checksums and update README.md
# ==============================================================================
Write-Host ""
Write-Host "===============================================================================" -ForegroundColor Cyan
Write-Host "Calculating SHA-256 checksums and updating README.md..." -ForegroundColor Cyan
Write-Host "===============================================================================" -ForegroundColor Cyan
Write-Host ""

# Calculate checksums for both files
$exeHash = (Get-FileHash -Algorithm SHA256 -Path $installerFile).Hash
$zipHash = (Get-FileHash -Algorithm SHA256 -Path $zipFile).Hash
$exeSize = (Get-Item $installerFile).Length
$zipSize = (Get-Item $zipFile).Length
$date = Get-Date -Format "yyyy-MM-dd"

Write-Host "EXE File: $installerFile" -ForegroundColor White
Write-Host "  Size: $exeSize bytes" -ForegroundColor White
Write-Host "  SHA-256: $exeHash" -ForegroundColor Green
Write-Host ""
Write-Host "ZIP File: $zipFile" -ForegroundColor White
Write-Host "  Size: $zipSize bytes" -ForegroundColor White
Write-Host "  SHA-256: $zipHash" -ForegroundColor Green
Write-Host ""

# Update README.md
$readmePath = "..\README.md"

if (-not (Test-Path $readmePath)) {
    Write-Host "WARNING: README.md not found at $readmePath" -ForegroundColor Red
} else {
    # Use .NET methods for better encoding control
    $utf8NoBom = New-Object System.Text.UTF8Encoding $false
    $readmeFullPath = (Resolve-Path $readmePath).Path
    $content = [System.IO.File]::ReadAllText($readmeFullPath, $utf8NoBom)
    
    # Update date pattern - matches (更新日期: YYYY-MM-DD)
    $datePattern = '\(\S+\s*\S*\s*\d{4}-\d{2}-\d{2}\)'
    $content = $content -replace $datePattern, "($([char]0x66F4)$([char]0x65B0)$([char]0x65E5)$([char]0x671F): $date)"
    
    # Update exe hash - more specific pattern to match the table row
    $exePattern = '(\| DIME-x86armUniversal\.exe \| `)([A-F0-9]{64})(`)'
    $content = $content -replace $exePattern, ('${1}' + $exeHash + '${3}')
    
    # Update zip hash
    $zipPattern = '(\| DIME-x86armUniversal\.zip \| `)([A-F0-9]{64}|[^\`]+)(`)'
    $content = $content -replace $zipPattern, ('${1}' + $zipHash + '${3}')
    
    # Write back to file with UTF8 encoding (no BOM)
    [System.IO.File]::WriteAllText($readmeFullPath, $content, $utf8NoBom)
    
    Write-Host "README.md updated with checksums!" -ForegroundColor Green
    Write-Host "  Updated EXE hash: $exeHash" -ForegroundColor Gray
    Write-Host "  Updated ZIP hash: $zipHash" -ForegroundColor Gray
    Write-Host "  Updated date: $date" -ForegroundColor Gray
}

# ==============================================================================
# Summary
# ==============================================================================
Write-Host ""
Write-Host "===============================================================================" -ForegroundColor Cyan
Write-Host "Deployment Complete!" -ForegroundColor Cyan
Write-Host "===============================================================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "CREATED FILES:" -ForegroundColor Yellow
Write-Host "  - DIME-x86armUniversal.exe (Installer)" -ForegroundColor White
Write-Host "  - DIME-x86armUniversal.zip (ZIP Archive)" -ForegroundColor White
Write-Host ""
Write-Host "NEXT STEPS:" -ForegroundColor Yellow
Write-Host "  1. Review the updated README.md with checksums" -ForegroundColor White
Write-Host "  2. Commit both installer files and README.md" -ForegroundColor White
Write-Host "  3. Create GitHub release with both .exe and .zip" -ForegroundColor White
Write-Host "  4. The checksums are now embedded in README.md" -ForegroundColor White
Write-Host ""
Write-Host "===============================================================================" -ForegroundColor Cyan
Write-Host ""

Read-Host "Press Enter to exit"
