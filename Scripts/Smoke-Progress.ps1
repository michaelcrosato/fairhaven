<#
.SYNOPSIS
Checks packaged progress saving, loading in a new process, and the player off switch.

.DESCRIPTION
Uses one unique temporary save slot and an isolated user directory. The player's
normal saves and settings are never read or written. -Capture also records the
real Continue and Save Progress menu controls.
#>
[CmdletBinding()]
param(
    [ValidateRange(1, 1440)]
    [int] $TimeoutMinutes = 5,
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
$userDir = [IO.Path]::GetFullPath((Join-Path $packagedProject "Saved\ProgressSmoke\$runId"))
$ownedPrefix = [IO.Path]::GetFullPath((Join-Path $packagedProject 'Saved\ProgressSmoke')).TrimEnd('\') + '\'
if (-not $userDir.StartsWith($ownedPrefix, [StringComparison]::OrdinalIgnoreCase) -or
    (Split-Path -Leaf $userDir) -ne $runId) {
    throw 'Progress smoke user directory escaped the packaged build.'
}
$saveDir = Join-Path $userDir 'Saved\SaveGames'
$logDir = Join-Path $projectRoot 'Saved\Logs'
$captureDir = Join-Path $projectRoot "Saved\Screenshots\ProgressSmoke\$runId"
New-Item -ItemType Directory -Path $userDir, $logDir -Force | Out-Null
if ($Capture) { New-Item -ItemType Directory -Path $captureDir -Force | Out-Null }

Write-Host "Progress smoke: $($exe.FullName)"
Write-Host "Isolated slot: $slot"
try {
    foreach ($phase in @('Write', 'Read', 'NewVisit', 'Disabled')) {
        $log = Join-Path $logDir "ProgressSmoke-$runId-$phase.log"
        $arguments = @(
            '-RenderOffscreen', '-Windowed', '-ForceRes', '-ResX=1920', '-ResY=1080',
            '-unattended', '-nosplash', '-nopause', '-nosound',
            "-UEGT2ProgressSmoke=$phase", "-UEGT2ProgressSlot=$slot",
            "-UserDir=`"$($userDir.Replace('\', '/'))`"", "-abslog=`"$log`""
        )
        # NewVisit must enter play through the one-shot travel request; a
        # permanent SkipMenu flag would conceal a broken journey transition.
        if ($phase -ne 'NewVisit') { $arguments += '-UEGT2SkipMenu' }
        if ($Capture -and $phase -eq 'Read') {
            $arguments += "-UEGT2ProgressCapture=`"$($captureDir.Replace('\', '/'))`""
        }
        $process = Start-Process -FilePath $exe.FullName -ArgumentList $arguments -PassThru -WindowStyle Hidden `
            -RedirectStandardOutput (Join-Path $logDir "ProgressSmoke-$runId-$phase.stdout.log") `
            -RedirectStandardError (Join-Path $logDir "ProgressSmoke-$runId-$phase.stderr.log")
        # Windows PowerShell needs the handle retained before a redirected wait.
        $null = $process.Handle
        try {
            if (-not $process.WaitForExit($TimeoutMinutes * 60 * 1000)) {
                throw "Progress smoke $phase exceeded $TimeoutMinutes minutes. Log: $log"
            }
            if ($process.ExitCode -ne 0) {
                throw "Progress smoke $phase exited with code $($process.ExitCode). Log: $log"
            }
        } finally {
            if (-not $process.HasExited) {
                Stop-Process -Id $process.Id -Force
                $null = $process.WaitForExit(10000)
            }
            $process.Dispose()
        }
        if (-not (Test-Path -LiteralPath $log -PathType Leaf)) {
            throw "Progress smoke $phase produced no log at $log."
        }
        $text = Get-Content -LiteralPath $log -Raw
        $marker = "UEGT2_PROGRESS_SMOKE_COMPLETE phase=$phase slot=$slot"
        if (-not $text.Contains($marker) -or
            $text -match 'UEGT2_PROGRESS_SMOKE_FAILED|Critical error|Assertion failed|invalid ShaderMap') {
            throw "Progress smoke $phase failed or did not complete. Log: $log"
        }
        Write-Host "Progress smoke $phase passed. Log: $log"
    }

    if ($Capture) {
        foreach ($name in @('01_Continue.png', '02_SaveProgress.png', '03_ProgressSetting.png')) {
            $shot = Join-Path $captureDir $name
            if (-not (Test-Path -LiteralPath $shot -PathType Leaf) -or (Get-Item -LiteralPath $shot).Length -eq 0) {
                throw "Progress smoke did not produce $shot."
            }
        }
        Write-Host "Menu screenshots: $captureDir"
    }
    Write-Host 'Progress smoke passed: cross-process restoration and disabled saving verified.' -ForegroundColor Green
} finally {
    # Runtime deletes its own two slots after Disabled. On failure, remove only
    # these exact files beneath this run's isolated Saved/SaveGames directory.
    # Keep logs/settings for diagnosis; never recursively delete a chosen path.
    $savePrefix = [IO.Path]::GetFullPath($saveDir).TrimEnd('\') + '\'
    foreach ($suffix in @('_A', '_B')) {
        $saveFile = [IO.Path]::GetFullPath((Join-Path $saveDir ($slot + $suffix + '.sav')))
        if (-not $saveFile.StartsWith($ownedPrefix, [StringComparison]::OrdinalIgnoreCase) -or
            -not $saveFile.StartsWith($savePrefix, [StringComparison]::OrdinalIgnoreCase) -or
            (Split-Path -Leaf $saveFile) -ne ($slot + $suffix + '.sav')) {
            throw 'Refusing to remove a save outside the progress smoke fixture.'
        }
        if (Test-Path -LiteralPath $saveFile -PathType Leaf) { Remove-Item -LiteralPath $saveFile -Force }
    }
}
