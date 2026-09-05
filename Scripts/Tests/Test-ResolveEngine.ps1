<#
.SYNOPSIS
Checks engine discovery with isolated registry/drive fixtures and real file markers.
No Unreal processes are launched and no registry values are changed.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$sourceScripts = Split-Path -Parent $PSScriptRoot
$tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$fixtureRoot = Join-Path $tempRoot ('UEGT2-EngineTests-' + [guid]::NewGuid().ToString('N'))
$savedEnvironment = @{}
foreach ($name in @('ProgramData', 'UEGT2_ENGINE_ROOT', 'UEGT2_TEST_ENGINE_ROOT')) {
    $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
$engineFixture = @{ Count = 0 }
$userKey = 'HKCU:\SOFTWARE\Epic Games\Unreal Engine\Builds'
$machineKey = 'HKLM:\SOFTWARE\EpicGames\Unreal Engine\5.8'
$legacyKey = 'HKLM:\SOFTWARE\WOW6432Node\EpicGames\Unreal Engine\5.8'

function Assert-True([bool] $Condition, [string] $Message) {
    if (-not $Condition) { throw $Message }
}

# Mock only discovery sources. Path normalization, JSON parsing, Build.bat and
# Build.version validation all run against real files under the fixture root.
function Get-ItemProperty {
    param([string] $LiteralPath, [string[]] $Name, [string] $ErrorAction)
    $engineFixture.RegistryReads.Add(@{ Path = $LiteralPath; Names = $Name })
    if (-not $engineFixture.Registry.ContainsKey($LiteralPath)) { return $null }
    $properties = @{}
    foreach ($entry in $engineFixture.Registry[$LiteralPath].GetEnumerator()) {
        if (-not $Name -or $Name -contains $entry.Key) { $properties[$entry.Key] = $entry.Value }
    }
    if ($properties.Count -eq 0) { return $null }
    return [pscustomobject] $properties
}

function Get-PSDrive {
    param([string] $PSProvider)
    Assert-True ($PSProvider -eq 'FileSystem') 'Only filesystem drives should be searched.'
    $engineFixture.DriveReads++
    return $engineFixture.Drives
}

function New-EngineMarker {
    param([string] $Name, [string] $Version = '5.8.2', [switch] $NoBuildScript)
    $path = Join-Path $engineFixture.CaseRoot $Name
    $buildDir = Join-Path $path 'Engine\Build'
    New-Item -ItemType Directory -Path (Join-Path $buildDir 'BatchFiles') -Force | Out-Null
    if (-not $NoBuildScript) {
        Set-Content -LiteralPath (Join-Path $buildDir 'BatchFiles\Build.bat') -Value '@exit /b 99'
    }
    if ($Version -eq 'malformed') {
        Set-Content -LiteralPath (Join-Path $buildDir 'Build.version') -Value '{ invalid json'
    } elseif ($Version) {
        $parsed = [version] $Version
        @{ MajorVersion = $parsed.Major; MinorVersion = $parsed.Minor; PatchVersion = $parsed.Build } |
            ConvertTo-Json | Set-Content -LiteralPath (Join-Path $buildDir 'Build.version')
    }
    return $path
}

function Write-LauncherFixture([object[]] $Installations) {
    $launcherDir = Join-Path $env:ProgramData 'Epic\UnrealEngineLauncher'
    New-Item -ItemType Directory -Path $launcherDir -Force | Out-Null
    @{ InstallationList = $Installations } | ConvertTo-Json -Depth 6 |
        Set-Content -LiteralPath (Join-Path $launcherDir 'LauncherInstalled.dat')
}

function Invoke-EngineCase([string] $Name, [scriptblock] $Arrange) {
    $engineFixture.Count++
    $engineFixture.CaseRoot = Join-Path $fixtureRoot ('case {0:00}' -f $engineFixture.Count)
    $scripts = Join-Path $engineFixture.CaseRoot 'Scripts'
    New-Item -ItemType Directory -Path $scripts -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $sourceScripts 'Resolve-Engine.ps1') -Destination $scripts
    $env:ProgramData = Join-Path $engineFixture.CaseRoot 'program data'
    $env:UEGT2_ENGINE_ROOT = $null
    $env:UEGT2_TEST_ENGINE_ROOT = $null
    $engineFixture.Registry = @{}
    $engineFixture.RegistryReads = [System.Collections.Generic.List[object]]::new()
    $engineFixture.Drives = @()
    $engineFixture.DriveReads = 0
    $engineFixture.Arguments = @{}
    $engineFixture.Association = '5.8'
    $engineFixture.Expected = $null
    $engineFixture.Failure = ''
    & $Arrange
    @{ EngineAssociation = $engineFixture.Association } | ConvertTo-Json |
        Set-Content -LiteralPath (Join-Path $engineFixture.CaseRoot 'UEGT2.uproject')
    $failure = $null
    $actual = @()
    Push-Location -LiteralPath $engineFixture.CaseRoot
    try {
        $arguments = $engineFixture.Arguments
        $actual = @(& (Join-Path $scripts 'Resolve-Engine.ps1') @arguments)
    } catch {
        $failure = $_.Exception.Message
    } finally {
        Pop-Location
    }
    if ($engineFixture.Failure) {
        Assert-True ($null -ne $failure -and $failure -match $engineFixture.Failure) "${Name}: expected '$($engineFixture.Failure)', got '$failure'."
        Assert-True ($actual.Count -eq 0) "${Name}: a failed resolution emitted an engine root."
    } else {
        Assert-True ($null -eq $failure) "${Name}: unexpected failure '$failure'."
        Assert-True ($actual.Count -eq 1) "${Name}: expected exactly one output path."
        Assert-True ($actual[0].TrimEnd('\', '/') -eq $engineFixture.Expected.TrimEnd('\', '/')) "${Name}: selected the wrong engine."
    }
    if ($engineFixture.Arguments.ContainsKey('EngineRoot') -or $env:UEGT2_ENGINE_ROOT) {
        Assert-True ($engineFixture.RegistryReads.Count -eq 0 -and $engineFixture.DriveReads -eq 0) "${Name}: an explicit override unexpectedly searched other installations."
    } else {
        $userReads = @($engineFixture.RegistryReads | Where-Object { $_.Path -eq $userKey })
        foreach ($read in $userReads) {
            Assert-True (@($read.Names).Count -eq 1 -and $read.Names[0] -eq $engineFixture.Association) "${Name}: user registry lookup was not restricted to the project association."
        }
    }
    Write-Host "PASS $Name"
}

try {
    Invoke-EngineCase 'argument override wins and permits another engine version' {
        $engineFixture.Expected = New-EngineMarker 'explicit engine' -Version '5.9.0'
        $engineFixture.Arguments.EngineRoot = $engineFixture.Expected
        $env:UEGT2_ENGINE_ROOT = New-EngineMarker 'environment engine'
    }
    Invoke-EngineCase 'invalid argument cannot silently fall back to environment' {
        $engineFixture.Arguments.EngineRoot = Join-Path $engineFixture.CaseRoot 'missing'
        $env:UEGT2_ENGINE_ROOT = New-EngineMarker 'environment engine'
        $engineFixture.Failure = 'override is invalid'
    }
    Invoke-EngineCase 'environment override needs no release metadata' {
        $engineFixture.Expected = New-EngineMarker 'source override' -Version ''
        $env:UEGT2_ENGINE_ROOT = $engineFixture.Expected
        $engineFixture.Association = ''
    }
    Invoke-EngineCase 'invalid environment cannot silently fall back to launcher' {
        $env:UEGT2_ENGINE_ROOT = Join-Path $engineFixture.CaseRoot 'missing'
        Write-LauncherFixture @(@{ AppName = 'UE_5.8'; InstallLocation = (New-EngineMarker 'launcher engine') })
        $engineFixture.Failure = 'override is invalid'
    }
    Invoke-EngineCase 'override expands environment variables and trailing separators' {
        $engineFixture.Expected = New-EngineMarker 'expanded engine'
        $env:UEGT2_TEST_ENGINE_ROOT = $engineFixture.Expected
        $engineFixture.Arguments.EngineRoot = '%UEGT2_TEST_ENGINE_ROOT%/'
    }
    Invoke-EngineCase 'relative override follows PowerShell location' {
        $engineFixture.Expected = New-EngineMarker 'relative engine'
        $engineFixture.Arguments.EngineRoot = './relative engine'
    }
    Invoke-EngineCase 'GUID association resolves an arbitrary source directory name' {
        $engineFixture.Association = '{13BC8BA2-5F64-4616-A5E3-4A88E8C793FA}'
        $engineFixture.Expected = New-EngineMarker 'source checkout' -Version ''
        $engineFixture.Registry[$userKey] = @{ $engineFixture.Association = $engineFixture.Expected }
    }
    Invoke-EngineCase 'custom association alias resolves by exact key' {
        $engineFixture.Association = 'FairhavenSource'
        $engineFixture.Expected = New-EngineMarker 'custom checkout' -Version ''
        $engineFixture.Registry[$userKey] = @{ FairhavenSource = $engineFixture.Expected }
    }
    Invoke-EngineCase 'unregistered GUID rejects unrelated UE-named installations' {
        $engineFixture.Association = '{13BC8BA2-5F64-4616-A5E3-4A88E8C793FA}'
        $engineFixture.Registry[$userKey] = @{ '{99999999-9999-9999-9999-999999999999}' = (New-EngineMarker 'UE_5.8') }
        $engineFixture.Failure = 'was not found'
    }
    Invoke-EngineCase 'exact numeric user association has precedence over launcher' {
        $engineFixture.Expected = New-EngineMarker 'registered source'
        $engineFixture.Registry[$userKey] = @{ '5.8' = $engineFixture.Expected }
        Write-LauncherFixture @(@{ AppName = 'UE_5.8'; InstallLocation = (New-EngineMarker 'launcher engine') })
    }
    Invoke-EngineCase 'launcher discovers a nonstandard path and ignores unrelated user builds' {
        $engineFixture.Expected = New-EngineMarker 'custom install directory'
        $engineFixture.Registry[$userKey] = @{ '{99999999-9999-9999-9999-999999999999}' = (New-EngineMarker 'UE_5.7' -Version '5.7.0') }
        Write-LauncherFixture @(@{ AppName = 'UE_5.8'; InstallLocation = $engineFixture.Expected })
    }
    Invoke-EngineCase 'launcher AppName must match the association' {
        Write-LauncherFixture @(@{ AppName = 'UE_5.7'; InstallLocation = (New-EngineMarker 'other release') })
        $engineFixture.Failure = 'was not found'
    }
    Invoke-EngineCase 'stale launcher falls back to native registry before WOW6432Node' {
        $engineFixture.Expected = New-EngineMarker 'native registration'
        Write-LauncherFixture @(@{ AppName = 'UE_5.8'; InstallLocation = (Join-Path $engineFixture.CaseRoot 'missing') })
        $engineFixture.Registry[$machineKey] = @{ InstalledDirectory = $engineFixture.Expected }
        $engineFixture.Registry[$legacyKey] = @{ InstalledDirectory = (New-EngineMarker 'legacy registration') }
    }
    Invoke-EngineCase 'wrong version registration falls back to a compatible engine' {
        $engineFixture.Expected = New-EngineMarker 'compatible registration'
        $engineFixture.Registry[$machineKey] = @{ InstalledDirectory = (New-EngineMarker 'wrong version' -Version '5.7.9') }
        $engineFixture.Registry[$legacyKey] = @{ InstalledDirectory = $engineFixture.Expected }
    }
    Invoke-EngineCase 'missing and malformed version files are rejected automatically' {
        $engineFixture.Expected = New-EngineMarker 'valid release'
        $engineFixture.Registry[$userKey] = @{ '5.8' = (New-EngineMarker 'missing version' -Version '') }
        Write-LauncherFixture @(@{ AppName = 'UE_5.8'; InstallLocation = (New-EngineMarker 'invalid version' -Version 'malformed') })
        $engineFixture.Registry[$machineKey] = @{ InstalledDirectory = $engineFixture.Expected }
    }
    Invoke-EngineCase 'release patch updates remain compatible' {
        $engineFixture.Expected = New-EngineMarker 'patched release' -Version '5.8.42'
        Write-LauncherFixture @(@{ AppName = 'UE_5.8'; InstallLocation = $engineFixture.Expected })
    }
    Invoke-EngineCase 'version metadata without the build script is insufficient' {
        Write-LauncherFixture @(@{ AppName = 'UE_5.8'; InstallLocation = (New-EngineMarker 'incomplete engine' -NoBuildScript) })
        $engineFixture.Failure = 'was not found'
    }
    Invoke-EngineCase 'malformed launcher JSON permits registry fallback' {
        $engineFixture.Expected = New-EngineMarker 'registered release'
        Write-LauncherFixture @()
        Set-Content -LiteralPath (Join-Path $env:ProgramData 'Epic\UnrealEngineLauncher\LauncherInstalled.dat') -Value 'not json'
        $engineFixture.Registry[$machineKey] = @{ InstalledDirectory = $engineFixture.Expected }
    }
    Invoke-EngineCase 'malformed launcher rows do not hide a valid entry' {
        $engineFixture.Expected = New-EngineMarker 'launcher release'
        Write-LauncherFixture @($null, @{ InstallLocation = 'incomplete' }, @{ AppName = 'UE_5.8'; InstallLocation = $engineFixture.Expected })
    }
    Invoke-EngineCase 'duplicate launcher entries are ordered independently of manifest order' {
        $engineFixture.Expected = New-EngineMarker 'A launcher release'
        Write-LauncherFixture @(@{ AppName = 'UE_5.8'; InstallLocation = (New-EngineMarker 'Z launcher release') }, @{ AppName = 'UE_5.8'; InstallLocation = $engineFixture.Expected })
    }
    Invoke-EngineCase 'conventional drive fallback has deterministic order' {
        $engineFixture.Expected = New-EngineMarker 'drive A\Program Files\Epic Games\UE_5.8'
        $null = New-EngineMarker 'drive Z\Program Files\Epic Games\UE_5.8'
        $engineFixture.Drives = @(
            [pscustomobject]@{ Name = 'Z'; Root = (Join-Path $engineFixture.CaseRoot 'drive Z') },
            [pscustomobject]@{ Name = 'A'; Root = (Join-Path $engineFixture.CaseRoot 'drive A') }
        )
    }
    Invoke-EngineCase 'empty association requires an explicit override' {
        $engineFixture.Association = ''
        $engineFixture.Failure = 'no EngineAssociation'
    }
    Write-Host "Engine discovery: $($engineFixture.Count) cases passed."
} finally {
    foreach ($entry in $savedEnvironment.GetEnumerator()) {
        [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value, 'Process')
    }
    $resolvedFixture = [IO.Path]::GetFullPath($fixtureRoot)
    $tempPrefix = $tempRoot.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if (-not $resolvedFixture.StartsWith($tempPrefix, [StringComparison]::OrdinalIgnoreCase) -or
        (Split-Path -Leaf $resolvedFixture) -notmatch '^UEGT2-EngineTests-[0-9a-f]{32}$') {
        throw "Refusing to remove unexpected fixture path: $resolvedFixture"
    }
    if (Test-Path -LiteralPath $resolvedFixture) { Remove-Item -LiteralPath $resolvedFixture -Recurse -Force }
}
