# Build-MSIX.ps1
# PowerShell script to build DIME MSIX/MSIX Bundle using Windows SDK tools
# Does NOT require Windows Application Packaging Project workload
# Run from the DIME.Packaging folder or repository root

param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    
    [ValidateSet("x86", "x64", "ARM64", "Bundle")]
    [string]$Platform = "Bundle",
    
    [string]$CertificatePath = "",
    [string]$CertificatePassword = "",
    
    [switch]$Clean,
    [switch]$SkipBuild,
    [switch]$Help
)

# Show help
if ($Help) {
    Write-Host @"
DIME MSIX Build Script (Windows SDK)
=====================================

Usage: .\Build-MSIX.ps1 [options]

Options:
    -Configuration <Debug|Release>  Build configuration (default: Release)
    -Platform <x86|x64|ARM64|Bundle> Target platform or Bundle for all (default: Bundle)
    -CertificatePath <path>         Path to .pfx certificate for signing
    -CertificatePassword <password> Password for the certificate
    -Clean                          Clean build outputs before building
    -SkipBuild                      Skip native build, only create packages
    -Help                           Show this help message

Examples:
    .\Build-MSIX.ps1                           # Build Release bundle (x86, x64, ARM64)
    .\Build-MSIX.ps1 -Platform x64             # Build Release x64 only
    .\Build-MSIX.ps1 -Configuration Debug      # Build Debug bundle
    .\Build-MSIX.ps1 -Clean                    # Clean and rebuild
    .\Build-MSIX.ps1 -CertificatePath "cert.pfx" -CertificatePassword "pass"

"@
    exit 0
}

$ErrorActionPreference = "Stop"

# Determine script and solution paths
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir

# If running from repo root, adjust paths
if (Test-Path "$ScriptDir\DIME.Packaging") {
    $RepoRoot = $ScriptDir
    $ScriptDir = "$ScriptDir\DIME.Packaging"
}

$SolutionPath = "$RepoRoot\DIME.sln"
$ManifestPath = "$ScriptDir\Package.appxmanifest"
$OutputDir = "$ScriptDir\AppPackages"
$StagingRoot = "$ScriptDir\Staging"

# Use default test certificate if not specified
if (-not $CertificatePath) {
    $defaultCert = "$ScriptDir\DIME.Packaging_TemporaryKey.pfx"
    if (Test-Path $defaultCert) {
        $CertificatePath = $defaultCert
    }
}

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "DIME MSIX Build Script (Windows SDK)" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Repository Root: $RepoRoot" -ForegroundColor Gray
Write-Host "Configuration: $Configuration" -ForegroundColor Yellow
Write-Host "Platform: $Platform" -ForegroundColor Yellow
Write-Host ""

# Verify paths exist
if (-not (Test-Path $SolutionPath)) {
    Write-Host "Error: Solution file not found at $SolutionPath" -ForegroundColor Red
    exit 1
}



if (-not (Test-Path $ManifestPath)) {
    Write-Host "Error: Package.appxmanifest not found at $ManifestPath" -ForegroundColor Red
    exit 1
}

# Find MSBuild
function Find-MSBuild {
    $paths = @(
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe",
        "${env:ProgramFiles}\Microsoft Visual Studio\2019\Enterprise\MSBuild\Current\Bin\MSBuild.exe",
        "${env:ProgramFiles}\Microsoft Visual Studio\2019\Professional\MSBuild\Current\Bin\MSBuild.exe",
        "${env:ProgramFiles}\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe"
    )
    
    foreach ($path in $paths) {
        if (Test-Path $path) { return $path }
    }
    
    # Try vswhere
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -requires Microsoft.Component.MSBuild -property installationPath
        if ($vsPath) {
            $msbuild = Join-Path $vsPath "MSBuild\Current\Bin\MSBuild.exe"
            if (Test-Path $msbuild) { return $msbuild }
        }
    }
    return $null
}

