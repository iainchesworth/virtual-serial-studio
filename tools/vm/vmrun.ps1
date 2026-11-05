#!/usr/bin/env pwsh
#
# Copyright (c) 2026 Virtual Serial Studio contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
<#
.SYNOPSIS
    VMware Workstation test loop for the VSS guests.

.DESCRIPTION
    A thin wrapper around vmrun. It works on the VSS guests only: the .vmx file
    name must start with "VSS " or "VSS-". Any other guest is refused.

    Host actions (list, status, start, stop, snapshots, snapshot, revert) need no
    credentials. Guest actions (copy-to, copy-from, run, screenshot, ip,
    wait-tools) read VSS_VM_USER and VSS_VM_PASSWORD from the environment.
    Nothing is stored in the repository, and the password is replaced by
    "<hidden>" in every line this script prints.

    The rollback unit is a snapshot. The script never creates one on its own: the
    snapshot action creates the named snapshot once and fails if it exists.

    The run action exits with the exit code of the guest command. If the guest
    does not report one, the action fails.

.PARAMETER Action
    list | status | start | stop | snapshots | snapshot | revert | copy-to |
    copy-from | run | screenshot | ip | wait-tools | wait-guest

.PARAMETER Kind
    The guest family, used by the run action to choose the shell. The default
    reads guestOS from the .vmx file.

.EXAMPLE
    ./vmrun.ps1 snapshots -Vm 'D:\Virtual Machines\VSS - Windows 10\VSS - Windows 10.vmx'

.EXAMPLE
    # The credentials come from the environment of your own shell.
    ./vmrun.ps1 copy-to -Vm $vm -Local ./artifacts/vsscom.sys -Guest 'C:\vss\vsscom.sys'
    ./vmrun.ps1 run -Vm $vm -CommandLine 'powershell -File C:\vss\test.ps1'
    "guest exit code: $LASTEXITCODE"
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory, Position = 0)]
    [ValidateSet('list', 'status', 'start', 'stop', 'snapshots', 'snapshot',
                 'revert', 'copy-to', 'copy-from', 'run', 'screenshot', 'ip',
                 'wait-tools', 'wait-guest')]
    [string] $Action,

    [string] $Vm,
    [string] $Name = 'clean',
    [string] $Local,
    [string] $Guest,
    [string] $CommandLine,
    [ValidateSet('auto', 'windows', 'linux')]
    [string] $Kind = 'auto',
    [int] $TimeoutSeconds = 300,
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

