# Comprehensive DIME Coverage Analysis
# Runs all test suites and generates coverage report

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "DIME Comprehensive Coverage Analysis" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Check OpenCppCoverage
$opencpp = Get-Command "OpenCppCoverage.exe" -ErrorAction SilentlyContinue
if (-not $opencpp) {
    Write-Host "ERROR: OpenCppCoverage not installed!" -ForegroundColor Red
    Write-Host "Install with: choco install opencppcoverage" -ForegroundColor Yellow
    exit 1
}

# Find vstest.console.exe
$vstest = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\TestWindow\vstest.console.exe"
if (-not (Test-Path $vstest)) {
    Write-Host "ERROR: vstest.console.exe not found at expected location" -ForegroundColor Red
    exit 1
}

Write-Host "✓ OpenCppCoverage found" -ForegroundColor Green
Write-Host "✓ vstest.console.exe found" -ForegroundColor Green
Write-Host ""

# Navigate to root DIME directory
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$rootDir = Split-Path -Parent (Split-Path -Parent $scriptDir)
Set-Location $rootDir

# Build paths
$dllPath = "src\tests\bin\x64\Debug\DIMETests.dll"
$dimeDllPath = "src\Debug\x64\DIME.dll"

if (-not (Test-Path $dllPath)) {
    Write-Host "ERROR: Test DLL not found at: $dllPath" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $dimeDllPath)) {
    Write-Host "ERROR: DIME.dll not found at: $dimeDllPath" -ForegroundColor Red
    exit 1
}

Write-Host "✓ Test DLL found: $dllPath" -ForegroundColor Green
Write-Host "✓ DIME.dll found: $dimeDllPath" -ForegroundColor Green
Write-Host ""

# Create output directory
$outputDir = "src\tests\coverage_results"
if (-not (Test-Path $outputDir)) {
    New-Item -ItemType Directory -Path $outputDir | Out-Null
}

Write-Host "Running comprehensive coverage analysis..." -ForegroundColor Cyan
Write-Host ""

# Run coverage for ALL tests
$outputFile = "$outputDir\AllTests_Coverage.xml"

$args = @(
    "--modules", "*DIME.dll",
    "--sources", "C:\Users\Jeremy\Documents\GitHub\DIME\src\*",
    "--excluded_sources", "*\tests\*",
    "--export_type=cobertura:$outputFile",
    "--", $vstest, $dllPath
)

Write-Host "Command: OpenCppCoverage.exe $($args -join ' ')" -ForegroundColor Gray
Write-Host ""

& OpenCppCoverage.exe $args

if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Green
    Write-Host "Coverage analysis completed!" -ForegroundColor Green
    Write-Host "========================================" -ForegroundColor Green
    Write-Host ""
    Write-Host "Output file: $outputFile" -ForegroundColor Cyan
    Write-Host ""
    
    # Parse coverage summary
    if (Test-Path $outputFile) {
        [xml]$coverage = Get-Content $outputFile
        
        if ($coverage.coverage) {
            $lineRate = [math]::Round([decimal]$coverage.coverage.'line-rate' * 100, 2)
            $branchRate = [math]::Round([decimal]$coverage.coverage.'branch-rate' * 100, 2)
            
            $totalLines = [int]$coverage.coverage.'lines-valid'
            $coveredLines = [int]$coverage.coverage.'lines-covered'
            
            Write-Host "========================================" -ForegroundColor Cyan
            Write-Host "COVERAGE SUMMARY" -ForegroundColor Cyan
            Write-Host "========================================" -ForegroundColor Cyan
            Write-Host "Line Coverage:   $lineRate% ($coveredLines / $totalLines lines)" -ForegroundColor $(if ($lineRate -ge 90) { "Green" } elseif ($lineRate -ge 80) { "Yellow" } else { "Red" })
            Write-Host "Branch Coverage: $branchRate%" -ForegroundColor $(if ($branchRate -ge 85) { "Green" } elseif ($branchRate -ge 75) { "Yellow" } else { "Red" })
            Write-Host ""
        }
    }
} else {
    Write-Host ""
    Write-Host "ERROR: Coverage analysis failed with exit code $LASTEXITCODE" -ForegroundColor Red
    exit $LASTEXITCODE
}
