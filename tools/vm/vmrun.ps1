#!/usr/bin/env pwsh
#
# Copyright (c) 2026 Virtual Serial Studio contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
<#
.SYNOPSIS
    VMware Workstation driver test loop.

.DESCRIPTION
    A thin, predictable wrapper around vmrun. Host-side actions (list, status,
    start, stop, snapshot, revert) need no credentials. Guest actions (copy-to,
    run, ip) read VSS_VM_USER and VSS_VM_PASSWORD from the environment; nothing
    is stored in the repository.

    The rollback unit is a snapshot: prepare the VM once, take a "clean"
    snapshot, revert before every run.

.PARAMETER Action
    list | status | start | stop | snapshot | revert | copy-to | run | ip

.EXAMPLE
    ./vmrun.ps1 list
    ./vmrun.ps1 snapshot -Vm 'D:\Virtual Machines\Developer VM\Developer VM.vmx' -Name clean

.EXAMPLE
    $env:VSS_VM_USER = 'tester'; $env:VSS_VM_PASSWORD = '...'
    ./vmrun.ps1 copy-to -Vm $vm -Local ./artifacts/vsscom.sys -Guest 'C:\vss\vsscom.sys'
    ./vmrun.ps1 run -Vm $vm -CommandLine 'powershell -ExecutionPolicy Bypass -File C:\vss\test.ps1'
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory, Position = 0)]
    [ValidateSet('list', 'status', 'start', 'stop', 'snapshot', 'revert',
                 'copy-to', 'run', 'ip')]
    [string] $Action,

    [string] $Vm,
    [string] $Name = 'clean',
    [string] $Local,
    [string] $Guest,
    [string] $CommandLine,
    [switch] $Gui,
    [switch] $DryRun
)

$ErrorActionPreference = 'Stop'

function Find-Vmrun {
    if ($env:VSS_VMRUN) {
        return $env:VSS_VMRUN
    }

    $candidates = @(
        'C:\Program Files (x86)\VMware\VMware Workstation\vmrun.exe',
        'C:\Program Files\VMware\VMware Workstation\vmrun.exe',
        '/usr/bin/vmrun',
        '/Applications/VMware Fusion.app/Contents/Library/vmrun'
    )
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) { return $candidate }
    }

    $command = Get-Command vmrun -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }

    throw 'vmrun not found; set VSS_VMRUN to the executable'
}

$vmrun = Find-Vmrun

function Invoke-Vmrun {
    param([string[]] $Arguments)

    Write-Verbose ("vmrun -T ws " + ($Arguments -join ' '))
    if ($DryRun) {
        Write-Host ("vmrun -T ws " + ($Arguments -join ' '))
        return 0
    }

    & $vmrun -T ws @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "vmrun failed with exit code $LASTEXITCODE"
    }
}

function Get-GuestArguments {
    if (-not $env:VSS_VM_USER -or -not $env:VSS_VM_PASSWORD) {
        throw 'set VSS_VM_USER and VSS_VM_PASSWORD for guest operations'
    }
    return @('-gu', $env:VSS_VM_USER, '-gp', $env:VSS_VM_PASSWORD)
}

function Require-Vm {
    if (-not $Vm) { throw "-Vm is required for $Action" }
}

switch ($Action) {
    'list' {
        Invoke-Vmrun @('list')
    }

    'status' {
        Require-Vm
        $running = & $vmrun -T ws list
        if ($running | Where-Object { $_ -like "*$Vm" }) { 'running' }
        else { 'stopped' }
    }

    'start' {
        Require-Vm
        $mode = if ($Gui) { 'gui' } else { 'nogui' }
        Invoke-Vmrun @('start', $Vm, $mode)
    }

    'stop' {
        Require-Vm
        Invoke-Vmrun @('stop', $Vm, 'soft')
    }

    'snapshot' {
        Require-Vm
        Invoke-Vmrun @('snapshot', $Vm, $Name)
    }

    'revert' {
        Require-Vm
        Invoke-Vmrun @('revertToSnapshot', $Vm, $Name)
    }

    'copy-to' {
        Require-Vm
        if (-not $Local -or -not $Guest) {
            throw '-Local and -Guest are required for copy-to'
        }
        Invoke-Vmrun (@('copyFileFromHostToGuest', $Vm) +
                      (Get-GuestArguments) + @($Local, $Guest))
    }

    'run' {
        Require-Vm
        if (-not $CommandLine) { throw '-CommandLine is required for run' }

        # Pipe the program's exit code through a marker file: vmrun itself only
        # reports whether the guest command was launched.
        $marker = 'C:\vss-exit.txt'
        $wrapped = "cmd.exe /c `"$CommandLine & echo %ERRORLEVEL% > $marker`""
        Invoke-Vmrun (@('runProgramInGuest', $Vm) +
                      (Get-GuestArguments) + @($wrapped))

        $local = Join-Path ([IO.Path]::GetTempPath()) 'vss-exit.txt'
        Invoke-Vmrun (@('copyFileFromGuestToHost', $Vm) +
                      (Get-GuestArguments) + @($marker, $local))
        $code = [int]((Get-Content $local -Raw).Trim())
        Remove-Item $local -ErrorAction SilentlyContinue
        return $code
    }

    'ip' {
        Require-Vm
        Invoke-Vmrun @('getGuestIPAddress', $Vm, '-wait')
    }
}