# Find Windows SDK tools
function Find-WindowsSDKTool {
    param([string]$ToolName)
    
    $sdkPaths = @(
        "${env:ProgramFiles(x86)}\Windows Kits\10\bin\10.0.26100.0\x64",
        "${env:ProgramFiles(x86)}\Windows Kits\10\bin\10.0.22621.0\x64",
        "${env:ProgramFiles(x86)}\Windows Kits\10\bin\10.0.22000.0\x64",
        "${env:ProgramFiles(x86)}\Windows Kits\10\bin\10.0.19041.0\x64",
        "${env:ProgramFiles(x86)}\Windows Kits\10\bin\10.0.18362.0\x64"
    )
    
    # Try to find latest SDK version
    $sdkRoot = "${env:ProgramFiles(x86)}\Windows Kits\10\bin"
    if (Test-Path $sdkRoot) {
        $versions = Get-ChildItem -Path $sdkRoot -Directory | Where-Object { $_.Name -match "^10\." } | Sort-Object Name -Descending
        foreach ($ver in $versions) {
            $toolPath = Join-Path $ver.FullName "x64\$ToolName"
            if (Test-Path $toolPath) { return $toolPath }
        }
    }
    
    foreach ($path in $sdkPaths) {
        $toolPath = Join-Path $path $ToolName
        if (Test-Path $toolPath) { return $toolPath }
    }
    return $null
}

$MSBuild = Find-MSBuild
$MakeAppx = Find-WindowsSDKTool "makeappx.exe"
$SignTool = Find-WindowsSDKTool "signtool.exe"
$MakePri = Find-WindowsSDKTool "makepri.exe"

if (-not $MSBuild) {
    Write-Host "Error: MSBuild not found. Please install Visual Studio." -ForegroundColor Red
    exit 1
}

if (-not $MakeAppx) {
    Write-Host "Error: makeappx.exe not found. Please install Windows 10 SDK." -ForegroundColor Red
    exit 1
}

Write-Host "MSBuild:   $MSBuild" -ForegroundColor Gray
Write-Host "MakeAppx:  $MakeAppx" -ForegroundColor Gray
Write-Host "SignTool:  $SignTool" -ForegroundColor Gray
Write-Host "MakePri:   $MakePri" -ForegroundColor Gray
Write-Host ""

# Read version from manifest
[xml]$manifest = Get-Content $ManifestPath
$PackageName = $manifest.Package.Identity.Name
$PackageVersion = $manifest.Package.Identity.Version
$Publisher = $manifest.Package.Identity.Publisher

Write-Host "Package: $PackageName" -ForegroundColor Cyan
Write-Host "Version: $PackageVersion" -ForegroundColor Cyan
Write-Host "Publisher: $Publisher" -ForegroundColor Cyan
Write-Host ""

# Clean if requested
if ($Clean) {
    Write-Host "[Clean] Cleaning build outputs..." -ForegroundColor Yellow
    
    $foldersToClean = @(
        "$RepoRoot\Debug",
        "$RepoRoot\Release",
        "$RepoRoot\x64",
        "$RepoRoot\ARM64",
        "$RepoRoot\ARM64EC",
        $StagingRoot,
        $OutputDir
    )
    
    foreach ($folder in $foldersToClean) {
        if (Test-Path $folder) {
            Remove-Item -Path $folder -Recurse -Force -ErrorAction SilentlyContinue
            Write-Host "  Cleaned: $folder" -ForegroundColor Gray
        }
    }
    Write-Host ""
}

# Build native projects
function Build-NativeProject {
    param(
        [string]$Project,
        [string]$Platform,
        [string]$Configuration
    )
    
    Write-Host "  Building $Project ($Platform|$Configuration)..." -ForegroundColor Gray
    
    $buildArgs = @(
        "$RepoRoot\$Project",
        "/p:Configuration=$Configuration",
        "/p:Platform=$Platform",
        "/p:PlatformToolset=v143",
        "/t:Build",
        "/m",
        "/v:minimal",
        "/nologo"
    )
    
    & $MSBuild $buildArgs 2>&1 | Out-Null
    
    if ($LASTEXITCODE -ne 0) {
        # Retry with more verbose output
        & $MSBuild $buildArgs
        if ($LASTEXITCODE -ne 0) {
            throw "Build failed for $Project ($Platform)"
        }
    }
}

# Get output paths based on platform
function Get-BuildOutputPath {
    param([string]$Platform, [string]$Configuration)
    
    switch ($Platform) {
        "x86"   { return "$RepoRoot\$Configuration\Win32" }
        "x64"   { return "$RepoRoot\$Configuration\x64" }
        "ARM64" { return "$RepoRoot\$Configuration\ARM64EC" }
    }
}

