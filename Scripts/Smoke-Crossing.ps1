<#
.SYNOPSIS
Walks the packaged lower river bridge in both directions with the normal capsule.

.DESCRIPTION
Uses real mapped forward input, one setup teleport and ordinary collision.
The runtime checks support on both ramps and the deck, bounded progress and dry
terrain at both ends. Dedicated screenshot-tour viewpoints cover appearance.
All user data is isolated and retained for diagnosis; checkpoint IO is disabled.
#>
[CmdletBinding()]
param(
    [ValidateRange(1, 1440)]
    [int] $TimeoutMinutes = 4,
    [ValidateRange(1280, 7680)]
    [int] $Width = 1920,
    [ValidateRange(720, 4320)]
    [int] $Height = 1080
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
$userDir = [IO.Path]::GetFullPath((Join-Path $packagedProject "Saved\CrossingSmoke\$runId"))
$ownedPrefix = [IO.Path]::GetFullPath((Join-Path $packagedProject 'Saved\CrossingSmoke')).TrimEnd('\') + '\'
if (-not $userDir.StartsWith($ownedPrefix, [StringComparison]::OrdinalIgnoreCase) -or (Split-Path -Leaf $userDir) -ne $runId) {
    throw 'Crossing smoke user directory escaped the packaged build.'
}
$logDir = Join-Path $projectRoot 'Saved\Logs'
$log = Join-Path $logDir "CrossingSmoke-$runId.log"
New-Item -ItemType Directory -Path $userDir, $logDir -Force | Out-Null
$arguments = @(
    '-RenderOffscreen', '-Windowed', '-ForceRes', "-ResX=$Width", "-ResY=$Height",
    '-unattended', '-nosplash', '-nopause', '-nosound', '-UEGT2SkipMenu', '-UEGT2CrossingSmoke',
    "-UserDir=`"$($userDir.Replace('\', '/'))`"", "-abslog=`"$log`""
)

Write-Host "Crossing smoke: $($exe.FullName) at ${Width}x$Height"
$process = Start-Process -FilePath $exe.FullName -ArgumentList $arguments -PassThru -WindowStyle Hidden `
    -RedirectStandardOutput (Join-Path $logDir "CrossingSmoke-$runId.stdout.log") `
    -RedirectStandardError (Join-Path $logDir "CrossingSmoke-$runId.stderr.log")
# Retain the handle before a redirected wait so Windows PowerShell reports exit.
$null = $process.Handle
try {
    if (-not $process.WaitForExit($TimeoutMinutes * 60 * 1000)) {
        throw "Crossing smoke exceeded $TimeoutMinutes minutes. Log: $log"
    }
    if ($process.ExitCode -ne 0) { throw "Crossing smoke exited with code $($process.ExitCode). Log: $log" }
} finally {
    if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force; $null = $process.WaitForExit(10000) }
    $process.Dispose()
}
if (-not (Test-Path -LiteralPath $log -PathType Leaf)) { throw "Crossing smoke produced no log at $log." }
$text = Get-Content -LiteralPath $log -Raw
if (-not $text.Contains("UEGT2_CROSSING_SMOKE_COMPLETE run=$runId ") -or
    $text -match 'UEGT2_CROSSING_SMOKE_FAILED|Critical error|Assertion failed|invalid ShaderMap') {
    throw "Crossing smoke failed or did not complete. Log: $log"
}
$saveFiles = @(Get-ChildItem -LiteralPath (Join-Path $userDir 'Saved\SaveGames') -Filter '*.sav' -File -Recurse -ErrorAction SilentlyContinue)
if ($saveFiles.Count -gt 0) { throw "Crossing smoke unexpectedly wrote a checkpoint under $userDir." }
Write-Host "Crossing smoke passed: both directions, ordinary capsule, bridge support and dry endpoints. Log: $log" -ForegroundColor Green
