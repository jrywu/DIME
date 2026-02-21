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
    & $nsisPath "..\Installer\DIME-Universal.nsi"
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

$installerFile = "DIME-Universal.exe"
$zipFile = "DIME-Universal.zip"

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
    $utf8WithBom = New-Object System.Text.UTF8Encoding $true
    $readmeFullPath = (Resolve-Path $readmePath).Path
    $content = [System.IO.File]::ReadAllText($readmeFullPath, $utf8NoBom)
    # Extract commit count from buildInfo.h
    $buildInfoPath = "..\src\buildInfo.h"
    $commitCount = 0
    if (Test-Path $buildInfoPath) {
        $buildInfoContent = Get-Content $buildInfoPath | Select-String "#define BUILD_COMMIT_COUNT\s+(\d+)"
        if ($buildInfoContent) {
            $commitCount = [int]($buildInfoContent.Matches[0].Groups[1].Value)
        }
    }
    
    # Build the new checksum section with direct Chinese text
    # Note: This script file should be saved as UTF-8 with BOM for proper encoding
    $checksumSection = @"

   **最新開發版本 DIME v1.2.$commitCount SHA-256 CHECKSUM (更新日期: $date):**
   
   | 檔案 | SHA-256 CHECKSUM |
   |------|----------------|
   | DIME-Universal.exe | ``$exeHash`` |
   | DIME-Universal.zip | ``$zipHash`` |

"@
    
    # Replace content between DOWNLOAD_START and DOWNLOAD_END markers
    $downloadSection = @"

   **最新開發版本 DIME v1.2.$commitCount (更新日期: $date)**

"@
    $downloadPattern = '(?s)(   <!-- DOWNLOAD_START -->).*?(   <!-- DOWNLOAD_END -->)'
    $downloadReplacement = "`${1}$downloadSection`${2}"
    $content = $content -replace $downloadPattern, $downloadReplacement

    # Replace content between CHECKSUM_START and CHECKSUM_END markers
    $pattern = '(?s)(   <!-- CHECKSUM_START -->).*?(   <!-- CHECKSUM_END -->)'
    $replacement = "`${1}$checksumSection`${2}"
    $content = $content -replace $pattern, $replacement
    
    


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
Write-Host "  - DIME-Universal.exe (Installer)" -ForegroundColor White
Write-Host "  - DIME-Universal.zip (ZIP Archive)" -ForegroundColor White
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
