# ==============================================================================
# DIME Input Method Framework - WiX Installer Deployment Script
# ==============================================================================
# This script:
# 1. Copies build artifacts to Installer directory
# 2. Builds WiX MSIs (DIME-64bit.msi, DIME-32bit.msi) and Burn Bundle (DIME-Universal.exe)
# 3. Creates ZIP archive from the installer
# 4. Calculates SHA-256 checksums for both .exe and .zip
# 5. Updates README.md with the checksums
# ==============================================================================
param(
    # If non-empty, skip BuildInfo.h parsing and use this exact version string (e.g. "1.3.001.0")
    [string] $ProductVersionOverride = "",

    # Write MSI/EXE outputs to this directory instead of $PSScriptRoot (must exist or be creatable)
    [string] $OutputDir = "",

    # Suppress all Read-Host pause prompts (required for scripted/CI builds)
    [switch] $NonInteractive,

    # Skip ZIP creation and README.md checksum update steps
    [switch] $SkipChecksumUpdate
)

$ErrorActionPreference = "Stop"

Write-Host "===============================================================================" -ForegroundColor Cyan
Write-Host "DIME WiX Installer Deployment" -ForegroundColor Cyan
Write-Host "===============================================================================" -ForegroundColor Cyan
Write-Host ""

# Resolve output directory (default to $PSScriptRoot)
$srcDir = if ($OutputDir -ne "") {
    New-Item -ItemType Directory -Force $OutputDir | Out-Null
    (Resolve-Path $OutputDir).Path
} else {
    (Resolve-Path $PSScriptRoot).Path
}

# ==============================================================================
# Step 1: Create directory structure
# ==============================================================================
Write-Host "Creating directory structure..." -ForegroundColor Yellow