function Format-Arguments {
    # The text shown to a person: the value after -gp is never printed.
    param([string[]] $Arguments)

    $shown = @()
    for ($i = 0; $i -lt $Arguments.Count; $i++) {
        $shown += $Arguments[$i]
        if ($Arguments[$i] -eq '-gp' -and $i + 1 -lt $Arguments.Count) {
            $shown += '<hidden>'
            $i++
        }
    }
    return ($shown | ForEach-Object { if ($_ -match '\s') { "`"$_`"" } else { $_ } }) -join ' '
}

function Get-GuestArguments {
    if ($DryRun) { return @('-gu', '<user>', '-gp', '<hidden>') }
    if (-not $env:VSS_VM_USER -or -not $env:VSS_VM_PASSWORD) {
        throw 'set VSS_VM_USER and VSS_VM_PASSWORD for guest operations'
    }
    return @('-gu', $env:VSS_VM_USER, '-gp', $env:VSS_VM_PASSWORD)
}

function Invoke-Vmrun {
    # Runs vmrun and returns its output lines. Authentication flags go before
    # the command, as vmrun documents them.
    param(
        [string] $Command,
        [string[]] $Parameters = @(),
        [switch] $Guest
    )

    $arguments = @('-T', 'ws')
    if ($Guest) { $arguments += Get-GuestArguments }
    $arguments += $Command
    $arguments += $Parameters

    Write-Verbose ('vmrun ' + (Format-Arguments $arguments))
    if ($DryRun) {
        Write-Host ('vmrun ' + (Format-Arguments $arguments))
        return @()
    }

    $output = & $script:vmrun @arguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "vmrun $Command failed with exit code ${LASTEXITCODE}: $($output -join ' ')"
    }
    return @($output | ForEach-Object { "$_" })
}

function Assert-VssGuest {
    $leaf = Split-Path -Leaf $Vm
    if ($leaf -notmatch '^VSS[ -].*\.vmx$') {
        throw "refusing '$leaf': this script works on VSS guests only (file name starts with 'VSS ' or 'VSS-')"
    }
    if (-not $DryRun -and -not (Test-Path -LiteralPath $Vm)) {
        throw "no such guest: $Vm"
    }
}

function Get-GuestKind {
    if ($Kind -ne 'auto') { return $Kind }
    if (-not (Test-Path -LiteralPath $Vm)) {
        if ($DryRun) { return 'windows' }
        throw "no such guest: $Vm"
    }
    $line = Select-String -LiteralPath $Vm -Pattern '^guestOS\s*=\s*"(.*)"' |
        Select-Object -First 1
    if ($line -and $line.Matches[0].Groups[1].Value -match '^windows') { return 'windows' }
    return 'linux'
}

function Get-SnapshotNames {
    $lines = Invoke-Vmrun 'listSnapshots' @($Vm)
    return @($lines | Where-Object { $_ -notmatch '^Total snapshots' } |
        ForEach-Object { $_.Trim() } | Where-Object { $_ })
}

if ($Action -ne 'list') {
    if (-not $Vm) { throw "-Vm is required for $Action" }
    Assert-VssGuest
}

$script:vmrun = if ($DryRun) { 'vmrun' } else { Find-Vmrun }

switch ($Action) {
    'list' {
        $lines = Invoke-Vmrun 'list'
        $lines | Where-Object { $_ -match '^Total running' -or (Split-Path -Leaf $_) -match '^VSS[ -]' }
        exit 0
    }

    'status' {
        $running = Invoke-Vmrun 'list'
        if ($running | Where-Object { $_ -eq $Vm }) { 'running' } else { 'stopped' }
        exit 0
    }

    'start' {
        $mode = if ($Gui) { 'gui' } else { 'nogui' }
        Invoke-Vmrun 'start' @($Vm, $mode) | Out-Null
        exit 0
    }

    'stop' {
        Invoke-Vmrun 'stop' @($Vm, 'soft') | Out-Null
        exit 0
    }

    'snapshots' {
        Get-SnapshotNames
        exit 0
    }

    'snapshot' {
        if (-not $DryRun -and (Get-SnapshotNames) -contains $Name) {
            throw "snapshot '$Name' already exists; revert to it, or choose another name"
        }
        Invoke-Vmrun 'snapshot' @($Vm, $Name) | Out-Null
        exit 0
    }

    'revert' {
        if (-not $DryRun -and (Get-SnapshotNames) -notcontains $Name) {
            throw "snapshot '$Name' does not exist; this script does not create it"
        }
        Invoke-Vmrun 'revertToSnapshot' @($Vm, $Name) | Out-Null
        exit 0
    }

    'copy-to' {
        if (-not $Local -or -not $Guest) {
            throw '-Local and -Guest are required for copy-to'
        }
        Invoke-Vmrun 'copyFileFromHostToGuest' @($Vm, $Local, $Guest) -Guest | Out-Null
        exit 0
    }

    'copy-from' {
        if (-not $Local -or -not $Guest) {
            throw '-Local and -Guest are required for copy-from'
        }
        Invoke-Vmrun 'copyFileFromGuestToHost' @($Vm, $Guest, $Local) -Guest | Out-Null
        exit 0
    }

    'screenshot' {
        if (-not $Local) { throw '-Local is required for screenshot' }
        Invoke-Vmrun 'captureScreen' @($Vm, $Local) -Guest | Out-Null
        exit 0
    }

    'ip' {
        Invoke-Vmrun 'getGuestIPAddress' @($Vm, '-wait')
        exit 0
    }

    'wait-tools' {
        $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
        do {
            # No credentials here: with them vmrun keeps answering "installed" while Tools are running.
            $state = (Invoke-Vmrun 'checkToolsState' @($Vm)) -join ''
            if ($DryRun -or $state -match 'running') { exit 0 }
            Start-Sleep -Seconds 5
        } while ((Get-Date) -lt $deadline)
        throw "VMware Tools did not report 'running' within $TimeoutSeconds seconds (last state: $state)"
    }

    'wait-guest' {
        # Tools can report "running" before the guest accepts operations (the logon is still
        # under way), and copy-to then fails with "VMware Tools are not running in the guest".
        # Ask for something that needs the guest's answer, with the credentials.
        $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
        do {
            try {
                Invoke-Vmrun 'listProcessesInGuest' @($Vm) -Guest | Out-Null
                exit 0
            } catch {
                if ($DryRun) { exit 0 }
                $last = $_.Exception.Message
            }
            Start-Sleep -Seconds 5
        } while ((Get-Date) -lt $deadline)
        throw "the guest did not accept operations within $TimeoutSeconds seconds (last error: $last)"
    }

    'run' {
        if (-not $CommandLine) { throw '-CommandLine is required for run' }

        # vmrun reports only whether the guest program started, so the exit code goes through a marker
        # file. A Windows guest runs the command in PowerShell (cmd.exe returns 1 under vmrun in these
        # guests whatever it is asked to do), so on Windows the command line is PowerShell syntax; a
        # native program in it sets $LASTEXITCODE. A Linux guest runs it in bash.
        $family = Get-GuestKind
        if ($family -eq 'windows') {
            # Not the root of C:, which an ordinary user cannot write.
            $marker = 'C:\Users\Public\vss-exit.txt'
            $program = 'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe'
            $script = "$CommandLine; `$code = `$LASTEXITCODE; if (`$null -eq `$code) { `$code = if (`$?) { 0 } else { 1 } }; Set-Content -Path '$marker' -Value `$code"
            $arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command', $script)
        } else {
            $marker = '/tmp/vss-exit.txt'
            $program = '/bin/bash'
            $arguments = @('-c', "rm -f $marker; $CommandLine; echo `$? > $marker")
        }

        Invoke-Vmrun 'runProgramInGuest' (@($Vm, $program) + $arguments) -Guest | Out-Null
        if ($DryRun) { exit 0 }

        $local = Join-Path ([IO.Path]::GetTempPath()) ("vss-exit-" + [guid]::NewGuid().ToString('N') + '.txt')
        try {
            Invoke-Vmrun 'copyFileFromGuestToHost' @($Vm, $marker, $local) -Guest | Out-Null
            $text = (Get-Content -LiteralPath $local -Raw).Trim()
        } finally {
            Remove-Item -LiteralPath $local -ErrorAction SilentlyContinue
        }
        $code = 0
        if (-not [int]::TryParse($text, [ref] $code)) {
            throw "the guest wrote '$text' where an exit code was expected"
        }
        exit $code
    }
}
