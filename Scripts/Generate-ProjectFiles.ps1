[CmdletBinding()]
param([string] $EngineRoot)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$projectRoot = Split-Path -Parent $PSScriptRoot
$projectFile = Join-Path $projectRoot 'UEGT2.uproject'
$engine = & (Join-Path $PSScriptRoot 'Resolve-Engine.ps1') -EngineRoot $EngineRoot

& (Join-Path $engine 'Engine\Build\BatchFiles\Build.bat') -ProjectFiles "-Project=$projectFile" -Game -Engine -Progress
if ($LASTEXITCODE -ne 0) { throw "Project file generation failed with exit code $LASTEXITCODE." }
Write-Host "Generated project files for $projectFile"
