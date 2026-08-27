<#
.SYNOPSIS
Resolves the Unreal Engine installation this project builds against.
Writes the engine root to stdout. Override with -EngineRoot or UEGT2_ENGINE_ROOT.
#>
[CmdletBinding()]
param([string] $EngineRoot)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$projectRoot = Split-Path -Parent $PSScriptRoot
$projectFile = Join-Path $projectRoot 'UEGT2.uproject'
$association = (Get-Content -LiteralPath $projectFile -Raw | ConvertFrom-Json).EngineAssociation
$candidates = [System.Collections.Generic.List[string]]::new()

if ($EngineRoot) { $candidates.Add($EngineRoot) }
if ($env:UEGT2_ENGINE_ROOT) { $candidates.Add($env:UEGT2_ENGINE_ROOT) }

foreach ($registryPath in @(
    "HKLM:\SOFTWARE\EpicGames\Unreal Engine\$association",
    "HKLM:\SOFTWARE\WOW6432Node\EpicGames\Unreal Engine\$association")) {
    $item = Get-ItemProperty -LiteralPath $registryPath -ErrorAction SilentlyContinue
    if ($item -and $item.PSObject.Properties.Name -contains 'InstalledDirectory') {
        $candidates.Add($item.InstalledDirectory)
    }
}

$userBuilds = Get-ItemProperty -LiteralPath 'HKCU:\SOFTWARE\Epic Games\Unreal Engine\Builds' -ErrorAction SilentlyContinue
if ($userBuilds) {
    foreach ($property in $userBuilds.PSObject.Properties) {
        if ($property.Value -is [string] -and $property.Value -match 'UE_' ) { $candidates.Add($property.Value) }
    }
}

foreach ($drive in Get-PSDrive -PSProvider FileSystem) {
    $candidates.Add((Join-Path $drive.Root "Program Files\Epic Games\UE_$association"))
    $candidates.Add((Join-Path $drive.Root "Epic Games\UE_$association"))
}

foreach ($candidate in $candidates | Select-Object -Unique) {
    if (-not $candidate) { continue }
    $resolved = [Environment]::ExpandEnvironmentVariables($candidate).Replace('/', '\').TrimEnd('\')
    if (Test-Path -LiteralPath (Join-Path $resolved 'Engine\Build\BatchFiles\Build.bat') -PathType Leaf) {
        Write-Output $resolved
        exit 0
    }
}

throw "Unreal Engine $association was not found. Install it, pass -EngineRoot, or set UEGT2_ENGINE_ROOT."
