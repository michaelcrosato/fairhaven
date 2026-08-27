<#
.SYNOPSIS
Runs Fairhaven headlessly and captures a PNG at each tour viewpoint.

.DESCRIPTION
This is the visual regression check for the world. Screenshots land in
Saved/Screenshots/Tour by default. The viewpoint list lives in
Source/UEGT2/Private/Diagnostics/UEGT2CaptureSubsystem.cpp.

.EXAMPLE
./Scripts/Screenshot-Tour.ps1
./Scripts/Screenshot-Tour.ps1 -Only TownSquare -ResX 2560 -ResY 1440
./Scripts/Screenshot-Tour.ps1 -Menu -OutputDirectory Saved/Screenshots/Menu
#>
[CmdletBinding()]
param(
    [string] $OutputDirectory,
    [string] $Only,
    [int] $ResX = 1920,
    [int] $ResY = 1080,
    [double] $Delay = 7.0,
    [double] $Hold = 1.8,
    [int] $TimeoutMinutes = 20,
    [switch] $Menu,
    [string] $EngineRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$projectRoot = Split-Path -Parent $PSScriptRoot
$projectFile = Join-Path $projectRoot 'UEGT2.uproject'
$engine = & (Join-Path $PSScriptRoot 'Resolve-Engine.ps1') -EngineRoot $EngineRoot

# Use the packaged build. An uncooked game binary cannot start, because the
# global shader library only exists after a cook.
# Prefer the real binary under Binaries\Win64 over the staged launcher stub in
# the archive root: Windows Application Control blocks the stub.
$candidates = @(Get-ChildItem -LiteralPath (Join-Path $projectRoot 'LocalBuilds') -Recurse -File `
    -Filter 'UEGT2.exe' -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending)
if ($candidates.Count -eq 0) {
    throw 'No packaged build found. Run ./Scripts/Package.ps1 first.'
}
$packaged = $candidates | Where-Object { $_.FullName -like '*\Binaries\Win64\UEGT2.exe' } | Select-Object -First 1
if (-not $packaged) { $packaged = $candidates[0] }
$gameExe = $packaged.FullName
Write-Host "Using packaged build: $gameExe"

if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $projectRoot 'Saved\Screenshots\Tour'
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $OutputDirectory) {
    Remove-Item -LiteralPath $OutputDirectory -Recurse -Force
}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

$logDir = Join-Path $projectRoot 'Saved\Logs'
New-Item -ItemType Directory -Path $logDir -Force | Out-Null
$log = Join-Path $logDir 'ScreenshotTour.log'
if (Test-Path -LiteralPath $log) { Remove-Item -LiteralPath $log -Force }

# No .uproject argument: passing one makes a packaged build try to load
# uncooked project content through a Zen storage server and fail to start.
$arguments = @(
    '-RenderOffscreen',
    '-Windowed',
    # -ForceRes is required: without it the offscreen window falls back to a
    # smaller size and the screenshots come out at the wrong resolution.
    '-ForceRes',
    "-ResX=$ResX",
    "-ResY=$ResY",
    '-unattended', '-nosplash', '-nopause', '-nosound',
    "-UEGT2Capture=$($OutputDirectory.Replace('\','/'))",
    "-UEGT2CaptureDelay=$Delay",
    "-UEGT2CaptureHold=$Hold",
    "-abslog=$log"
)
if ($Only) { $arguments += "-UEGT2CaptureOnly=$Only" }
if ($Menu) { $arguments += '-UEGT2CaptureMenu' }

Write-Host "Capturing tour at ${ResX}x${ResY} -> $OutputDirectory"
$process = Start-Process -FilePath $gameExe -ArgumentList $arguments -PassThru -WindowStyle Hidden `
    -RedirectStandardOutput (Join-Path $logDir 'ScreenshotTour.stdout.log') `
    -RedirectStandardError (Join-Path $logDir 'ScreenshotTour.stderr.log')
if (-not $process.WaitForExit($TimeoutMinutes * 60 * 1000)) {
    Stop-Process -Id $process.Id -Force
    throw "Screenshot tour exceeded $TimeoutMinutes minutes. Inspect $log."
}

if (Test-Path -LiteralPath $log) {
    Select-String -LiteralPath $log -Pattern 'LogUEGT2Diag|LogUEGT2:|Error:' |
        Select-Object -Last 30 |
        ForEach-Object { Write-Host ("  " + ($_.Line -replace '^\[[^\]]+\]\[\s*\d+\]', '')) }
}

$shots = @(Get-ChildItem -LiteralPath $OutputDirectory -Filter '*.png' -File -ErrorAction SilentlyContinue)
if ($shots.Count -eq 0) {
    throw "No screenshots were produced. Inspect $log."
}
Write-Host "Captured $($shots.Count) screenshots in $OutputDirectory"
$shots | ForEach-Object { Write-Host ("  {0}  ({1:N0} KB)" -f $_.Name, ($_.Length / 1KB)) }
