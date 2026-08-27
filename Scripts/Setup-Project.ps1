<#
.SYNOPSIS
One command that takes a fresh clone to a playable build.

.DESCRIPTION
Runs the whole pipeline in order:
  1. verify the toolchain
  2. generate the terrain and audio (plain Python, no Unreal)
  3. build the editor and game targets
  4. build all content inside Unreal
  5. run the automation tests
  6. package a playable build

Expect roughly 10-20 minutes on a first run, most of it shader compilation.

.EXAMPLE
./Scripts/Setup-Project.ps1
./Scripts/Setup-Project.ps1 -SkipPackage
#>
[CmdletBinding()]
param(
    [string] $EngineRoot,
    [switch] $SkipTerrain,
    [switch] $SkipTests,
    [switch] $SkipPackage
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = $PSScriptRoot
$projectRoot = Split-Path -Parent $root
$started = Get-Date

function Step([string] $Message) {
    Write-Host ''
    Write-Host "=== $Message ===" -ForegroundColor Cyan
}

# --- 1. Toolchain ----------------------------------------------------------
Step 'Verifying toolchain'
$engine = & (Join-Path $root 'Resolve-Engine.ps1') -EngineRoot $EngineRoot
Write-Host "Unreal Engine: $engine"

$python = Get-Command python -ErrorAction SilentlyContinue
if (-not $python) { throw 'python not found on PATH. Python 3.10+ with numpy and Pillow is required.' }
Write-Host "Python: $($python.Source)"
& $python.Source -c "import numpy, PIL; print('numpy', numpy.__version__, '| Pillow', PIL.__version__)"
if ($LASTEXITCODE -ne 0) { throw 'numpy and Pillow are required: pip install numpy pillow' }

# --- 2. Offline generation -------------------------------------------------
if (-not $SkipTerrain) {
    Step 'Generating terrain (offline)'
    & $python.Source (Join-Path $projectRoot 'Tools\Terrain\generate_terrain.py')
    if ($LASTEXITCODE -ne 0) { throw 'Terrain generation failed.' }

    Step 'Generating audio (offline)'
    & $python.Source (Join-Path $projectRoot 'Tools\Audio\generate_audio.py')
    if ($LASTEXITCODE -ne 0) { throw 'Audio generation failed.' }
} else {
    Write-Host 'Skipping terrain and audio generation.'
}

# --- 3. Code ---------------------------------------------------------------
Step 'Building C++ (editor and game)'
& (Join-Path $root 'Build.ps1') -Target Both -Configuration Development -EngineRoot $engine

# --- 4. Content ------------------------------------------------------------
Step 'Building content in Unreal'
& (Join-Path $root 'Build-Content.ps1') -Stages all -EngineRoot $engine

# --- 5. Tests --------------------------------------------------------------
if (-not $SkipTests) {
    Step 'Running automation tests'
    & (Join-Path $root 'Test.ps1') -SkipBuild -EngineRoot $engine
} else {
    Write-Host 'Skipping tests.'
}

# --- 6. Package ------------------------------------------------------------
if (-not $SkipPackage) {
    Step 'Packaging a playable build'
    & (Join-Path $root 'Package.ps1') -Configuration Development -EngineRoot $engine
} else {
    Write-Host 'Skipping packaging.'
}

$elapsed = (Get-Date) - $started
Write-Host ''
Write-Host ("Setup complete in {0:hh\:mm\:ss}." -f $elapsed) -ForegroundColor Green
if (-not $SkipPackage) {
    $exe = Get-ChildItem -LiteralPath (Join-Path $projectRoot 'LocalBuilds') -Recurse -File `
        -Filter 'UEGT2.exe' -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -like '*\Binaries\Win64\UEGT2.exe' } | Select-Object -First 1
    if ($exe) { Write-Host "Play it: $($exe.FullName)" -ForegroundColor Green }
}
