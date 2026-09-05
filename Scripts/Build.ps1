<#
.SYNOPSIS
Builds a UEGT2 target with UnrealBuildTool.
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'DebugGame', 'Development', 'Shipping', 'Test')]
    [string] $Configuration = 'Development',
    [ValidateSet('Editor', 'Game', 'Both')]
    [string] $Target = 'Editor',
    [string] $EngineRoot,
    [switch] $Clean,
    [switch] $DisableAdaptiveUnity
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$projectRoot = Split-Path -Parent $PSScriptRoot
$projectFile = Join-Path $projectRoot 'UEGT2.uproject'
$engine = & (Join-Path $PSScriptRoot 'Resolve-Engine.ps1') -EngineRoot $EngineRoot
# 'Both' matters: the Game target catches editor-only APIs that the editor
# build happily accepts, which is a very easy mistake to make in this codebase.
$targetNames = switch ($Target) {
    'Editor' { @('UEGT2Editor') }
    'Game'   { @('UEGT2') }
    'Both'   { @('UEGT2Editor', 'UEGT2') }
}

foreach ($targetName in $targetNames) {
$arguments = @($targetName, 'Win64', $Configuration, $projectFile, '-WaitMutex', '-NoHotReloadFromIDE')
if ($DisableAdaptiveUnity) { $arguments += '-DisableAdaptiveUnity' }

if ($Clean) {
    Write-Host "Cleaning $targetName Win64 $Configuration"
    & (Join-Path $engine 'Engine\Build\BatchFiles\Clean.bat') @arguments
    if ($LASTEXITCODE -ne 0) { throw "Clean failed with exit code $LASTEXITCODE." }
}

Write-Host "Building $targetName Win64 $Configuration using $engine"
& (Join-Path $engine 'Engine\Build\BatchFiles\Build.bat') @arguments
if ($LASTEXITCODE -ne 0) { throw "Build failed ($targetName) with exit code $LASTEXITCODE." }
Write-Host "Build succeeded: $targetName Win64 $Configuration"
}
