param(
    [string] $OutputDir = ""
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$sourceDir = Join-Path $repoRoot "src"
$deployScript = Join-Path $PSScriptRoot "deploy-wix-installer.ps1"

if ($OutputDir -eq "") {
    $OutputDir = Join-Path $PSScriptRoot "logging-output"
}
New-Item -ItemType Directory -Force $OutputDir | Out-Null
$OutputDir = (Resolve-Path $OutputDir).Path

$msbuildCommand = Get-Command msbuild.exe -ErrorAction SilentlyContinue
$msbuildPath = if ($msbuildCommand) { $msbuildCommand.Source } else { "" }
if ($msbuildPath -eq "") {
    $vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vsWhere) {
        $msbuildPath = & $vsWhere -latest -products * -requires Microsoft.Component.MSBuild Microsoft.VisualStudio.Component.VC.Runtimes.x86.x64.Spectre Microsoft.VisualStudio.Component.VC.Runtimes.ARM64.Spectre Microsoft.VisualStudio.Component.VC.Runtimes.ARM64EC.Spectre -find "MSBuild\**\Bin\MSBuild.exe" |
            Select-Object -First 1
    }
}
if (-not $msbuildPath -or -not (Test-Path $msbuildPath)) {
    throw "MSBuild.exe was not found. Install Visual Studio or Build Tools with the MSBuild component."
}

$builds = @(
    @{ Platform = "Win32"; Target = "DIME"; Diagnostic = $true },
    @{ Platform = "x64"; Target = "DIME"; Diagnostic = $true },
    @{ Platform = "ARM64"; Target = "DIME"; Diagnostic = $true },
    @{ Platform = "Win32"; Target = "DIMESettings"; Diagnostic = $false }
)

Push-Location $sourceDir
try {
    foreach ($build in $builds) {
        $arguments = @(
            "DIME.sln",
            "/t:$($build.Target)",
            "/p:Configuration=Release",
            "/p:RunCodeAnalysis=false",
            "/p:Platform=$($build.Platform)"
        )
        if ($build.Diagnostic) {
            $arguments += "/p:DiagnosticLogging=true"
        }

        Write-Host "Building Release|$($build.Platform) $($build.Target) (logging=$($build.Diagnostic))..." -ForegroundColor Yellow
        & $msbuildPath @arguments
        if ($LASTEXITCODE -ne 0) {
            throw "Build failed for $($build.Target) Release|$($build.Platform) with exit code $LASTEXITCODE."
        }
    }
}
finally {
    Pop-Location
}

Push-Location $PSScriptRoot
try {
    & $deployScript -OutputDir $OutputDir -NonInteractive -SkipChecksumUpdate
    if ($LASTEXITCODE -ne 0) {
        throw "WiX packaging failed with exit code $LASTEXITCODE."
    }
}
finally {
    Pop-Location
}

$standardExe = Join-Path $OutputDir "DIME-Universal.exe"
$loggingExe = Join-Path $OutputDir "DIME-Universal-logging.exe"
$loggingZip = Join-Path $OutputDir "DIME-Universal-logging.zip"
if (-not (Test-Path $standardExe)) {
    throw "Expected Burn bundle was not produced: $standardExe"
}

Move-Item -Path $standardExe -Destination $loggingExe -Force
Compress-Archive -Path $loggingExe -DestinationPath $loggingZip -Force

Write-Host "" 
Write-Host "Diagnostic logging installer built successfully:" -ForegroundColor Green
Write-Host "  $loggingExe"
Write-Host "  $loggingZip"