# Stage files for MSIX
function Stage-Platform {
    param([string]$Platform)
    
    Write-Host "[Stage] Staging files for $Platform..." -ForegroundColor Yellow
    
    $stagingDir = "$StagingRoot\$Platform"
    if (Test-Path $stagingDir) {
        Remove-Item -Path $stagingDir -Recurse -Force
    }
    New-Item -ItemType Directory -Path $stagingDir -Force | Out-Null
    New-Item -ItemType Directory -Path "$stagingDir\DIME" -Force | Out-Null
    
    # Determine build output paths
    $mainBuildPath = Get-BuildOutputPath -Platform $Platform -Configuration $Configuration
    $settingsBuildPath = "$RepoRoot\DIMESettings\$Configuration"  # DIMESettings outputs to DIMESettings\Release folder
    
    # Copy DIMESettings.exe (always Win32, runs under emulation on ARM64)
    $settingsExe = "$settingsBuildPath\DIMESettings.exe"
    if (Test-Path $settingsExe) {
        Copy-Item $settingsExe "$stagingDir\" -Force
        Write-Host "  Copied: DIMESettings.exe" -ForegroundColor Gray
    } else {
        throw "DIMESettings.exe not found at $settingsExe"
    }
    
    # Copy DIME.dll to DIME subfolder
    $dimeDll = "$mainBuildPath\DIME.dll"
    if (Test-Path $dimeDll) {
        Copy-Item $dimeDll "$stagingDir\DIME\" -Force
        Write-Host "  Copied: DIME\DIME.dll ($Platform)" -ForegroundColor Gray
    } else {
        throw "DIME.dll not found at $dimeDll"
    }
    
    # For x64 and ARM64, also include Win32 DIME.dll for 32-bit app support
    if ($Platform -eq "x64" -or $Platform -eq "ARM64") {
        $win32DllPath = "$RepoRoot\$Configuration\Win32\DIME.dll"
        if (Test-Path $win32DllPath) {
            New-Item -ItemType Directory -Path "$stagingDir\DIME.x86" -Force | Out-Null
            Copy-Item $win32DllPath "$stagingDir\DIME.x86\" -Force
            Write-Host "  Copied: DIME.x86\DIME.dll (Win32)" -ForegroundColor Gray
        } else {
            Write-Host "  Warning: Win32 DIME.dll not found, 32-bit apps won't work" -ForegroundColor Yellow
        }
    }
    
    # Copy dictionary files (.cin)
    $cinFiles = @(
        "Array.cin", "Array40.cin", "Array-Ext-B.cin", "Array-Ext-CD.cin",
        "Array-Ext-EF.cin", "Array-Phrase.cin", "Array-shortcode.cin",
        "Array-special.cin", "phone.cin", "TCFreq.cin", "TCSC.cin"
    )
    
    foreach ($cin in $cinFiles) {
        $cinPath = "$RepoRoot\installer\$cin"
        if (Test-Path $cinPath) {
            Copy-Item $cinPath "$stagingDir\" -Force
        }
    }
    Write-Host "  Copied: Dictionary files (.cin)" -ForegroundColor Gray
    
    # Copy images
    $imagesDir = "$ScriptDir\Images"
    if (Test-Path $imagesDir) {
        New-Item -ItemType Directory -Path "$stagingDir\Images" -Force | Out-Null
        Copy-Item "$imagesDir\*" "$stagingDir\Images\" -Force -Recurse
        Write-Host "  Copied: Images" -ForegroundColor Gray
    }
    
    # Copy cleanup script (deployed to LocalAppData during registration for post-uninstall cleanup)
    $cleanupScript = "$ScriptDir\DIME_Cleanup.ps1"
    if (Test-Path $cleanupScript) {
        Copy-Item $cleanupScript "$stagingDir\" -Force
        Write-Host "  Copied: DIME_Cleanup.ps1 (post-uninstall cleanup)" -ForegroundColor Gray
    } else {
        Write-Host "  Warning: DIME_Cleanup.ps1 not found" -ForegroundColor Yellow
    }
    
    # Create platform-specific manifest
    $platformManifest = "$stagingDir\AppxManifest.xml"
    [xml]$manifestXml = Get-Content $ManifestPath
    
    # Set processor architecture
    $arch = switch ($Platform) {
        "x86"   { "x86" }
        "x64"   { "x64" }
        "ARM64" { "arm64" }
    }
    $manifestXml.Package.Identity.SetAttribute("ProcessorArchitecture", $arch)
    
    # NOTE: Automatic uninstall cleanup (desktop:customInstall, desktop6:uninstallAction) 
    # is only available for Microsoft Store packages, not sideloaded MSIX.
    # Users must run DIME_Cleanup.ps1 as admin after uninstalling to clean up registry entries.
    
    $manifestXml.Save($platformManifest)
    Write-Host "  Created: AppxManifest.xml (arch=$arch)" -ForegroundColor Gray
    
    # Create priconfig.xml and resources.pri
    if ($MakePri) {
        $priconfigPath = "$stagingDir\priconfig.xml"
        $priConfigContent = @"
<?xml version="1.0" encoding="UTF-8"?>
<resources targetOsVersion="10.0.0" majorVersion="1">
  <packaging>
    <autoResourcePackage qualifier="Language"/>
    <autoResourcePackage qualifier="Scale"/>
  </packaging>
  <index root="$stagingDir" startIndexAt="$stagingDir">
    <default>
      <qualifier name="Language" value="zh-Hant"/>
      <qualifier name="Contrast" value="standard"/>
      <qualifier name="Scale" value="200"/>
      <qualifier name="HomeRegion" value="001"/>
      <qualifier name="TargetSize" value="256"/>
      <qualifier name="LayoutDirection" value="LTR"/>
      <qualifier name="DXFeatureLevel" value="DX9"/>
      <qualifier name="Configuration" value=""/>
      <qualifier name="AlternateForm" value=""/>
    </default>
    <indexer-config type="folder" foldernameAsQualifier="true" filenameAsQualifier="true" qualifierDelimiter="."/>
    <indexer-config type="PRI"/>
  </index>
</resources>
"@
        $priConfigContent | Out-File -FilePath $priconfigPath -Encoding UTF8
        
        Push-Location $stagingDir
        & $MakePri new /pr "$stagingDir" /cf "$priconfigPath" /of "$stagingDir\resources.pri" /mf AppX /o 2>&1 | Out-Null
        Pop-Location
        
        if (Test-Path "$stagingDir\resources.pri") {
            Write-Host "  Created: resources.pri" -ForegroundColor Gray
        }
        
        # Clean up priconfig
        Remove-Item $priconfigPath -Force -ErrorAction SilentlyContinue
    }
    
    return $stagingDir
}

