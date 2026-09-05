<#
.SYNOPSIS
Checks the packaged survey journal, rebound input, tracking and both off switches.

.DESCRIPTION
Runs with an isolated user directory and progress saving disabled. -Capture
records the empty/surveyed journal, tracking HUD, pause root and gameplay settings.
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
$userDir = [IO.Path]::GetFullPath((Join-Path $packagedProject "Saved\SurveySmoke\$runId"))
$ownedPrefix = [IO.Path]::GetFullPath((Join-Path $packagedProject 'Saved\SurveySmoke')).TrimEnd('\') + '\'
if (-not $userDir.StartsWith($ownedPrefix, [StringComparison]::OrdinalIgnoreCase) -or
    (Split-Path -Leaf $userDir) -ne $runId) {
    throw 'Survey smoke user directory escaped the packaged build.'
}
$logDir = Join-Path $projectRoot 'Saved\Logs'
$captureDir = Join-Path $projectRoot "Saved\Screenshots\SurveySmoke\$runId"
$log = Join-Path $logDir "SurveySmoke-$runId.log"
New-Item -ItemType Directory -Path $userDir, $logDir -Force | Out-Null
if ($Capture) { New-Item -ItemType Directory -Path $captureDir -Force | Out-Null }
$arguments = @(
    '-RenderOffscreen', '-Windowed', '-ForceRes', "-ResX=$Width", "-ResY=$Height",
    '-unattended', '-nosplash', '-nopause', '-nosound', '-UEGT2SkipMenu', '-UEGT2SurveySmoke',
    "-UserDir=`"$($userDir.Replace('\', '/'))`"", "-abslog=`"$log`""
)
if ($Capture) { $arguments += "-UEGT2SurveyCapture=`"$($captureDir.Replace('\', '/'))`"" }

Write-Host "Survey smoke: $($exe.FullName) at ${Width}x$Height"
$process = Start-Process -FilePath $exe.FullName -ArgumentList $arguments -PassThru -WindowStyle Hidden `
    -RedirectStandardOutput (Join-Path $logDir "SurveySmoke-$runId.stdout.log") `
    -RedirectStandardError (Join-Path $logDir "SurveySmoke-$runId.stderr.log")
# Retain the handle before a redirected wait so Windows PowerShell reports exit.
$null = $process.Handle
try {
    if (-not $process.WaitForExit($TimeoutMinutes * 60 * 1000)) {
        throw "Survey smoke exceeded $TimeoutMinutes minutes. Log: $log"
    }
    if ($process.ExitCode -ne 0) { throw "Survey smoke exited with code $($process.ExitCode). Log: $log" }
} finally {
    if (-not $process.HasExited) {
        Stop-Process -Id $process.Id -Force
        $null = $process.WaitForExit(10000)
    }
    $process.Dispose()
}
if (-not (Test-Path -LiteralPath $log -PathType Leaf)) { throw "Survey smoke produced no log at $log." }
$text = Get-Content -LiteralPath $log -Raw
if (-not $text.Contains("UEGT2_SURVEY_SMOKE_COMPLETE run=$runId ") -or
    $text -match 'UEGT2_SURVEY_SMOKE_FAILED|Critical error|Assertion failed|invalid ShaderMap') {
    throw "Survey smoke failed or did not complete. Log: $log"
}
# No checkpoint IO belongs to this feature. Retain the isolated directory for
# diagnosis, including unexpected files; this runner never deletes user data.
$saveFiles = @(Get-ChildItem -LiteralPath (Join-Path $userDir 'Saved\SaveGames') -Filter '*.sav' -File -Recurse -ErrorAction SilentlyContinue)
if ($saveFiles.Count -gt 0) { throw "Survey smoke unexpectedly wrote a checkpoint under $userDir." }
if ($Capture) {
    foreach ($name in @('01_Empty.png', '02_Surveyed.png', '03_Tracking.png', '04_PauseRoot.png', '05_Settings.png')) {
        $shot = Join-Path $captureDir $name
        if (-not (Test-Path -LiteralPath $shot -PathType Leaf) -or (Get-Item -LiteralPath $shot).Length -eq 0) {
            throw "Survey smoke did not produce $shot."
        }
    }
    Write-Host "Survey screenshots: $captureDir"
}
Write-Host "Survey smoke passed: rebound key, Slate close, tracking and both off switches. Log: $log" -ForegroundColor Green
