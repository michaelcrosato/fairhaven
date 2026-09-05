<#
.SYNOPSIS
Resolves the Unreal Engine installation this project builds against.
Writes the engine root to stdout. Override with -EngineRoot or UEGT2_ENGINE_ROOT.
Explicit overrides may select a different engine version. Automatic discovery
matches the project's association and verifies release major/minor versions.
#>
[CmdletBinding()]
param([string] $EngineRoot)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Resolve-EngineCandidate {
    param([string] $Path, [version] $RequiredVersion)

    if ([string]::IsNullOrWhiteSpace($Path)) { return $null }
    try {
        $expanded = [Environment]::ExpandEnvironmentVariables($Path).Replace('/', '\')
        # Resolve relative paths against PowerShell's location, not the process
        # working directory. Keep trailing separators on drive and UNC roots.
        $resolved = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($expanded)
        if (-not (Test-Path -LiteralPath (Join-Path $resolved 'Engine\Build\BatchFiles\Build.bat') -PathType Leaf)) {
            return $null
        }
        if ($null -ne $RequiredVersion) {
            $versionFile = Join-Path $resolved 'Engine\Build\Build.version'
            $version = Get-Content -LiteralPath $versionFile -Raw | ConvertFrom-Json
            if ($version.MajorVersion -ne $RequiredVersion.Major -or
                $version.MinorVersion -ne $RequiredVersion.Minor) {
                Write-Verbose "Skipping an engine whose version does not match $RequiredVersion."
                return $null
            }
        }
        return $resolved
    } catch {
        Write-Verbose "Skipping an invalid engine candidate: $($_.Exception.Message)"
        return $null
    }
}

$override = if (-not [string]::IsNullOrWhiteSpace($EngineRoot)) { $EngineRoot } else { $env:UEGT2_ENGINE_ROOT }
if (-not [string]::IsNullOrWhiteSpace($override)) {
    $resolved = Resolve-EngineCandidate -Path $override
    if (-not $resolved) {
        throw 'The engine override is invalid: expected Engine\Build\BatchFiles\Build.bat. Check -EngineRoot or UEGT2_ENGINE_ROOT.'
    }
    Write-Output $resolved
    return
}

$projectRoot = Split-Path -Parent $PSScriptRoot
$projectFile = Join-Path $projectRoot 'UEGT2.uproject'
$association = (Get-Content -LiteralPath $projectFile -Raw | ConvertFrom-Json).EngineAssociation
if ([string]::IsNullOrWhiteSpace($association)) {
    throw 'The project has no EngineAssociation. Pass -EngineRoot or set UEGT2_ENGINE_ROOT.'
}
$releaseVersion = if ($association -match '^\d+\.\d+$') { [version] $association } else { $null }
$candidates = [System.Collections.Generic.List[string]]::new()

# A source installation is registered under its association identifier (usually
# a GUID). The directory's name says nothing about which project it belongs to.
$userBuilds = Get-ItemProperty -LiteralPath 'HKCU:\SOFTWARE\Epic Games\Unreal Engine\Builds' `
    -Name $association -ErrorAction SilentlyContinue
if ($userBuilds) {
    $property = $userBuilds.PSObject.Properties[$association]
    if ($property -and $property.Value -is [string]) { $candidates.Add($property.Value) }
}

# Launcher installations may live outside Program Files and need not have HKLM
# registrations. This is the same exact AppName mapping DesktopPlatform uses.
if ($env:ProgramData) {
    $launcherFile = Join-Path $env:ProgramData 'Epic\UnrealEngineLauncher\LauncherInstalled.dat'
    if (Test-Path -LiteralPath $launcherFile -PathType Leaf) {
        try {
            $launcher = Get-Content -LiteralPath $launcherFile -Raw | ConvertFrom-Json
            $matching = @($launcher.InstallationList | Where-Object {
                $null -ne $_ -and $_.PSObject.Properties['AppName'] -and
                $_.AppName -eq "UE_$association" -and $_.PSObject.Properties['InstallLocation'] -and
                $_.InstallLocation -is [string]
            } | Sort-Object InstallLocation)
            foreach ($installation in $matching) { $candidates.Add($installation.InstallLocation) }
        } catch {
            Write-Verbose "Ignoring an unreadable launcher installation list: $($_.Exception.Message)"
        }
    }
}

# Only release identifiers imply versioned registry keys and directory names;
# an unregistered GUID must not silently fall back to another installed engine.
if ($null -ne $releaseVersion) {
    foreach ($registryPath in @(
        "HKLM:\SOFTWARE\EpicGames\Unreal Engine\$association",
        "HKLM:\SOFTWARE\WOW6432Node\EpicGames\Unreal Engine\$association")) {
        $item = Get-ItemProperty -LiteralPath $registryPath -Name InstalledDirectory -ErrorAction SilentlyContinue
        if ($item -and $item.PSObject.Properties['InstalledDirectory'] -and $item.InstalledDirectory -is [string]) {
            $candidates.Add($item.InstalledDirectory)
        }
    }
    foreach ($drive in Get-PSDrive -PSProvider FileSystem | Sort-Object Name) {
        $candidates.Add((Join-Path $drive.Root "Program Files\Epic Games\UE_$association"))
        $candidates.Add((Join-Path $drive.Root "Epic Games\UE_$association"))
    }
}

foreach ($candidate in $candidates | Select-Object -Unique) {
    $resolved = Resolve-EngineCandidate -Path $candidate -RequiredVersion $releaseVersion
    if ($resolved) {
        Write-Output $resolved
        return
    }
}

throw "Unreal Engine $association was not found. Install it, pass -EngineRoot, or set UEGT2_ENGINE_ROOT."
