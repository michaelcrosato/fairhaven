<#
.SYNOPSIS
Checks the real survey contract, one-time payment, checkpoint restore and off gates.

.DESCRIPTION
Uses one unique temporary save slot and an isolated user directory. The player's
normal saves and settings are never read or written. -Capture records the real
signpost, unfinished checklist, eligible claim, Paid page and disabled setting. Use -Width 1280
-Height 720 for the smaller layout check.
#>
[CmdletBinding()]
param(
    [ValidateRange(1, 1440)]
    [int] $TimeoutMinutes = 5,
    [ValidateRange(1280, 7680)]
    [int] $Width = 1920,
    [ValidateRange(720, 4320)]
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
$slot = 'UEGT2_ContractSmoke_' + $runId
$userDir = [IO.Path]::GetFullPath((Join-Path $packagedProject "Saved\ContractSmoke\$runId"))
$ownedPrefix = [IO.Path]::GetFullPath((Join-Path $packagedProject 'Saved\ContractSmoke')).TrimEnd('\') + '\'
if (-not $userDir.StartsWith($ownedPrefix, [StringComparison]::OrdinalIgnoreCase) -or
    (Split-Path -Leaf $userDir) -ne $runId) {
    throw 'Contract smoke user directory escaped the packaged build.'
}
$saveDir = Join-Path $userDir 'Saved\SaveGames'
$logDir = Join-Path $projectRoot 'Saved\Logs'
$captureDir = Join-Path $projectRoot "Saved\Screenshots\ContractSmoke\$runId"
New-Item -ItemType Directory -Path $userDir, $logDir -Force | Out-Null
if ($Capture) { New-Item -ItemType Directory -Path $captureDir -Force | Out-Null }

Write-Host "Contract smoke: $($exe.FullName)"
Write-Host "Isolated slot: $slot"
try {
    foreach ($phase in @('Write', 'Read', 'NewVisit', 'Disabled')) {
        $log = Join-Path $logDir "ContractSmoke-$runId-$phase.log"
        $arguments = @(
            '-RenderOffscreen', '-Windowed', '-ForceRes', "-ResX=$Width", "-ResY=$Height",
            '-unattended', '-nosplash', '-nopause', '-nosound',
            "-UEGT2ContractSmoke=$phase", "-UEGT2ContractSlot=$slot",
            "-UserDir=`"$($userDir.Replace('\', '/'))`"", "-abslog=`"$log`""
        )
        # NewVisit must enter play through the one-shot travel request; a
        # permanent SkipMenu flag would conceal a broken journey transition.
        if ($phase -ne 'NewVisit') { $arguments += '-UEGT2SkipMenu' }
        if ($Capture -and $phase -eq 'Write') {
            $arguments += "-UEGT2ContractCapture=`"$($captureDir.Replace('\', '/'))`""
        }
        $process = Start-Process -FilePath $exe.FullName -ArgumentList $arguments -PassThru -WindowStyle Hidden `
            -RedirectStandardOutput (Join-Path $logDir "ContractSmoke-$runId-$phase.stdout.log") `
            -RedirectStandardError (Join-Path $logDir "ContractSmoke-$runId-$phase.stderr.log")
        # Windows PowerShell needs the handle retained before a redirected wait.
        $null = $process.Handle
        try {
            if (-not $process.WaitForExit($TimeoutMinutes * 60 * 1000)) {
                throw "Contract smoke $phase exceeded $TimeoutMinutes minutes. Log: $log"
            }
            if ($process.ExitCode -ne 0) {
                throw "Contract smoke $phase exited with code $($process.ExitCode). Log: $log"
            }
        } finally {
            if (-not $process.HasExited) {
                Stop-Process -Id $process.Id -Force
                $null = $process.WaitForExit(10000)
            }
            $process.Dispose()
        }
        if (-not (Test-Path -LiteralPath $log -PathType Leaf)) {
            throw "Contract smoke $phase produced no log at $log."
        }
        $text = Get-Content -LiteralPath $log -Raw
        $marker = "UEGT2_CONTRACT_SMOKE_COMPLETE phase=$phase slot=$slot "
        if (-not $text.Contains($marker) -or
            $text -match 'UEGT2_CONTRACT_SMOKE_FAILED|Critical error|Assertion failed|invalid ShaderMap') {
            throw "Contract smoke $phase failed or did not complete. Log: $log"
        }
        Write-Host "Contract smoke $phase passed. Log: $log"
        foreach ($suffix in @('_Auto_A', '_Auto_B')) {
            if (Test-Path -LiteralPath (Join-Path $saveDir ($slot + $suffix + '.sav')) -PathType Leaf) {
                throw "Contract smoke unexpectedly wrote an autosave. Log: $log"
            }
        }
    }

    if ($Capture) {
        foreach ($name in @('01_Board.png', '02_NotSurveyed.png', '03_ClaimReady.png', '04_Paid.png', '05_ContractSetting.png')) {
            $shot = Join-Path $captureDir $name
            if (-not (Test-Path -LiteralPath $shot -PathType Leaf) -or (Get-Item -LiteralPath $shot).Length -eq 0) {
                throw "Contract smoke did not produce $shot."
            }
        }
        Write-Host "Menu screenshots: $captureDir"
    }
    Write-Host 'Contract smoke passed: real survey/claim, one payment, paid checkpoint restore, New Visit and both off gates verified.' -ForegroundColor Green
} finally {
    # Remove only these two exact files after all phases or a failed phase.
    # Keep logs/settings for diagnosis; never recursively delete a chosen path.
    $savePrefix = [IO.Path]::GetFullPath($saveDir).TrimEnd('\') + '\'
    foreach ($suffix in @('_A', '_B')) {
        $saveFile = [IO.Path]::GetFullPath((Join-Path $saveDir ($slot + $suffix + '.sav')))
        if (-not $saveFile.StartsWith($ownedPrefix, [StringComparison]::OrdinalIgnoreCase) -or
            -not $saveFile.StartsWith($savePrefix, [StringComparison]::OrdinalIgnoreCase) -or
            (Split-Path -Leaf $saveFile) -ne ($slot + $suffix + '.sav')) {
            throw 'Refusing to remove a save outside the contract smoke fixture.'
        }
        if (Test-Path -LiteralPath $saveFile -PathType Leaf) { Remove-Item -LiteralPath $saveFile -Force }
    }
}
