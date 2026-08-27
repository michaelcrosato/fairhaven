<#
.SYNOPSIS
Runs the UEGT2 automation tests headlessly and fails on any error.

.DESCRIPTION
Covers the failure modes that have actually bitten this project: materials that
do not compile, meshes without vertex colours, and a map that lost its
landscape, scatter or interactables.
#>
[CmdletBinding()]
param(
    [string] $EngineRoot,
    [switch] $SkipBuild,
    [string] $Filter = 'UEGT2'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$projectRoot = Split-Path -Parent $PSScriptRoot
$projectFile = Join-Path $projectRoot 'UEGT2.uproject'
$engine = & (Join-Path $PSScriptRoot 'Resolve-Engine.ps1') -EngineRoot $EngineRoot

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'Build.ps1') -Target Editor -EngineRoot $engine | Out-Null
}

$editorCmd = Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$logDir = Join-Path $projectRoot 'Saved\Logs'
$reportDir = Join-Path $projectRoot 'Saved\TestReports'
New-Item -ItemType Directory -Path $logDir, $reportDir -Force | Out-Null
$log = Join-Path $logDir 'Test.log'

# Invoked with the call operator, not Start-Process: -ExecCmds contains spaces
# and a semicolon, and Start-Process -ArgumentList splits it into pieces.
Write-Host "Running automation tests matching '$Filter'"
& $editorCmd $projectFile -unattended -nopause -nosplash -nop4 -NullRHI `
    "-ExecCmds=Automation RunTests $Filter; Quit" `
    '-TestExit=Automation Test Queue Empty' `
    "-ReportExportPath=$reportDir" `
    "-abslog=$log" | Out-Null

$report = Join-Path $reportDir 'index.json'
if (-not (Test-Path -LiteralPath $report -PathType Leaf)) {
    Write-Host '--- last 30 log lines ---' -ForegroundColor Red
    Get-Content -LiteralPath $log -Tail 30 | ForEach-Object { Write-Host $_ }
    throw "Automation produced no report at $report."
}

$results = Get-Content -LiteralPath $report -Raw | ConvertFrom-Json
foreach ($test in $results.tests) {
    $state = $test.state
    $colour = if ($state -eq 'Success') { 'Green' } else { 'Red' }
    Write-Host ("  {0,-40} {1}" -f $test.fullTestPath, $state) -ForegroundColor $colour
}
if ($results.failed -gt 0 -or $results.succeeded -lt 1) {
    throw "Automation failed: $($results.succeeded) passed, $($results.failed) failed."
}
Write-Host "Automation passed: $($results.succeeded) test(s). Report: $report"
