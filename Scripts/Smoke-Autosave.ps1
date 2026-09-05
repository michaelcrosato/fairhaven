<#
.SYNOPSIS
Checks periodic packaged autosaves, independent manual files and cross-process load.

.DESCRIPTION
Uses a unique progress slot and isolated user directory across Write, Read and
Disabled processes. The two-second interval is accepted only for validated smoke
arguments. -Capture records the asynchronous Main row and gameplay settings.
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
$slot = 'UEGT2_ProgressSmoke_' + $runId
$userDir = [IO.Path]::GetFullPath((Join-Path $packagedProject "Saved\AutosaveSmoke\$runId"))
$ownedPrefix = [IO.Path]::GetFullPath((Join-Path $packagedProject 'Saved\AutosaveSmoke')).TrimEnd('\') + '\'
if (-not $userDir.StartsWith($ownedPrefix, [StringComparison]::OrdinalIgnoreCase) -or (Split-Path -Leaf $userDir) -ne $runId) {
    throw 'Autosave smoke user directory escaped the packaged build.'
}
$saveDir = Join-Path $userDir 'Saved\SaveGames'
$logDir = Join-Path $projectRoot 'Saved\Logs'
$captureDir = Join-Path $projectRoot "Saved\Screenshots\AutosaveSmoke\$runId"
New-Item -ItemType Directory -Path $userDir, $logDir -Force | Out-Null
if ($Capture) { New-Item -ItemType Directory -Path $captureDir -Force | Out-Null }

Write-Host "Autosave smoke: $($exe.FullName) at ${Width}x$Height"
Write-Host "Isolated slot: $slot"
try {
    foreach ($phase in @('Write', 'Read', 'Disabled')) {
        $log = Join-Path $logDir "AutosaveSmoke-$runId-$phase.log"
        $arguments = @(
            '-RenderOffscreen', '-Windowed', '-ForceRes', "-ResX=$Width", "-ResY=$Height",
            '-unattended', '-nosplash', '-nopause', '-nosound', '-UEGT2AutosaveIntervalSeconds=2',
            "-UEGT2AutosaveSmoke=$phase", "-UEGT2ProgressSlot=$slot",
            "-UserDir=`"$($userDir.Replace('\', '/'))`"", "-abslog=`"$log`""
        )
        # Read must exercise the actual initial Main menu and asynchronous row.
        if ($phase -ne 'Read') { $arguments += '-UEGT2SkipMenu' }
        if ($Capture -and $phase -eq 'Read') { $arguments += "-UEGT2AutosaveCapture=`"$($captureDir.Replace('\', '/'))`"" }
        $process = Start-Process -FilePath $exe.FullName -ArgumentList $arguments -PassThru -WindowStyle Hidden `
            -RedirectStandardOutput (Join-Path $logDir "AutosaveSmoke-$runId-$phase.stdout.log") `
            -RedirectStandardError (Join-Path $logDir "AutosaveSmoke-$runId-$phase.stderr.log")
        # Windows PowerShell needs the handle retained before a redirected wait.
        $null = $process.Handle
        try {
            if (-not $process.WaitForExit($TimeoutMinutes * 60 * 1000)) {
                throw "Autosave smoke $phase exceeded $TimeoutMinutes minutes. Log: $log"
            }
            if ($process.ExitCode -ne 0) { throw "Autosave smoke $phase exited with code $($process.ExitCode). Log: $log" }
        } finally {
            if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force; $null = $process.WaitForExit(10000) }
            $process.Dispose()
        }
        if (-not (Test-Path -LiteralPath $log -PathType Leaf)) { throw "Autosave smoke $phase produced no log at $log." }
        $text = Get-Content -LiteralPath $log -Raw
        if (-not $text.Contains("UEGT2_AUTOSAVE_SMOKE_COMPLETE phase=$phase slot=$slot ") -or
            $text -match 'UEGT2_AUTOSAVE_SMOKE_FAILED|Critical error|Assertion failed|invalid ShaderMap') {
            throw "Autosave smoke $phase failed or did not complete. Log: $log"
        }
        Write-Host "Autosave smoke $phase passed. Log: $log"
    }
    if ($Capture) {
        foreach ($name in @('01_ContinueAutosave.png', '02_AutosaveSetting.png')) {
            $shot = Join-Path $captureDir $name
            if (-not (Test-Path -LiteralPath $shot -PathType Leaf) -or (Get-Item -LiteralPath $shot).Length -eq 0) {
                throw "Autosave smoke did not produce $shot."
            }
        }
        Write-Host "Autosave screenshots: $captureDir"
    }
    Write-Host 'Autosave smoke passed: periodic writes, manual preservation, cross-process fallback and off gates.' -ForegroundColor Green
} finally {
    # Every process has exited before cleanup. Remove only this GUID's four
    # exact fixture files; retain logs/settings and any unrelated data.
    $savePrefix = [IO.Path]::GetFullPath($saveDir).TrimEnd('\') + '\'
    foreach ($suffix in @('_A', '_B', '_Auto_A', '_Auto_B')) {
        $fileName = $slot + $suffix + '.sav'
        $saveFile = [IO.Path]::GetFullPath((Join-Path $saveDir $fileName))
        if (-not $saveFile.StartsWith($ownedPrefix, [StringComparison]::OrdinalIgnoreCase) -or
            -not $saveFile.StartsWith($savePrefix, [StringComparison]::OrdinalIgnoreCase) -or
            (Split-Path -Leaf $saveFile) -ne $fileName) {
            throw 'Refusing to remove a checkpoint outside the autosave fixture.'
        }
        if (Test-Path -LiteralPath $saveFile -PathType Leaf) { Remove-Item -LiteralPath $saveFile -Force }
    }
}
