<#
.SYNOPSIS
Packages Fairhaven into a standalone build under LocalBuilds.

.DESCRIPTION
Runs the engine's BuildCookRun. The first package is slow because every shader
has to be compiled; later packages reuse the derived data cache and are much
faster.

.EXAMPLE
./Scripts/Package.ps1
./Scripts/Package.ps1 -Configuration Shipping
#>
[CmdletBinding()]
param(
    [ValidateSet('DebugGame', 'Development', 'Shipping', 'Test')]
    [string] $Configuration = 'Development',
    [string] $OutputDirectory,
    [string] $EngineRoot,
    [int] $TimeoutMinutes = 120
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$projectRoot = Split-Path -Parent $PSScriptRoot
$projectFile = Join-Path $projectRoot 'UEGT2.uproject'
$engine = & (Join-Path $PSScriptRoot 'Resolve-Engine.ps1') -EngineRoot $EngineRoot
$uat = Join-Path $engine 'Engine\Build\BatchFiles\RunUAT.bat'

if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $projectRoot "LocalBuilds\Windows-$Configuration"
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)

$logDir = Join-Path $projectRoot 'Saved\Logs'
New-Item -ItemType Directory -Path $logDir -Force | Out-Null
$log = Join-Path $logDir 'Package.log'

$uatArgs = @(
    'BuildCookRun',
    "-project=$projectFile",
    '-noP4', '-utf8output', '-nocompileeditor',
    '-platform=Win64',
    "-clientconfig=$Configuration",
    # -nozenstore matters: without it an incremental cook can stage a build that
    # streams content from a local Zen server, which then fails to launch
    # standalone. We always want self-contained pak files.
    '-build', '-cook', '-stage', '-pak', '-iostore', '-nozenstore',
    '-package', '-archive', '-compressed',
    "-archivedirectory=$OutputDirectory"
)

Write-Host "Packaging Fairhaven Win64 $Configuration -> $OutputDirectory"
$started = Get-Date
& $uat @uatArgs *>&1 | Tee-Object -FilePath $log | Select-String -Pattern 'ERROR|WARNING: .*failed|Took |BUILD SUCCESSFUL|BUILD FAILED|Cook: ' |
    Select-Object -Last 0 | Out-Null
if ($LASTEXITCODE -ne 0) {
    Write-Host '--- last 40 log lines ---' -ForegroundColor Red
    Get-Content -LiteralPath $log -Tail 40 | ForEach-Object { Write-Host $_ }
    throw "Packaging failed with exit code $LASTEXITCODE. Log: $log"
}
$elapsed = (Get-Date) - $started

# The cook is where material shaders are actually compiled, so this is the only
# place a broken material shows up. A failed compile silently swaps in the
# default material, which renders black in game.
$materialFailures = @(Select-String -LiteralPath $log -Pattern 'Failed to compile Material' |
    Where-Object { $_.Line -notmatch 'LogInit: Display:' })
if ($materialFailures.Count -gt 0) {
    Write-Host '--- material compile failures during cook ---' -ForegroundColor Red
    foreach ($failure in ($materialFailures | Select-Object -First 6)) {
        Write-Host $failure.Line
        $detail = (Get-Content -LiteralPath $log)[$failure.LineNumber]
        if ($detail) { Write-Host "   $detail" }
    }
    throw "Cook produced materials that do not compile. Log: $log"
}

$exe = Get-ChildItem -LiteralPath $OutputDirectory -Recurse -File -Filter 'UEGT2.exe' -ErrorAction SilentlyContinue |
    Select-Object -First 1
if (-not $exe) {
    throw "Packaging finished but produced no UEGT2.exe under $OutputDirectory."
}

$sizeMb = [math]::Round(((Get-ChildItem -LiteralPath $OutputDirectory -Recurse -File |
    Measure-Object -Property Length -Sum).Sum / 1MB), 1)
Write-Host ("Packaged in {0:hh\:mm\:ss}. {1} MB" -f $elapsed, $sizeMb)
Write-Host "Executable: $($exe.FullName)"
