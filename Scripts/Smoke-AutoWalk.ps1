<#
.SYNOPSIS
Checks packaged auto-walk input, steering, cancellation, off gates and indicator.

.DESCRIPTION
Uses an isolated user directory with persistence disabled and a bounded clear
route. -Capture records the live indicator at Normal/Larger with Needs hidden,
plus the real Gameplay setting. Indicator captures keep walking active; the
settings capture opens the paused menu after cancellation.
Use -Width 1280 -Height 720 for the smaller layout check.
#>
[CmdletBinding()]
param(
    [ValidateRange(1, 1440)]
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
$candidates = @(Get-ChildItem -LiteralPath (Join-Path $projectRoot 'LocalBuilds') -Recurse -File `
    -Filter 'UEGT2.exe' -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending)
$exe = $candidates | Where-Object { $_.FullName -like '*\Binaries\Win64\UEGT2.exe' } | Select-Object -First 1
if (-not $exe) { throw 'No packaged build found. Run ./Scripts/Package.ps1 first.' }

$packagedProject = Split-Path -Parent (Split-Path -Parent $exe.DirectoryName)
$runId = [guid]::NewGuid().ToString('N')
$userDir = [IO.Path]::GetFullPath((Join-Path $packagedProject "Saved\AutoWalkSmoke\$runId"))
$ownedPrefix = [IO.Path]::GetFullPath((Join-Path $packagedProject 'Saved\AutoWalkSmoke')).TrimEnd('\') + '\'
if (-not $userDir.StartsWith($ownedPrefix, [StringComparison]::OrdinalIgnoreCase) -or (Split-Path -Leaf $userDir) -ne $runId) {
    throw 'Auto-walk smoke user directory escaped the packaged build.'
}
$logDir = Join-Path $projectRoot 'Saved\Logs'
$captureDir = Join-Path $projectRoot "Saved\Screenshots\AutoWalkSmoke\$runId"
$log = Join-Path $logDir "AutoWalkSmoke-$runId.log"
New-Item -ItemType Directory -Path $userDir, $logDir -Force | Out-Null
if ($Capture) { New-Item -ItemType Directory -Path $captureDir -Force | Out-Null }
$arguments = @(
    '-RenderOffscreen', '-Windowed', '-ForceRes', "-ResX=$Width", "-ResY=$Height",
    '-unattended', '-nosplash', '-nopause', '-nosound', '-UEGT2SkipMenu', '-UEGT2AutoWalkSmoke',
    "-UserDir=`"$($userDir.Replace('\', '/'))`"", "-abslog=`"$log`""
)
if ($Capture) { $arguments += "-UEGT2AutoWalkCapture=`"$($captureDir.Replace('\', '/'))`"" }

Write-Host "Auto-walk smoke: $($exe.FullName) at ${Width}x$Height"
$process = Start-Process -FilePath $exe.FullName -ArgumentList $arguments -PassThru -WindowStyle Hidden `
    -RedirectStandardOutput (Join-Path $logDir "AutoWalkSmoke-$runId.stdout.log") `
    -RedirectStandardError (Join-Path $logDir "AutoWalkSmoke-$runId.stderr.log")
# Retain the handle before a redirected wait so Windows PowerShell reports exit.
$null = $process.Handle
try {
    if (-not $process.WaitForExit($TimeoutMinutes * 60 * 1000)) {
        throw "Auto-walk smoke exceeded $TimeoutMinutes minutes. Log: $log"
    }
    if ($process.ExitCode -ne 0) { throw "Auto-walk smoke exited with code $($process.ExitCode). Log: $log" }
} finally {
    if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force; $null = $process.WaitForExit(10000) }
    $process.Dispose()
}
if (-not (Test-Path -LiteralPath $log -PathType Leaf)) { throw "Auto-walk smoke produced no log at $log." }
$text = Get-Content -LiteralPath $log -Raw
if (-not $text.Contains("UEGT2_AUTO_WALK_SMOKE_COMPLETE run=$runId ") -or
    $text -match 'UEGT2_AUTO_WALK_SMOKE_FAILED|Critical error|Assertion failed|invalid ShaderMap') {
    throw "Auto-walk smoke failed or did not complete. Log: $log"
}
# Retain all isolated data for diagnosis. This feature never needs checkpoint IO.
$saveFiles = @(Get-ChildItem -LiteralPath (Join-Path $userDir 'Saved\SaveGames') -Filter '*.sav' -File -Recurse -ErrorAction SilentlyContinue)
if ($saveFiles.Count -gt 0) { throw "Auto-walk smoke unexpectedly wrote a checkpoint under $userDir." }
if ($Capture) {
    foreach ($name in @('01_ActiveNormal.png', '02_ActiveLarger.png', '03_AutoWalkSetting.png')) {
        $shot = Join-Path $captureDir $name
        if (-not (Test-Path -LiteralPath $shot -PathType Leaf) -or (Get-Item -LiteralPath $shot).Length -eq 0) {
            throw "Auto-walk smoke did not produce $shot."
        }
    }
    Write-Host "Auto-walk screenshots: $captureDir"
}
Write-Host "Auto-walk smoke passed: input, steering, cancellations, off gates and independent indicator. Log: $log" -ForegroundColor Green
