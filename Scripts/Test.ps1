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
    [ValidateNotNullOrEmpty()]
    [string] $Filter = 'UEGT2',
    [ValidateRange(1, 1440)]
    [int] $TimeoutMinutes = 20
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$projectRoot = Split-Path -Parent $PSScriptRoot
$projectFile = Join-Path $projectRoot 'UEGT2.uproject'
$engine = & (Join-Path $PSScriptRoot 'Resolve-Engine.ps1') -EngineRoot $EngineRoot

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'Build.ps1') -Target Both -EngineRoot $engine | Out-Null
}

$editorCmd = Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$logDir = Join-Path $projectRoot 'Saved\Logs'
$reportDir = Join-Path $projectRoot 'Saved\TestReports'
New-Item -ItemType Directory -Path $logDir, $reportDir -Force | Out-Null
$log = Join-Path $logDir 'Test.log'
$report = Join-Path $reportDir 'index.json'
# An editor crash may not write either file. Never interpret the previous
# run's successful report as evidence for this invocation.
foreach ($old in @($log, $report)) {
    if (Test-Path -LiteralPath $old) { Remove-Item -LiteralPath $old -Force }
}

# Start-Process joins ArgumentList into one command line: retain quotes around
# paths and multiword values, especially the automation command and TestExit.
$arguments = @(
    "`"$projectFile`"", '-unattended', '-nopause', '-nosplash', '-nop4', '-NullRHI',
    "-ExecCmds=`"Automation RunTests $Filter; Quit`"",
    '-TestExit="Automation Test Queue Empty"',
    "-ReportExportPath=`"$reportDir`"", "-abslog=`"$log`""
)
Write-Host "Running automation tests matching '$Filter'"
$process = Start-Process -FilePath $editorCmd -ArgumentList $arguments -PassThru -WindowStyle Hidden `
    -RedirectStandardOutput (Join-Path $logDir 'Test.stdout.log') `
    -RedirectStandardError (Join-Path $logDir 'Test.stderr.log')
# Cache the handle before waiting: Windows PowerShell can otherwise lose the
# exit code of a redirected process after it exits.
$null = $process.Handle
if (-not $process.WaitForExit($TimeoutMinutes * 60 * 1000)) {
    Stop-Process -Id $process.Id -Force
    throw "Automation exceeded $TimeoutMinutes minutes. Inspect $log."
}
if ($process.ExitCode -ne 0) {
    throw "Automation editor failed with exit code $($process.ExitCode). Inspect $log."
}

if (-not (Test-Path -LiteralPath $report -PathType Leaf)) {
    if (Test-Path -LiteralPath $log -PathType Leaf) {
        Write-Host '--- last 30 log lines ---' -ForegroundColor Red
        Get-Content -LiteralPath $log -Tail 30 | ForEach-Object { Write-Host $_ }
    }
    throw "Automation produced no report at $report."
}

$results = Get-Content -LiteralPath $report -Raw | ConvertFrom-Json
foreach ($test in $results.tests) {
    $state = $test.state
    $colour = if ($state -eq 'Success') { 'Green' } else { 'Red' }
    Write-Host ("  {0,-40} {1}" -f $test.fullTestPath, $state) -ForegroundColor $colour
}
$passed = $results.succeeded + $results.succeededWithWarnings
$unfinished = $results.notRun + $results.inProcess
$unsuccessful = @($results.tests | Where-Object { $_.state -ne 'Success' })
if ($results.failed -gt 0 -or $passed -lt 1 -or $unfinished -gt 0 -or
    $unsuccessful.Count -gt 0 -or @($results.tests).Count -ne $passed) {
    throw "Automation failed or incomplete: $passed passed, $($results.failed) failed, $unfinished unfinished. Report: $report"
}
Write-Host "Automation passed: $passed test(s). Report: $report"
