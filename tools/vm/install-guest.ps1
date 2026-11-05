#!/usr/bin/env pwsh
#
# Copyright (c) 2026 Virtual Serial Studio contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
<#
.SYNOPSIS
    Runs the unattended install of a guest made by new-vm.ps1, from the first start to a verified
    "clean" snapshot, with nobody at the console.

.DESCRIPTION
    1. Starts the guest.
    2. Windows: when Setup restarts the machine at the end of its first phase, the install ISO would
       boot again and start over, because it has no boot prompt. The script stops the guest at that
       moment, disconnects the install ISO and starts it again, so the installed disk boots.
    3. Waits for the guest to power itself off, which means it has finished setting itself up.
    4. Disconnects the install media, starts the guest, waits for VMware Tools and runs a command in
       the guest with the credentials from VSS_VM_USER and VSS_VM_PASSWORD. Anything but exit code 0
       is a failure, and no snapshot is taken.
    5. Stops the guest and takes the "clean" snapshot, unless -NoSnapshot is given.

    The credentials come from the environment; load them with credentials.ps1 first. The script prints
    progress and never the password.

.PARAMETER Guest
    The id of a guest in matrix.json: windows10, windows11 or ubuntu2604.

.PARAMETER SkipInstall
    The install has already finished and the guest is powered off: go straight to the verification.

.PARAMETER TimeoutMinutes
    How long the unattended install may take before the script gives up. The default is 120.
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory, Position = 0)]
    [ValidateSet('windows10', 'windows11', 'ubuntu2604')]
    [string] $Guest,

    [string] $Root,
    [int] $TimeoutMinutes = 120,
    [switch] $NoSnapshot,
    [switch] $SkipInstall
)

$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$matrix = Get-Content (Join-Path $here 'matrix.json') -Raw | ConvertFrom-Json
$entry = $matrix.guests | Where-Object { $_.id -eq $Guest }
if (-not $entry) { throw "no guest '$Guest' in matrix.json" }
if (-not $Root) { $Root = $matrix.root }
$vm = [IO.Path]::Combine($Root, $entry.name, "$($entry.name).vmx")
$log = [IO.Path]::Combine($Root, $entry.name, 'vmware.log')
if (-not (Test-Path -LiteralPath $vm)) { throw "no such guest: $vm" }
if (-not $env:VSS_VM_USER -or -not $env:VSS_VM_PASSWORD) { throw 'load the credentials first: . ./tools/vm/credentials.ps1' }

$vmrun = if ($env:VSS_VMRUN) { $env:VSS_VMRUN } else { 'C:\Program Files\VMware\VMware Workstation\vmrun.exe' }
$harness = Join-Path $here 'vmrun.ps1'
$builder = Join-Path $here 'new-vm.ps1'
$snapshotName = $matrix.snapshot

function Write-Step([string] $Text) { Write-Host ("[{0:HH:mm:ss}] {1}" -f (Get-Date), $Text) }

# vmrun can block when the guest misbehaves, so every call has a time limit.
function Invoke-Vmrun([string[]] $Arguments, [int] $Seconds = 90) {
    $out = [IO.Path]::GetTempFileName()
    $process = Start-Process -FilePath $vmrun -ArgumentList $Arguments -PassThru -WindowStyle Hidden -RedirectStandardOutput $out
    if (-not $process.WaitForExit($Seconds * 1000)) {
        $process.Kill()
        Remove-Item $out -ErrorAction SilentlyContinue
        return [pscustomobject]@{ Code = -999; Output = 'timed out' }
    }
    $text = (Get-Content $out -ErrorAction SilentlyContinue) -join "`n"
    Remove-Item $out -ErrorAction SilentlyContinue
    return [pscustomobject]@{ Code = $process.ExitCode; Output = $text }
}

function Test-Running {
    return ((Invoke-Vmrun @('-T', 'ws', 'list') 30).Output -split "`n") -contains $vm
}

function Start-Guest {
    $result = Invoke-Vmrun @('-T', 'ws', 'start', "`"$vm`"", 'nogui') 120
    if ($result.Code -ne 0) { throw "start failed: $($result.Output)" }
}

function Stop-Guest {
    $null = Invoke-Vmrun @('-T', 'ws', 'stop', "`"$vm`"", 'hard') 120
    for ($i = 0; $i -lt 30 -and (Test-Running); $i++) { Start-Sleep 2 }
    if (Test-Running) { throw 'the guest did not stop' }
}

function Get-ResetCount {
    if (-not (Test-Path -LiteralPath $log)) { return 0 }
    return @(Select-String -LiteralPath $log -Pattern 'requested that the virtual machine be hard reset' -SimpleMatch:$false).Count
}

function Disconnect-InstallIso {
    $lines = Get-Content -LiteralPath $vm | ForEach-Object { if ($_ -match '^sata0:1\.present\s*=') { 'sata0:1.present = "FALSE"' } else { $_ } }
    Set-Content -LiteralPath $vm -Value $lines -Encoding ascii
}

$deadline = (Get-Date).AddMinutes($TimeoutMinutes)

if (-not $SkipInstall) {
    Write-Step "starting $($entry.name)"
    Start-Guest

    if ($entry.kind -eq 'windows') {
        Write-Step 'waiting for Setup to restart the machine at the end of its first phase'
        while ((Get-ResetCount) -lt 1) {
            if ((Get-Date) -gt $deadline) { throw 'Setup did not finish its first phase in time' }
            if (-not (Test-Running)) { throw 'the guest powered off before Setup restarted it' }
            Start-Sleep 5
        }
        Write-Step 'first phase done: disconnecting the install ISO and starting again from the disk'
        Stop-Guest
        Disconnect-InstallIso
        Start-Guest
    }

    Write-Step 'waiting for the guest to finish and power itself off'
    $last = Get-Date
    while (Test-Running) {
        if ((Get-Date) -gt $deadline) { throw 'the guest did not power off in time' }
        if (((Get-Date) - $last).TotalMinutes -ge 5) { Write-Step 'still installing'; $last = Get-Date }
        Start-Sleep 15
    }
    Write-Step 'the guest powered off'
}

& $builder -Guest $Guest -DetachMedia -Root $Root | Out-Null

Write-Step 'starting the finished guest to verify it'
Start-Guest
$toolsReady = $false
for ($i = 0; $i -lt 60; $i++) {
    $state = Invoke-Vmrun @('-T', 'ws', 'checkToolsState', "`"$vm`"") 30
    if ($state.Output -match 'running') { $toolsReady = $true; break }
    Start-Sleep 10
}
if (-not $toolsReady) { throw 'VMware Tools did not start in the finished guest' }

$check = if ($entry.kind -eq 'windows') { 'powershell.exe -NoProfile -Command "if (Test-Path C:\vss\ready.txt) { exit 0 } else { exit 3 }"' }
         else { "python3 -c 'import serial' && command -v socat picocom minicom screen && sudo -n true" }
& pwsh -NoProfile -File $harness run -Vm $vm -CommandLine $check
$code = $LASTEXITCODE
if ($code -ne 0) { throw "the guest check failed with exit code $code; no snapshot was taken" }
Write-Step 'the guest check passed'

Stop-Guest
if (-not $NoSnapshot) {
    & pwsh -NoProfile -File $harness snapshot -Vm $vm -Name $snapshotName
    if ($LASTEXITCODE -ne 0) { throw 'taking the snapshot failed' }
    Write-Step "took the '$snapshotName' snapshot of $($entry.name)"
}
Write-Step 'done'