# Create MSIX package
function Create-MSIX {
    param(
        [string]$StagingDir,
        [string]$Platform,
        [string]$OutputPath
    )
    
    Write-Host "[Package] Creating MSIX for $Platform..." -ForegroundColor Yellow
    
    $msixName = "DIME_${PackageVersion}_${Platform}.msix"
    $msixPath = Join-Path $OutputPath $msixName
    
    # Create output directory
    if (-not (Test-Path $OutputPath)) {
        New-Item -ItemType Directory -Path $OutputPath -Force | Out-Null
    }
    
    # Create MSIX
    & $MakeAppx pack /d "$StagingDir" /p "$msixPath" /o 2>&1 | Out-Null
    
    if ($LASTEXITCODE -ne 0) {
        & $MakeAppx pack /d "$StagingDir" /p "$msixPath" /o
        throw "Failed to create MSIX for $Platform"
    }
    
    Write-Host "  Created: $msixName" -ForegroundColor Green
    return $msixPath
}

# Create MSIX Bundle
function Create-Bundle {
    param(
        [string[]]$MsixFiles,
        [string]$OutputPath
    )
    
    Write-Host "[Bundle] Creating MSIX Bundle..." -ForegroundColor Yellow
    
    $bundleName = "DIME_${PackageVersion}.msixbundle"
    $bundlePath = Join-Path $OutputPath $bundleName
    $bundleDir = "$StagingRoot\Bundle"
    
    # Create bundle staging directory
    if (Test-Path $bundleDir) {
        Remove-Item -Path $bundleDir -Recurse -Force
    }
    New-Item -ItemType Directory -Path $bundleDir -Force | Out-Null
    
    # Copy MSIX files to bundle directory
    foreach ($msix in $MsixFiles) {
        Copy-Item $msix $bundleDir -Force
    }
    
    # Create bundle
    & $MakeAppx bundle /d "$bundleDir" /p "$bundlePath" /o 2>&1 | Out-Null
    
    if ($LASTEXITCODE -ne 0) {
        & $MakeAppx bundle /d "$bundleDir" /p "$bundlePath" /o
        throw "Failed to create MSIX bundle"
    }
    
    Write-Host "  Created: $bundleName" -ForegroundColor Green
    return $bundlePath
}