$directories = @(
    "..\Installer\x86",
    "..\Installer\amd64",
    "..\Installer\arm64"
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
    @{Source = "..\src\Release\x64\DIME.dll"; Dest = "..\Installer\amd64\"; Desc = "AMD64 DLL"},
    @{Source = "..\src\Release\arm64ec\DIME.dll"; Dest = "..\Installer\arm64\"; Desc = "ARM64 DLL"},
    @{Source = "..\src\Release\Win32\DIME.dll"; Dest = "..\Installer\x86\"; Desc = "x86 DLL"}
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
# Step 3: Build WiX installer (MSIs + Burn Bundle)
# ==============================================================================
Write-Host ""
Write-Host "Building WiX installer..." -ForegroundColor Yellow

# Extract version info from BuildInfo.h
$buildInfoPath = "..\src\BuildInfo.h"
$commitCount = 0
$buildSubVersionStr = ""
$buildVersionStrShort = ""
if (Test-Path $buildInfoPath) {
    $buildInfoContent = Get-Content $buildInfoPath -Raw
    $commitMatch = [regex]::Match($buildInfoContent, "#define BUILD_COMMIT_COUNT\s+(\d+)")
    if ($commitMatch.Success) {
        $commitCount = [int]$commitMatch.Groups[1].Value
    }
    $subVersionStrMatch = [regex]::Match($buildInfoContent, '#define BUILD_SUBVERSION_STR\s+"([^"]+)"')
    if ($subVersionStrMatch.Success) {
        $buildSubVersionStr = $subVersionStrMatch.Groups[1].Value
    }
    $versionStrShortMatch = [regex]::Match($buildInfoContent, '#define BUILD_VERSION_STR_SHORT\s+"([^"]+)"')
    if ($versionStrShortMatch.Success) {
        $buildVersionStrShort = $versionStrShortMatch.Groups[1].Value
    }
}

Write-Host "  Commit count: $commitCount (BUILD_SUBVERSION_STR: $buildSubVersionStr, BUILD_VERSION_STR_SHORT: $buildVersionStrShort)" -ForegroundColor Gray

if ($ProductVersionOverride -ne "") {
    $fullVersion = $ProductVersionOverride
    # Derive short version (first 3 components) and sub-version (4th component)
    $parts = $fullVersion -split '\.'
    $buildVersionStrShort = $parts[0..2] -join '.'
    $buildSubVersionStr   = if ($parts.Count -ge 4) { $parts[3] } else { '0' }
    Write-Host "  ProductVersionOverride active: $fullVersion" -ForegroundColor Yellow
} else {
    $fullVersion = "$buildVersionStrShort.$buildSubVersionStr"  # e.g. "1.2.476.60303"
}
$displayVersion = "v$buildVersionStrShort"                    # e.g. "v1.2.476" (no build counter)

# Generate deterministic ProductCode GUIDs from major.minor.commitcount.
# Same version always gets the same ProductCode so rebuilds of the same version
# reinstall cleanly over each other. Different commit count = different GUID =
# MajorUpgrade fires. Date field excluded intentionally.
# Patch PackageCode (Summary Info property 9) in a built MSI via COM.
# WiX v6 has no attribute for this, so we set it post-build.
function Set-MsiPackageCode([string]$msiPath, [string]$packageCode) {
    $wi = New-Object -ComObject WindowsInstaller.Installer
    $si = $wi.SummaryInformation($msiPath, 1)  # 1 = open for update
    $si.Property(9) = $packageCode             # property 9 = RevisionNumber = PackageCode
    $si.Persist()
    # Explicitly release COM objects so the MSI file is unlocked before Burn build reads it
    [System.Runtime.InteropServices.Marshal]::ReleaseComObject($si)  | Out-Null
    [System.Runtime.InteropServices.Marshal]::ReleaseComObject($wi)  | Out-Null
    [GC]::Collect()
    [GC]::WaitForPendingFinalizers()
}

function Get-DeterministicGuid([string]$seed) {
    $md5   = [System.Security.Cryptography.MD5]::Create()
    $bytes = $md5.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($seed))
    return [System.Guid]::new($bytes).ToString('B').ToUpper()
}
$productCodeGuid    = Get-DeterministicGuid "DIME-x64-$buildVersionStrShort"
$productCodeGuidX86 = Get-DeterministicGuid "DIME-x86-$buildVersionStrShort"
$packageCodeGuid    = Get-DeterministicGuid "DIME-x64-pkg-$buildVersionStrShort"
$packageCodeGuidX86 = Get-DeterministicGuid "DIME-x86-pkg-$buildVersionStrShort"
Write-Host "  ProductCode (x64): $productCodeGuid" -ForegroundColor Gray
Write-Host "  ProductCode (x86): $productCodeGuidX86" -ForegroundColor Gray
Write-Host "  PackageCode (x64): $packageCodeGuid" -ForegroundColor Gray
Write-Host "  PackageCode (x86): $packageCodeGuidX86" -ForegroundColor Gray

# ==============================================================================
# Step 2b: Build NSIS-confirm custom action DLL (DIMEInstallerCA)
# ==============================================================================
Write-Host ""
Write-Host "Building DIMEInstallerCA custom action DLL..." -ForegroundColor Yellow

$vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$msbuildExe = $null
if (Test-Path $vsWhere) {
    $msbuildExe = & $vsWhere -latest -requires Microsoft.Component.MSBuild `
        -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
}
if (-not $msbuildExe -or -not (Test-Path $msbuildExe)) {
    # Fallback: search PATH
    $msbuildCmd = Get-Command msbuild -ErrorAction SilentlyContinue
    $msbuildExe = if ($msbuildCmd) { $msbuildCmd.Source } else { $null }
}
if (-not $msbuildExe -or -not (Test-Path $msbuildExe)) {
    Write-Host "  ERROR: MSBuild.exe not found. Install Visual Studio or Build Tools." -ForegroundColor Red
    if (-not $NonInteractive) { Read-Host "Press Enter to exit" }
    exit 1
}
Write-Host "  Using MSBuild: $msbuildExe" -ForegroundColor Gray

$caProj = "$PSScriptRoot\wix\DIMEInstallerCA\DIMEInstallerCA.vcxproj"

# Build CA DLL (x64 + x86) for standalone MSI installs
foreach ($plat in @("x64", "Win32")) {
    Write-Host "  Building DIMEInstallerCA Release|$plat..." -ForegroundColor Yellow
    & $msbuildExe $caProj /p:Configuration=Release /p:Platform=$plat /nologo /v:minimal
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  ERROR: DIMEInstallerCA build failed for $plat!" -ForegroundColor Red
        if (-not $NonInteractive) { Read-Host "Press Enter to exit" }
        exit 1
    }
}
Write-Host "  DIMEInstallerCA DLLs built successfully!" -ForegroundColor Green

# Copy CA DLLs to staging dir so WiX can find them via $(var.InstallerSourceDir)
$caDir = "$PSScriptRoot\wix\DIMEInstallerCA\bin"
Copy-Item "$caDir\x64\DIMEInstallerCA_x64.dll" "$PSScriptRoot\" -Force
Copy-Item "$caDir\x86\DIMEInstallerCA_x86.dll" "$PSScriptRoot\" -Force
Write-Host "  Copied CA DLLs to staging dir" -ForegroundColor Gray

# Verify wix tool is available
if (-not (Get-Command "wix" -ErrorAction SilentlyContinue)) {
    Write-Host "  wix tool not found. Installing via dotnet tool..." -ForegroundColor Yellow
    dotnet tool install --global wix
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  ERROR: Failed to install wix tool!" -ForegroundColor Red
        if (-not $NonInteractive) { Read-Host "Press Enter to exit" }
        exit 1
    }
}

# Ensure required WiX extensions are installed (pin to v6 to avoid incompatible v7 from NuGet)
foreach ($ext in @("WixToolset.Util.wixext/6.0.2", "WixToolset.UI.wixext/6.0.2", "WixToolset.BootstrapperApplications.wixext/6.0.2")) {
    wix extension add $ext --global 2>$null
}

$wixDir = "$PSScriptRoot\wix"

# WiX uses the Windows Installer COM API to create MSI databases.
# msiserver is demand-start and can stop between calls; ensure it is running.
Start-Service msiserver -ErrorAction SilentlyContinue

# Build x64 MSI (covers AMD64 + ARM64 Windows)
Write-Host "  Building DIME-64bit.msi (AMD64 + ARM64)..." -ForegroundColor Yellow
wix build "$wixDir\DIME-64bit.wxs" `
    -arch x64 `
    -d "ProductVersion=$fullVersion" `
    -d "DisplayVersion=$displayVersion" `
    -d "ProductCodeGuid=$productCodeGuid" `
    -d "PackageCodeGuid=$packageCodeGuid" `
    -d "InstallerSourceDir=$PSScriptRoot" `
    -ext WixToolset.Util.wixext `
    -ext WixToolset.UI.wixext `
    -loc "$wixDir\zh-TW.wxl" `
    -out "$srcDir\DIME-64bit.msi"
if ($LASTEXITCODE -ne 0) {
    Write-Host "  ERROR: Failed to build DIME-64bit.msi!" -ForegroundColor Red
    if (-not $NonInteractive) { Read-Host "Press Enter to exit" }
    exit 1
}
Set-MsiPackageCode "$srcDir\DIME-64bit.msi" $packageCodeGuid
Write-Host "  DIME-64bit.msi PackageCode patched: $packageCodeGuid" -ForegroundColor Gray
Write-Host "  DIME-64bit.msi built successfully!" -ForegroundColor Green

# Build x86 MSI (32-bit Windows only)
Write-Host "  Building DIME-32bit.msi (32-bit Windows only)..." -ForegroundColor Yellow
wix build "$wixDir\DIME-32bit.wxs" `
    -arch x86 `
    -d "ProductVersion=$fullVersion" `
    -d "DisplayVersion=$displayVersion" `
    -d "ProductCodeGuidX86=$productCodeGuidX86" `
    -d "PackageCodeGuidX86=$packageCodeGuidX86" `
    -d "InstallerSourceDir=$PSScriptRoot" `
    -ext WixToolset.Util.wixext `
    -ext WixToolset.UI.wixext `
    -loc "$wixDir\zh-TW.wxl" `
    -out "$srcDir\DIME-32bit.msi"
if ($LASTEXITCODE -ne 0) {
    Write-Host "  ERROR: Failed to build DIME-32bit.msi!" -ForegroundColor Red
    if (-not $NonInteractive) { Read-Host "Press Enter to exit" }
    exit 1
}
Set-MsiPackageCode "$srcDir\DIME-32bit.msi" $packageCodeGuidX86
Write-Host "  DIME-32bit.msi PackageCode patched: $packageCodeGuidX86" -ForegroundColor Gray
Write-Host "  DIME-32bit.msi built successfully!" -ForegroundColor Green

# Build Burn Bundle (DIME-Universal.exe)
Write-Host "  Building DIME-Universal.exe (Burn Bundle)..." -ForegroundColor Yellow
wix build "$wixDir\Bundle.wxs" `
    -d "ProductVersion=$fullVersion" `
    -d "DisplayVersion=$displayVersion" `
    -d "InstallerSourceDir=$srcDir" `
    -ext WixToolset.BootstrapperApplications.wixext `
    -loc "$wixDir\zh-TW.wxl" `
    -out "$srcDir\DIME-Universal.exe"
if ($LASTEXITCODE -ne 0) {
    Write-Host "  ERROR: Failed to build DIME-Universal.exe!" -ForegroundColor Red
    if (-not $NonInteractive) { Read-Host "Press Enter to exit" }
    exit 1
}
Write-Host "  DIME-Universal.exe built successfully!" -ForegroundColor Green

Write-Host ""
Write-Host "===============================================================================" -ForegroundColor Cyan
Write-Host "Deployment and Installer packaging done!!" -ForegroundColor Cyan
Write-Host "===============================================================================" -ForegroundColor Cyan

# ==============================================================================
# Step 4: Create ZIP archive
# ==============================================================================
if ($SkipChecksumUpdate) {
    Write-Host ""
    Write-Host "Skipping ZIP creation and checksum update (-SkipChecksumUpdate)." -ForegroundColor Yellow
} else {

Write-Host ""
Write-Host "===============================================================================" -ForegroundColor Cyan
Write-Host "Creating ZIP archive..." -ForegroundColor Cyan
Write-Host "===============================================================================" -ForegroundColor Cyan
Write-Host ""

$installerFile = "$srcDir\DIME-Universal.exe"
$zipFile       = "$srcDir\DIME-Universal.zip"

if (-not (Test-Path $installerFile)) {
    Write-Host "ERROR: $installerFile not found!" -ForegroundColor Red
    if (-not $NonInteractive) { Read-Host "Press Enter to exit" }
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

# Calculate checksums for all files
$exeHash    = (Get-FileHash -Algorithm SHA256 -Path "$srcDir\DIME-Universal.exe").Hash
$zipHash    = (Get-FileHash -Algorithm SHA256 -Path "$srcDir\DIME-Universal.zip").Hash
$msi64Hash  = (Get-FileHash -Algorithm SHA256 -Path "$srcDir\DIME-64bit.msi").Hash
$msi32Hash  = (Get-FileHash -Algorithm SHA256 -Path "$srcDir\DIME-32bit.msi").Hash
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
Write-Host "MSI File: DIME-64bit.msi" -ForegroundColor White
Write-Host "  SHA-256: $msi64Hash" -ForegroundColor Green
Write-Host ""
Write-Host "MSI File: DIME-32bit.msi" -ForegroundColor White
Write-Host "  SHA-256: $msi32Hash" -ForegroundColor Green
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

    # Build the new checksum section with direct Chinese text
    # Note: This script file should be saved as UTF-8 with BOM for proper encoding
    $checksumSection = @"

   **最新開發版本 DIME v$buildVersionStrShort SHA-256 CHECKSUM (更新日期: $date):**

   | 檔案 | SHA-256 CHECKSUM |
   |------|----------------|
   | DIME-Universal.exe | ``$exeHash`` |
   | DIME-Universal.zip | ``$zipHash`` |
   | DIME-64bit.msi | ``$msi64Hash`` |
   | DIME-32bit.msi | ``$msi32Hash`` |

"@

    # Replace content between DOWNLOAD_START and DOWNLOAD_END markers
    $downloadSection = @"

   **最新開發版本 DIME v$buildVersionStrShort (更新日期: $date)**

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

} # end if (-not $SkipChecksumUpdate)

# ==============================================================================
# Summary
# ==============================================================================
Write-Host ""
Write-Host "===============================================================================" -ForegroundColor Cyan
Write-Host "Deployment Complete!" -ForegroundColor Cyan
Write-Host "===============================================================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "CREATED FILES:" -ForegroundColor Yellow
Write-Host "  - DIME-Universal.exe (Burn Bundle Installer)" -ForegroundColor White
Write-Host "  - DIME-Universal.zip (ZIP Archive)" -ForegroundColor White
Write-Host "  - DIME-64bit.msi (AMD64/ARM64 MSI, embedded in bundle)" -ForegroundColor White
  Write-Host "  - DIME-32bit.msi (32-bit MSI, embedded in bundle)" -ForegroundColor White
Write-Host ""
Write-Host "NEXT STEPS:" -ForegroundColor Yellow
Write-Host "  1. Review the updated README.md with checksums" -ForegroundColor White
Write-Host "  2. Commit both installer files and README.md" -ForegroundColor White
Write-Host "  3. Create GitHub release with both .exe and .zip" -ForegroundColor White
Write-Host "  4. The checksums are now embedded in README.md" -ForegroundColor White
Write-Host ""
Write-Host "===============================================================================" -ForegroundColor Cyan
Write-Host ""

if (-not $NonInteractive) { Read-Host "Press Enter to exit" }
