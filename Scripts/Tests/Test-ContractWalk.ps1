<#
.SYNOPSIS
Checks the contract walking wrapper's failure handling and native argument quoting.

.DESCRIPTION
Uses isolated fake packages and process mocks. Only two short Python argument
probes launch real processes; no Unreal executable or player data is accessed.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$fixtureParent = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
$fixtureRoot = Join-Path $fixtureParent ('UEGT2-ContractWalkWrapper-' + [guid]::NewGuid().ToString('N'))
$source = Join-Path (Split-Path -Parent $PSScriptRoot) 'Smoke-ContractWalk.ps1'
$python = (Get-Command python -ErrorAction Stop).Source
$state = @{}
function Assert([bool]$Value, [string]$Message) { if (-not $Value) { throw $Message } }
function Start-Process {
    param($FilePath, $ArgumentList, [switch]$PassThru, $WindowStyle, $RedirectStandardOutput, $RedirectStandardError)
    $line = $ArgumentList -join ' '
    Assert ($state.mode -ne 'missing_exe') 'process created without a packaged executable'
    Assert ($ArgumentList -contains '-UEGT2ContractWalkSmoke' -and $ArgumentList -contains '-UEGT2SkipMenu') 'wrong diagnostic flags'
    Assert ($ArgumentList -contains "-ResX=$($state.width)" -and $ArgumentList -contains "-ResY=$($state.height)") 'resolution not forwarded'
    Assert ($line -notmatch '\.uproject|UEGT2Capture|UEGT2Progress|UEGT2Autosave|UEGT2ContractSmoke|UEGT2ContractSlot|UEGT2Time|UEGT2Weather|UEGT2Smoke|UEGT2CrossingSmoke|UEGT2LiveNPCs') 'interfering project or diagnostic'
    Assert ($ArgumentList.Count -eq 13) 'unexpected extra argument'
    Assert ($PassThru -and $WindowStyle -eq 'Hidden') 'process must be hidden and owned'
    Assert ($line -match '-UserDir="([^"]+/Saved/ContractWalkSmoke/([0-9a-f]{32}))"') 'unsafe or unquoted UserDir'
    $userDir=$Matches[1]; $runId=$Matches[2]
    $packaged = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $FilePath))
    Assert ([IO.Path]::GetFullPath($userDir) -eq [IO.Path]::GetFullPath((Join-Path $packaged "Saved\ContractWalkSmoke\$runId"))) 'UserDir escaped packaged project'
    Assert ($line -match '-abslog="([^"]+)"') 'log quoting'; $log=$Matches[1]
    Assert (-not (Test-Path -LiteralPath $log)) 'evidence log is not fresh'
    Assert ($RedirectStandardOutput -eq $log.Replace('.log', '.stdout.log') -and
        $RedirectStandardError -eq $log.Replace('.log', '.stderr.log')) 'stdout/stderr not isolated by run'
    $state.userDir=$userDir
    $state.outsideSave=Join-Path $packaged 'Saved\SaveGames\RealPlayerSentinel.sav'
    New-Item -ItemType Directory -Path (Split-Path -Parent $state.outsideSave) -Force | Out-Null
    Set-Content -LiteralPath $state.outsideSave -Value keep
    $state.outsideSettings=Join-Path $packaged 'Saved\Config\Windows\GameUserSettings.ini'
    New-Item -ItemType Directory -Path (Split-Path -Parent $state.outsideSettings) -Force | Out-Null
    Set-Content -LiteralPath $state.outsideSettings -Value 'unchanged preferences'
    if ($state.mode -like 'native*') {
        $probe=Join-Path $state.case 'native argv probe.py'
        $record=Join-Path $state.case 'native argv result.json'
        Set-Content -LiteralPath $probe -Encoding ASCII -Value 'import json,pathlib,sys; pathlib.Path(sys.argv[1]).write_text(json.dumps(sys.argv[2:]))'
        $native=Microsoft.PowerShell.Management\Start-Process -FilePath $python -PassThru -WindowStyle Hidden `
            -ArgumentList (@("`"$probe`"", "`"$record`"") + $ArgumentList)
        $null=$native.Handle
        try {
            Assert ($native.WaitForExit(10000)) 'native argv probe timed out'
            Assert ($native.ExitCode -eq 0) 'native argv probe failed'
        } finally {
            if (-not $native.HasExited) { Microsoft.PowerShell.Management\Stop-Process -Id $native.Id -Force }
            $native.Dispose()
        }
        # Windows PowerShell emits a JSON array as one pipeline object; avoid
        # wrapping it in another array before comparing the native argv.
        $actual=Get-Content -LiteralPath $record -Raw | ConvertFrom-Json
        $expected=@($ArgumentList | ForEach-Object { $_.Replace('"', '') })
        Assert (($actual -join "`n") -ceq ($expected -join "`n")) (
            "native parser changed arguments: actual=$($actual | ConvertTo-Json -Compress); expected=$($expected | ConvertTo-Json -Compress)")
    }
    if ($state.mode -ne 'missing_log') {
        $marker="UEGT2_CONTRACT_WALK_SMOKE_COMPLETE run=$runId legs=4 surveyed=3 verified"
        if ($state.mode -eq 'missing_marker') { $marker='incomplete' }
        if ($state.mode -eq 'wrong_guid') { $marker="UEGT2_CONTRACT_WALK_SMOKE_COMPLETE run=${runId}extra verified" }
        if ($state.mode -eq 'failed_marker') { $marker+="`nUEGT2_CONTRACT_WALK_SMOKE_FAILED explicit failure" }
        if ($state.mode -eq 'assertion') { $marker+="`nAssertion failed: injected" }
        if ($state.mode -eq 'shader_failure') { $marker+="`ninvalid ShaderMap injected" }
        Set-Content -LiteralPath $log -Value $marker
    }
    if ($state.mode -like 'unexpected_save*') {
        $saveDir=if ($state.mode -eq 'unexpected_save') {Join-Path $userDir 'Saved\SaveGames\Nested'} else {$userDir}
        New-Item -ItemType Directory -Path $saveDir -Force | Out-Null
        $state.unexpectedSave=Join-Path $saveDir 'KeepForDiagnosis.sav'
        Set-Content -LiteralPath $state.unexpectedSave -Value unexpected
    }
    $state.process=[pscustomobject]@{Id=12345; ExitCode=$(if ($state.mode -eq 'crash') {7} else {0}); HasExited=($state.mode -ne 'timeout'); Disposed=$false}
    $state.process | Add-Member ScriptProperty Handle {$state.handleTouched=$true; return 1}
    $state.process | Add-Member ScriptMethod WaitForExit {
        param($Timeout)
        Assert $state.handleTouched 'Windows PowerShell handle was not cached before wait'
        $state.waits.Add($Timeout)
        return $this.HasExited
    }
    $state.process | Add-Member ScriptMethod Dispose {$this.Disposed=$true}
    return $state.process
}
function Stop-Process {
    param($Id, [switch]$Force)
    Assert ($Id -eq 12345 -and $Force) 'stopped unrelated process'
    $state.stopped=$true; $state.process.HasExited=$true
}
try {
    foreach ($mode in @('success','missing_exe','crash','timeout','missing_log','missing_marker','wrong_guid',
        'failed_marker','assertion','shader_failure','unexpected_save','unexpected_save_root','native_1080','native_720')) {
        $case=Join-Path $fixtureRoot ('case ' + $mode)
        $scripts=Join-Path $case 'Scripts'
        $binaries=Join-Path $case 'LocalBuilds\Windows Development\UEGT2\Binaries\Win64'
        New-Item -ItemType Directory -Path $scripts,$binaries -Force | Out-Null
        Copy-Item -LiteralPath $source -Destination $scripts
        if ($mode -ne 'missing_exe') { Set-Content -LiteralPath (Join-Path $binaries 'UEGT2.exe') -Value 'text placeholder; never executed' }
        $state=@{mode=$mode; case=$case; stopped=$false; handleTouched=$false; waits=[Collections.Generic.List[int]]::new(); width=1920; height=1080}
        if ($mode -in @('success','native_720')) { $state.width=1280; $state.height=720 }
        $failure=$null
        try { & (Join-Path $scripts 'Smoke-ContractWalk.ps1') -Width $state.width -Height $state.height 6>$null } catch {$failure=$_.Exception.Message}
        if ($mode -eq 'success' -or $mode -like 'native*') { Assert ($null -eq $failure) "unexpected failure: $failure" }
        else {
            Assert ($null -ne $failure) "$mode accepted"
            $expected=switch ($mode) {
                missing_exe {'No packaged build found'}; crash {'exited with code 7'}; timeout {'exceeded 30 minutes'}
                missing_log {'produced no log'}; unexpected_save {'unexpectedly wrote a checkpoint'}
                unexpected_save_root {'unexpectedly wrote a checkpoint'}
                default {'failed or did not complete'}
            }
            Assert ($failure.Contains($expected)) "wrong failure for ${mode}: $failure"
        }
        if ($mode -ne 'missing_exe') {
            Assert $state.process.Disposed 'handle was not disposed'
            Assert ($state.stopped -eq ($mode -eq 'timeout')) 'wrong termination state'
            Assert ($state.waits[0] -eq 1800000) 'unbounded or incorrect main wait'
            if ($mode -eq 'timeout') { Assert ($state.waits.Count -eq 2 -and $state.waits[1] -eq 10000) 'termination wait not bounded to ten seconds' }
            Assert (Test-Path -LiteralPath $state.userDir) 'isolated evidence directory deleted'
            Assert ((Get-Content -LiteralPath $state.outsideSave -Raw).Trim() -eq 'keep') 'unrelated checkpoint changed'
            Assert ((Get-Content -LiteralPath $state.outsideSettings -Raw).Trim() -eq 'unchanged preferences') 'unrelated profile changed'
            if ($mode -like 'unexpected_save*') { Assert (Test-Path -LiteralPath $state.unexpectedSave) 'unexpected checkpoint evidence removed' }
        }
        Write-Host "PASS contract walk wrapper $mode"
    }
} finally {
    $resolved=[IO.Path]::GetFullPath($fixtureRoot)
    Assert ($resolved.StartsWith($fixtureParent, [StringComparison]::OrdinalIgnoreCase) -and (Split-Path -Leaf $resolved) -match '^UEGT2-ContractWalkWrapper-[0-9a-f]{32}$') 'unsafe fixture cleanup'
    if (Test-Path -LiteralPath $resolved) { Remove-Item -LiteralPath $resolved -Recurse -Force }
}
Write-Host 'Contract walk wrapper checks passed: 12 simulated cases and 2 native argument probes.' -ForegroundColor Green