# Sign package
function Sign-Package {
    param([string]$PackagePath)
    
    if (-not $SignTool) {
        Write-Host "  SignTool not found, skipping signing" -ForegroundColor Yellow
        return
    }
    
    if (-not $CertificatePath -or -not (Test-Path $CertificatePath)) {
        Write-Host "  No certificate provided, skipping signing" -ForegroundColor Yellow
        return
    }
    
    Write-Host "[Sign] Signing $([System.IO.Path]::GetFileName($PackagePath))..." -ForegroundColor Yellow
    
    $signArgs = @("sign", "/fd", "SHA256", "/f", $CertificatePath)
    if ($CertificatePassword) {
        $signArgs += @("/p", $CertificatePassword)
    }
    $signArgs += $PackagePath
    
    & $SignTool $signArgs 2>&1 | Out-Null
    
    if ($LASTEXITCODE -eq 0) {
        Write-Host "  Signed successfully" -ForegroundColor Green
    } else {
        Write-Host "  Warning: Signing failed" -ForegroundColor Yellow
    }
}

# Main build process
try {
    $platforms = @()
    if ($Platform -eq "Bundle") {
        $platforms = @("x86", "x64", "ARM64")
    } else {
        $platforms = @($Platform)
    }
    
    # Build native projects if not skipped
    if (-not $SkipBuild) {
        Write-Host "[Build] Building native projects..." -ForegroundColor Yellow
        
        # Always build Win32 for DIMESettings and x86 DIME.dll
        Build-NativeProject -Project "DIMESettings\DIMESettings.vcxproj" -Platform "Win32" -Configuration $Configuration
        Build-NativeProject -Project "DIME.vcxproj" -Platform "Win32" -Configuration $Configuration
        
        # Build platform-specific DIME.dll
        foreach ($plat in $platforms) {
            switch ($plat) {
                "x86" {
                    # Already built above
                }
                "x64" {
                    Build-NativeProject -Project "DIME.vcxproj" -Platform "x64" -Configuration $Configuration
                }
                "ARM64" {
                    Build-NativeProject -Project "DIME.vcxproj" -Platform "ARM64EC" -Configuration $Configuration
                }
            }
        }
        
        Write-Host "  Native build completed" -ForegroundColor Green
        Write-Host ""
    }
    
    # Stage and create packages
    $msixFiles = @()
    foreach ($plat in $platforms) {
        $stagingDir = Stage-Platform -Platform $plat
        $msixPath = Create-MSIX -StagingDir $stagingDir -Platform $plat -OutputPath $OutputDir
        $msixFiles += $msixPath
        
        # Sign individual MSIX
        Sign-Package -PackagePath $msixPath
    }
    
    # Create bundle if requested
    if ($Platform -eq "Bundle" -and $msixFiles.Count -gt 0) {
        $bundlePath = Create-Bundle -MsixFiles $msixFiles -OutputPath $OutputDir
        Sign-Package -PackagePath $bundlePath
    }
    
    # Display results
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "Build Complete!" -ForegroundColor Green
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "Output directory: $OutputDir" -ForegroundColor Gray
    Write-Host ""
    
    # List output files
    if (Test-Path $OutputDir) {
        $bundles = Get-ChildItem -Path $OutputDir -Filter "*.msixbundle" -ErrorAction SilentlyContinue
        foreach ($b in $bundles) {
            $size = [math]::Round($b.Length / 1MB, 2)
            Write-Host "  [Bundle] $($b.Name) ($size MB)" -ForegroundColor Green
        }
        
        $packages = Get-ChildItem -Path $OutputDir -Filter "*.msix" -ErrorAction SilentlyContinue
        foreach ($p in $packages) {
            $size = [math]::Round($p.Length / 1MB, 2)
            Write-Host "  [MSIX]   $($p.Name) ($size MB)" -ForegroundColor Cyan
        }
    }
    
    Write-Host ""
    Write-Host "To install (unsigned package):" -ForegroundColor Yellow
    Write-Host "  1. Enable Developer Mode in Windows Settings" -ForegroundColor Gray
    Write-Host "  2. Run: Add-AppxPackage -Path <package.msix>" -ForegroundColor Gray
    Write-Host ""
    Write-Host "To install (signed package):" -ForegroundColor Yellow
    Write-Host "  1. Install the signing certificate to 'Trusted People' store" -ForegroundColor Gray
    Write-Host "  2. Double-click the .msixbundle/.msix file" -ForegroundColor Gray
    Write-Host ""
    
} catch {
    Write-Host ""
    Write-Host "Error: $_" -ForegroundColor Red
    Write-Host $_.ScriptStackTrace -ForegroundColor Red
    exit 1
}
