$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$projectPath = Join-Path $repoRoot "src\DIME.vcxproj"
$globalsPath = Join-Path $repoRoot "src\Globals.cpp"
$builderPath = Join-Path $repoRoot "installer\build-logging-installer.ps1"

$project = Get-Content $projectPath -Raw
$globals = Get-Content $globalsPath -Raw
if (-not (Test-Path $builderPath)) { throw "Missing diagnostic builder: $builderPath" }
$builder = Get-Content $builderPath -Raw

$expected = @(
    "DIME.cpp", "ActiveLanguageProfileNotifySink.cpp", "ThreadMgrEventSink.cpp",
    "ThreadFocusSink.cpp", "KeyEventSink.cpp", "Composition.cpp",
    "StartComposition.cpp", "EndComposition.cpp", "TextEditSink.cpp",
    "TfTextLayoutSink.cpp", "Server.cpp", "UIACaretTracker.cpp"
)
foreach ($source in $expected) {
    $pattern = '<ClCompile Include="' + [regex]::Escape($source) + '">[\s\S]*?<PreprocessorDefinitions Condition="''\$\(DiagnosticLogging\)''==''true''">DEBUG_PRINT;%\(PreprocessorDefinitions\)</PreprocessorDefinitions>[\s\S]*?</ClCompile>'
    if ($project -notmatch $pattern) { throw "DiagnosticLogging is not enabled for $source" }
}
$definitions = [regex]::Matches($project, '<PreprocessorDefinitions Condition="''\$\(DiagnosticLogging\)''==''true''">DEBUG_PRINT;%\(PreprocessorDefinitions\)</PreprocessorDefinitions>')
if ($definitions.Count -ne $expected.Count) { throw "Expected $($expected.Count) selective definitions, found $($definitions.Count)" }

foreach ($token in @('GetCurrentProcessId()', 'GetCurrentThreadId()', 'GetModuleFileNameW', 'GetLocalTime', 'debug-')) {
    if (-not $globals.Contains($token)) { throw "Logger missing $token" }
}
if ($globals -notmatch 'if \(!PathFileExists\(logDirectory\) &&') { throw "Logger directory creation is not guarded correctly" }

foreach ($token in @('vswhere.exe', 'Microsoft.VisualStudio.Component.VC.Runtimes.x86.x64.Spectre', 'Microsoft.VisualStudio.Component.VC.Runtimes.ARM64EC.Spectre', '/p:Configuration=Release', '/p:RunCodeAnalysis=false', '/p:DiagnosticLogging=true', 'Push-Location $PSScriptRoot', '-SkipChecksumUpdate', 'DIME-Universal-logging.exe', 'DIME-Universal-logging.zip')) {
    if (-not $builder.Contains($token)) { throw "Builder missing $token" }
}

Write-Host "Diagnostic installer contract: PASS" -ForegroundColor Green
