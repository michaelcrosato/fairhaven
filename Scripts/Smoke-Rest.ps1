<#
.SYNOPSIS
Checks the packaged bed interaction, chosen-hour sleep, waking and off switches.

.DESCRIPTION
Uses an isolated user directory with checkpoint saving disabled. -Capture records
the real rest panel and waking HUD. The full generated population participates in
a 24-hour skip; logs include its elapsed time and post-wake ledger checks.
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
$userDir = [IO.Path]::GetFullPath((Join-Path $packagedProject "Saved\RestSmoke\$runId"))
$ownedPrefix = [IO.Path]::GetFullPath((Join-Path $packagedProject 'Saved\RestSmoke')).TrimEnd('\') + '\'
if (-not $userDir.StartsWith($ownedPrefix, [StringComparison]::OrdinalIgnoreCase) -or
    (Split-Path -Leaf $userDir) -ne $runId) {
    throw 'Rest smoke user directory escaped the packaged build.'
}
$logDir = Join-Path $projectRoot 'Saved\Logs'
$captureDir = Join-Path $projectRoot "Saved\Screenshots\RestSmoke\$runId"
$log = Join-Path $logDir "RestSmoke-$runId.log"
New-Item -ItemType Directory -Path $userDir, $logDir -Force | Out-Null
if ($Capture) { New-Item -ItemType Directory -Path $captureDir -Force | Out-Null }
$arguments = @(
    '-RenderOffscreen', '-Windowed', '-ForceRes', "-ResX=$Width", "-ResY=$Height",
    '-unattended', '-nosplash', '-nopause', '-nosound', '-UEGT2SkipMenu', '-UEGT2RestSmoke',
    "-UserDir=`"$($userDir.Replace('\', '/'))`"", "-abslog=`"$log`""
)
if ($Capture) { $arguments += "-UEGT2RestCapture=`"$($captureDir.Replace('\', '/'))`"" }

Write-Host "Rest smoke: $($exe.FullName) at ${Width}x$Height"
$process = Start-Process -FilePath $exe.FullName -ArgumentList $arguments -PassThru -WindowStyle Hidden `
    -RedirectStandardOutput (Join-Path $logDir "RestSmoke-$runId.stdout.log") `
    -RedirectStandardError (Join-Path $logDir "RestSmoke-$runId.stderr.log")
# Retain the handle before a redirected wait so Windows PowerShell reports exit.
$null = $process.Handle
try {
    if (-not $process.WaitForExit($TimeoutMinutes * 60 * 1000)) {
        throw "Rest smoke exceeded $TimeoutMinutes minutes. Log: $log"
    }
    if ($process.ExitCode -ne 0) { throw "Rest smoke exited with code $($process.ExitCode). Log: $log" }
} finally {
    if (-not $process.HasExited) {
        Stop-Process -Id $process.Id -Force
        $null = $process.WaitForExit(10000)
    }
    $process.Dispose()
}
if (-not (Test-Path -LiteralPath $log -PathType Leaf)) { throw "Rest smoke produced no log at $log." }
$text = Get-Content -LiteralPath $log -Raw
if (-not $text.Contains("UEGT2_REST_SMOKE_COMPLETE run=$runId ") -or
    $text -match 'UEGT2_REST_SMOKE_FAILED|Critical error|Assertion failed|invalid ShaderMap') {
    throw "Rest smoke failed or did not complete. Log: $log"
}
# Keep unexpected artifacts for diagnosis; this runner never removes player data.
$saveFiles = @(Get-ChildItem -LiteralPath (Join-Path $userDir 'Saved\SaveGames') -Filter '*.sav' -File -Recurse -ErrorAction SilentlyContinue)
if ($saveFiles.Count -gt 0) { throw "Rest smoke unexpectedly wrote a checkpoint under $userDir." }
if ($Capture) {
    foreach ($name in @('01_RestPanel.png', '02_Awake.png')) {
        $shot = Join-Path $captureDir $name
        if (-not (Test-Path -LiteralPath $shot -PathType Leaf) -or (Get-Item -LiteralPath $shot).Length -eq 0) {
            throw "Rest smoke did not produce $shot."
        }
    }
    Write-Host "Rest screenshots: $captureDir"
}
Write-Host "Rest smoke passed: bed probe, cancel, whole-day sleep, live wake and both off switches. Log: $log" -ForegroundColor Green
