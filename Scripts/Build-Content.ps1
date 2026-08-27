<#
.SYNOPSIS
Runs the Fairhaven content build inside UnrealEditor-Cmd.

.DESCRIPTION
Generates materials, meshes, the landscape, water, lighting and world content
from Tools/Python. Editor output goes to Saved/Logs/ContentBuild.log; only the
project's own [UEGT2] lines and any errors are echoed here.

.EXAMPLE
./Scripts/Build-Content.ps1                          # every stage
./Scripts/Build-Content.ps1 -Stages materials,meshes
#>
[CmdletBinding()]
param(
    [string[]] $Stages = @('all'),
    [string] $EngineRoot,
    [switch] $Rendering,
    [int] $TimeoutMinutes = 90
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$projectRoot = Split-Path -Parent $PSScriptRoot
$projectFile = Join-Path $projectRoot 'UEGT2.uproject'
$engine = & (Join-Path $PSScriptRoot 'Resolve-Engine.ps1') -EngineRoot $EngineRoot
$editorCmd = Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'

# Forward slashes on purpose: Unreal's -script= argument processes backslash
# escape sequences, so a path containing \u (as in \uegt2) is silently corrupted.
$script = (Join-Path $projectRoot 'Tools\Python\build_content.py').Replace('\', '/')

$logDir = Join-Path $projectRoot 'Saved\Logs'
New-Item -ItemType Directory -Path $logDir -Force | Out-Null
$log = Join-Path $logDir 'ContentBuild.log'
$stdout = Join-Path $logDir 'ContentBuild.stdout.log'
foreach ($old in @($log, $stdout)) {
    if (Test-Path -LiteralPath $old) { Remove-Item -LiteralPath $old -Force }
}

$stageArg = ($Stages -join ',')
$arguments = @(
    $projectFile,
    '-run=pythonscript',
    "-script=`"$script $stageArg`"",
    '-unattended', '-nopause', '-nosplash', '-nop4',
    "-abslog=$log"
)
if (-not $Rendering) { $arguments += '-NullRHI' }

Write-Host "Content build starting (stages: $stageArg)"
$started = Get-Date
$process = Start-Process -FilePath $editorCmd -ArgumentList $arguments -PassThru `
    -RedirectStandardOutput $stdout -RedirectStandardError (Join-Path $logDir 'ContentBuild.stderr.log') `
    -WindowStyle Hidden
if (-not $process.WaitForExit($TimeoutMinutes * 60 * 1000)) {
    Stop-Process -Id $process.Id -Force
    throw "Content build exceeded $TimeoutMinutes minutes. Inspect $log."
}
$elapsed = (Get-Date) - $started

if (-not (Test-Path -LiteralPath $log -PathType Leaf)) {
    throw "Content build produced no log at $log."
}

# Echo the project's own progress lines, which are the useful signal.
Select-String -LiteralPath $log -Pattern '\[UEGT2\]' |
    ForEach-Object { ($_.Line -replace '^\[[^\]]+\]\[\s*\d+\]', '') -replace '^LogPython: (Display|Warning|Error): ', '' } |
    ForEach-Object { Write-Host "  $_" }

$text = Get-Content -LiteralPath $log -Raw
if (-not $text.Contains('UEGT2_CONTENT_BUILD_SUCCEEDED')) {
    Write-Host ''
    Write-Host '--- errors ---' -ForegroundColor Red
    Select-String -LiteralPath $log -Pattern 'Error:' |
        Select-Object -Last 40 |
        ForEach-Object { Write-Host ($_.Line -replace '^\[[^\]]+\]\[\s*\d+\]', '') }
    throw "Content build did not report success. Full log: $log"
}

# A material that fails to compile silently falls back to the default material,
# which renders black. Never let that pass as a successful build.
$materialFailures = @(Select-String -LiteralPath $log -Pattern 'Failed to compile Material|doesn''t have a valid ShaderMap')
if ($materialFailures.Count -gt 0) {
    Write-Host '--- material compile failures ---' -ForegroundColor Red
    $materialFailures | Select-Object -First 10 |
        ForEach-Object { Write-Host ($_.Line -replace '^\[[^\]]+\]\[\s*\d+\]', '') }
    throw "Content build produced materials that do not compile. Full log: $log"
}

Write-Host ("Content build succeeded in {0:mm\:ss}. Log: {1}" -f $elapsed, $log)
