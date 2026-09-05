<#
.SYNOPSIS
Checks packaged HUD sizes with a fixed player, prompt, speech and tracking scene.

.DESCRIPTION
Uses an isolated user directory with persistence disabled. -Capture records the
same scene at Normal, Large, Larger and hard-off plus the real Gameplay setting.
Use -Width 1280 -Height 720 for the smaller layout check.
#>
[CmdletBinding()]
param(
    [ValidateRange(1, 1440)]
    [int] $TimeoutMinutes = 5,
    [ValidateRange(640, 7680)]
    [int] $Width = 1920,
    [ValidateRange(480, 4320)]
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
$userDir = [IO.Path]::GetFullPath((Join-Path $packagedProject "Saved\HudSizeSmoke\$runId"))
$ownedPrefix = [IO.Path]::GetFullPath((Join-Path $packagedProject 'Saved\HudSizeSmoke')).TrimEnd('\') + '\'
if (-not $userDir.StartsWith($ownedPrefix, [StringComparison]::OrdinalIgnoreCase) -or (Split-Path -Leaf $userDir) -ne $runId) {
    throw 'HUD size smoke user directory escaped the packaged build.'
}
$logDir = Join-Path $projectRoot 'Saved\Logs'
$captureDir = Join-Path $projectRoot "Saved\Screenshots\HudSizeSmoke\$runId"
$log = Join-Path $logDir "HudSizeSmoke-$runId.log"
New-Item -ItemType Directory -Path $userDir, $logDir -Force | Out-Null
if ($Capture) { New-Item -ItemType Directory -Path $captureDir -Force | Out-Null }
$arguments = @(
    '-RenderOffscreen', '-Windowed', '-ForceRes', "-ResX=$Width", "-ResY=$Height",
    '-unattended', '-nosplash', '-nopause', '-nosound', '-UEGT2SkipMenu', '-UEGT2HudSizeSmoke',
    "-UserDir=`"$($userDir.Replace('\', '/'))`"", "-abslog=`"$log`""
)
if ($Capture) { $arguments += "-UEGT2HudSizeCapture=`"$($captureDir.Replace('\', '/'))`"" }

Write-Host "HUD size smoke: $($exe.FullName) at ${Width}x$Height"
$process = Start-Process -FilePath $exe.FullName -ArgumentList $arguments -PassThru -WindowStyle Hidden `
    -RedirectStandardOutput (Join-Path $logDir "HudSizeSmoke-$runId.stdout.log") `
    -RedirectStandardError (Join-Path $logDir "HudSizeSmoke-$runId.stderr.log")
# Retain the handle before a redirected wait so Windows PowerShell reports exit.
$null = $process.Handle
try {
    if (-not $process.WaitForExit($TimeoutMinutes * 60 * 1000)) {
        throw "HUD size smoke exceeded $TimeoutMinutes minutes. Log: $log"
    }
    if ($process.ExitCode -ne 0) { throw "HUD size smoke exited with code $($process.ExitCode). Log: $log" }
} finally {
    if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force; $null = $process.WaitForExit(10000) }
    $process.Dispose()
}
if (-not (Test-Path -LiteralPath $log -PathType Leaf)) { throw "HUD size smoke produced no log at $log." }
$text = Get-Content -LiteralPath $log -Raw
if (-not $text.Contains("UEGT2_HUD_SIZE_SMOKE_COMPLETE run=$runId ") -or
    $text -match 'UEGT2_HUD_SIZE_SMOKE_FAILED|Critical error|Assertion failed|invalid ShaderMap') {
    throw "HUD size smoke failed or did not complete. Log: $log"
}
# Retain all isolated data for diagnosis. This feature never needs checkpoint IO.
$saveFiles = @(Get-ChildItem -LiteralPath (Join-Path $userDir 'Saved\SaveGames') -Filter '*.sav' -File -Recurse -ErrorAction SilentlyContinue)
if ($saveFiles.Count -gt 0) { throw "HUD size smoke unexpectedly wrote a checkpoint under $userDir." }
if ($Capture) {
    foreach ($name in @('01_Normal.png', '02_Large.png', '03_Larger.png', '04_HardOff.png', '05_HudSizeSetting.png')) {
        $shot = Join-Path $captureDir $name
        if (-not (Test-Path -LiteralPath $shot -PathType Leaf) -or (Get-Item -LiteralPath $shot).Length -eq 0) {
            throw "HUD size smoke did not produce $shot."
        }
    }
    Write-Host "HUD size screenshots: $captureDir"
}
Write-Host "HUD size smoke passed: same-scene size comparison, hard-off baseline and actual setting. Log: $log" -ForegroundColor Green
