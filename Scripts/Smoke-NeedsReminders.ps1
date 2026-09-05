<#
.SYNOPSIS
Checks optional needs reminders and their disabled paths in a packaged game.

.DESCRIPTION
Uses a fresh isolated user directory and retains all evidence. No checkpoint
slot is supplied, and any save file under the entire user directory fails.
Use -Capture for reminder, ordinary-message, disabled-state and settings PNGs.
When several packages exist, select the intended real binary explicitly.
#>
[CmdletBinding()]
param(
    [string] $PackagedExecutable,
    [ValidateRange(1, 30)]
    [int] $TimeoutMinutes = 5,
    [ValidateRange(1280, 7680)]
    [int] $Width = 1920,
    [ValidateRange(720, 4320)]
    [int] $Height = 1080,
    [switch] $Capture
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$projectRoot = Split-Path -Parent $PSScriptRoot
if ($PackagedExecutable) {
    $path = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($PackagedExecutable)
    if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or $path -notlike '*\Binaries\Win64\UEGT2.exe') {
        throw 'PackagedExecutable must name the real Binaries\Win64\UEGT2.exe of a packaged build.'
    }
    $exe = Get-Item -LiteralPath $path
} else {
    $candidates = @(Get-ChildItem -LiteralPath (Join-Path $projectRoot 'LocalBuilds') -Recurse -File `
        -Filter 'UEGT2.exe' -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -like '*\Binaries\Win64\UEGT2.exe' })
    if ($candidates.Count -eq 0) { throw 'No packaged build found. Run ./Scripts/Package.ps1 first.' }
    # A content-only cook preserves the executable's compile timestamp. It
    # cannot identify which of several archives contains the current content.
    if ($candidates.Count -ne 1) { throw 'Multiple packaged builds found. Select one with -PackagedExecutable.' }
    $exe = $candidates[0]
}

$packagedProject = Split-Path -Parent (Split-Path -Parent $exe.DirectoryName)
$runId = [guid]::NewGuid().ToString('N')
$userDir = [IO.Path]::GetFullPath((Join-Path $packagedProject "Saved\NeedsRemindersSmoke\$runId"))
$ownedPrefix = [IO.Path]::GetFullPath((Join-Path $packagedProject 'Saved\NeedsRemindersSmoke')).TrimEnd('\') + '\'
if ($runId -notmatch '^[0-9a-f]{32}$' -or
    -not $userDir.StartsWith($ownedPrefix, [StringComparison]::OrdinalIgnoreCase) -or
    (Split-Path -Leaf $userDir) -ne $runId) {
    throw 'Needs reminders user directory escaped the packaged build.'
}
$logDir = Join-Path $projectRoot 'Saved\Logs'
$log = Join-Path $logDir "NeedsRemindersSmoke-$runId.log"
$stdout = Join-Path $logDir "NeedsRemindersSmoke-$runId.stdout.log"
$stderr = Join-Path $logDir "NeedsRemindersSmoke-$runId.stderr.log"
$captureDir = Join-Path $projectRoot "Saved\Screenshots\NeedsRemindersSmoke\$runId"
foreach ($path in @($userDir, $log, $stdout, $stderr, $captureDir)) {
    if (Test-Path -LiteralPath $path) { throw "Needs reminders require fresh evidence paths: $path" }
}
New-Item -ItemType Directory -Path $userDir, $logDir -Force | Out-Null
if ($Capture) { New-Item -ItemType Directory -Path $captureDir -Force | Out-Null }
$arguments = @(
    '-RenderOffscreen', '-Windowed', '-ForceRes', "-ResX=$Width", "-ResY=$Height",
    '-unattended', '-nosplash', '-nopause', '-nosound', '-UEGT2SkipMenu', '-UEGT2NeedsRemindersSmoke',
    "-UserDir=`"$($userDir.Replace('\', '/'))`"", "-abslog=`"$log`""
)
if ($Capture) { $arguments += "-UEGT2NeedsRemindersCapture=`"$($captureDir.Replace('\', '/'))`"" }

Write-Host "Needs reminders: $($exe.FullName) at ${Width}x$Height; run=$runId"
$process = Start-Process -FilePath $exe.FullName -ArgumentList $arguments -PassThru -WindowStyle Hidden `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr
# Cache the handle before a redirected wait so Windows PowerShell retains exit.
$null = $process.Handle
try {
    if (-not $process.WaitForExit($TimeoutMinutes * 60 * 1000)) {
        throw "Needs reminders exceeded $TimeoutMinutes minutes. Log: $log"
    }
    if ($process.ExitCode -ne 0) { throw "Needs reminders exited with code $($process.ExitCode). Log: $log" }
} finally {
    if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force; $null = $process.WaitForExit(10000) }
    $process.Dispose()
}
if (-not (Test-Path -LiteralPath $log -PathType Leaf)) { throw "Needs reminders produced no log at $log." }
$text = Get-Content -LiteralPath $log -Raw
if (-not $text.Contains("UEGT2_NEEDS_REMINDERS_SMOKE_COMPLETE run=$runId ") -or
    $text -match 'UEGT2_NEEDS_REMINDERS_SMOKE_FAILED|Critical error|Assertion failed|invalid ShaderMap') {
    throw "Needs reminders failed or did not complete. Log: $log"
}
# Scan the whole owned directory so even an incorrectly placed checkpoint is
# caught. Keep unexpected files as evidence; never remove player saves.
$saveFiles = @(Get-ChildItem -LiteralPath $userDir -Filter '*.sav' -File -Recurse)
if ($saveFiles.Count -gt 0) { throw "Needs reminders unexpectedly wrote a checkpoint under $userDir." }
if ($Capture) {
    $expected = @('01_Reminder.png', '02_OrdinaryMessage.png', '03_PlayerOff.png',
        '04_HardOff.png', '05_ReminderSetting.png')
    foreach ($name in $expected) {
        $shot = Join-Path $captureDir $name
        if (-not (Test-Path -LiteralPath $shot -PathType Leaf) -or (Get-Item -LiteralPath $shot).Length -eq 0) {
            throw "Needs reminders capture missing or empty: $shot. Log: $log"
        }
    }
    if (@(Get-ChildItem -LiteralPath $captureDir -Filter '*.png' -File).Count -ne $expected.Count) {
        throw "Needs reminders capture count differs from the expected five images. Log: $log"
    }
    Write-Host "Needs reminders captures: $captureDir"
}
Write-Host "Needs reminders passed: reminder transitions, ordinary-message priority and disabled paths. Log: $log" -ForegroundColor Green
