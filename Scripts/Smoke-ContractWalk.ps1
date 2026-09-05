<#
.SYNOPSIS
Walks the entire town survey contract circuit with ordinary mapped input.

.DESCRIPTION
Walks sign -> Harbour -> Light -> Mill -> sign, uses the actual interactions,
and claims payment with live needs and clock. Ordinary movement follows one
explicit route; the native diagnostic allows at most 25 minutes.
No checkpoint slot or capture override is supplied. A fresh isolated user
directory and all logs are retained for diagnosis; any checkpoint file fails.
#>
[CmdletBinding()]
param(
    [ValidateRange(1, 30)]
    [int] $TimeoutMinutes = 30,
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
$userDir = [IO.Path]::GetFullPath((Join-Path $packagedProject "Saved\ContractWalkSmoke\$runId"))
$ownedPrefix = [IO.Path]::GetFullPath((Join-Path $packagedProject 'Saved\ContractWalkSmoke')).TrimEnd('\') + '\'
if ($runId -notmatch '^[0-9a-f]{32}$' -or
    -not $userDir.StartsWith($ownedPrefix, [StringComparison]::OrdinalIgnoreCase) -or
    (Split-Path -Leaf $userDir) -ne $runId) {
    throw 'Contract walk user directory escaped the packaged build.'
}
$logDir = Join-Path $projectRoot 'Saved\Logs'
$log = Join-Path $logDir "ContractWalkSmoke-$runId.log"
$stdout = Join-Path $logDir "ContractWalkSmoke-$runId.stdout.log"
$stderr = Join-Path $logDir "ContractWalkSmoke-$runId.stderr.log"
foreach ($path in @($userDir, $log, $stdout, $stderr)) {
    if (Test-Path -LiteralPath $path) { throw "Contract walk requires fresh evidence paths: $path" }
}
New-Item -ItemType Directory -Path $userDir, $logDir -Force | Out-Null
$arguments = @(
    '-RenderOffscreen', '-Windowed', '-ForceRes', "-ResX=$Width", "-ResY=$Height",
    '-unattended', '-nosplash', '-nopause', '-nosound', '-UEGT2SkipMenu', '-UEGT2ContractWalkSmoke',
    "-UserDir=`"$($userDir.Replace('\', '/'))`"", "-abslog=`"$log`""
)

Write-Host "Contract walk: $($exe.FullName) at ${Width}x$Height; run=$runId"
$process = Start-Process -FilePath $exe.FullName -ArgumentList $arguments -PassThru -WindowStyle Hidden `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr
# Cache the handle before a redirected wait so Windows PowerShell retains exit.
$null = $process.Handle
try {
    if (-not $process.WaitForExit($TimeoutMinutes * 60 * 1000)) {
        throw "Contract walk exceeded $TimeoutMinutes minutes. Log: $log"
    }
    if ($process.ExitCode -ne 0) { throw "Contract walk exited with code $($process.ExitCode). Log: $log" }
} finally {
    if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force; $null = $process.WaitForExit(10000) }
    $process.Dispose()
}
if (-not (Test-Path -LiteralPath $log -PathType Leaf)) { throw "Contract walk produced no log at $log." }
$text = Get-Content -LiteralPath $log -Raw
if (-not $text.Contains("UEGT2_CONTRACT_WALK_SMOKE_COMPLETE run=$runId ") -or
    $text -match 'UEGT2_CONTRACT_WALK_SMOKE_FAILED|Critical error|Assertion failed|invalid ShaderMap') {
    throw "Contract walk failed or did not complete. Log: $log"
}
# Scan only this run's complete user directory: an incorrectly placed checkpoint
# is still forbidden. Retain unexpected files as evidence, never remove saves.
$saveFiles = @(Get-ChildItem -LiteralPath $userDir -Filter '*.sav' -File -Recurse)
if ($saveFiles.Count -gt 0) { throw "Contract walk unexpectedly wrote a checkpoint under $userDir." }
Write-Host "Contract walk passed: complete ordinary walking circuit, real interactions, live ledger and claim. Log: $log" -ForegroundColor Green
