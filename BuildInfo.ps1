# BuildInfo.ps1
# Generates BuildInfo.h and optionally updates MSIX Package.appxmanifest
# Called as pre-build event by DIME.vcxproj
#
# Usage:
#   BuildInfo.ps1                              - Generate BuildInfo.h only
#   BuildInfo.ps1 -UpdateManifest              - Generate BuildInfo.h and update manifest
#   BuildInfo.ps1 -UpdateManifest -ManifestOnly - Update manifest only (skip BuildInfo.h)

param(
    [switch]$UpdateManifest,
    [switch]$ManifestOnly,
    [string]$ManifestPath = "DIME.Packaging\Package.appxmanifest"
)

# Get the script's directory (should be repo root)
$repoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildInfoFile = Join-Path $repoRoot "BuildInfo.h"

# Get git commit count
try {
    $commitCount = (git -C $repoRoot rev-list HEAD --count 2>$null)
    if (-not $commitCount) { $commitCount = "0" }
} catch {
    $commitCount = "0"
}

# Version components
$major = 1
$minor = 2

# Date components
$now = Get-Date
$year4 = $now.ToString("yyyy")
$year2 = $now.ToString("yy")
$month = $now.ToString("MM")
$day = $now.ToString("dd")
$date1 = $now.ToString("yyMMdd")
$date4 = $now.ToString("yyyyMMdd")

# Build version string
$buildVersion = "$major.$minor.$commitCount.$date1"

# Generate BuildInfo.h unless ManifestOnly is specified
if (-not $ManifestOnly) {
    $buildInfoContent = @"
#define BUILD_VER_MAJOR $major
#define BUILD_VER_MINOR $minor
#define BUILD_COMMIT_COUNT $commitCount
#define BUILD_YEAR_4 $year4
#define BUILD_YEAR_2 $year2
#define BUILD_YEAR_1 $year2
#define BUILD_MONTH $month
#define BUILD_DAY $day
#define BUILD_DATE_1 $date1
#define BUILD_DATE_4 $date4
#define BUILD_VERSION $buildVersion
#define BUILD_VERSION_STR "$buildVersion"

"@

    # Write BuildInfo.h (always overwrite to ensure fresh build info)
    $buildInfoContent | Set-Content $buildInfoFile -Encoding ASCII
    Write-Host "Generated BuildInfo.h: $buildVersion"
}

# Update MSIX manifest if requested
if ($UpdateManifest) {
    $manifestFile = Join-Path $repoRoot $ManifestPath
    
    if (Test-Path $manifestFile) {
        # MSIX version format: Major.Minor.Build.Revision (all parts 0-65535)
        # Use commit count as Build, 0 as Revision (date exceeds 65535)
        $msixVersion = "$major.$minor.$commitCount.0"
        
        $manifest = Get-Content $manifestFile -Raw
        
        # Match and replace Version attribute in Identity element
        $pattern = '(<Identity[^>]*Version=")[^"]*(")'
        $replacement = "`${1}$msixVersion`${2}"
        
        $updatedManifest = $manifest -replace $pattern, $replacement
        
        if ($manifest -ne $updatedManifest) {
            $updatedManifest | Set-Content $manifestFile -NoNewline -Encoding UTF8
            Write-Host "Updated Package.appxmanifest: $msixVersion"
        } else {
            Write-Host "Package.appxmanifest already at: $msixVersion"
        }
    } else {
        Write-Host "Manifest not found (skipping): $manifestFile"
    }
}

exit 0
