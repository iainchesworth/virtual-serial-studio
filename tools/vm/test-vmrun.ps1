#!/usr/bin/env pwsh
#
# Copyright (c) 2026 Virtual Serial Studio contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
<#
.SYNOPSIS
    Tests tools/vm/vmrun.ps1 against a stand-in for vmrun. No VM is involved.

.DESCRIPTION
    The stand-in records its arguments and answers the few commands the script
    uses. The password in these tests is a placeholder, not a credential.
#>

$ErrorActionPreference = 'Stop'

$here = $PSScriptRoot
$vmrun = Join-Path $here 'vmrun.ps1'
$pwsh = (Get-Process -Id $PID).Path
$work = Join-Path ([IO.Path]::GetTempPath()) ("vss-vmrun-test-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null

$failures = @()
$count = 0

function Assert-That([string] $Label, [bool] $Condition, [string] $Detail = '') {
    $script:count++
    if ($Condition) {
        Write-Host "ok   $Label"
    } else {
        Write-Host "FAIL $Label $Detail"
        $script:failures += $Label
    }
}

try {
    # The stand-in for vmrun.
    $log = Join-Path $work 'calls.log'
    $stub = Join-Path $work 'fake-vmrun.ps1'
    Set-Content -LiteralPath $stub -Value @'
param()
Add-Content -LiteralPath $env:FAKE_LOG -Value ($args -join ' ')
$command = $args | Where-Object { $_ -in 'list','listSnapshots','snapshot','revertToSnapshot','start','stop','copyFileFromGuestToHost','copyFileFromHostToGuest','runProgramInGuest','checkToolsState','getGuestIPAddress','captureScreen' } | Select-Object -First 1
switch ($command) {
    'list' { 'Total running VMs: 0' }
    'listSnapshots' {
        $names = @($env:FAKE_SNAPSHOTS -split ',' | Where-Object { $_ })
        "Total snapshots: $($names.Count)"
        $names
    }
    'copyFileFromGuestToHost' {
        Set-Content -LiteralPath $args[-1] -Value $env:FAKE_MARKER
    }
    'checkToolsState' { 'running' }
}
exit 0
'@
    if ($IsWindows -or $env:OS -eq 'Windows_NT') {
        $wrapper = Join-Path $work 'fake-vmrun.cmd'
        Set-Content -LiteralPath $wrapper -Value "@`"$pwsh`" -NoProfile -File `"$stub`" %*" -Encoding ascii
    } else {
        $wrapper = Join-Path $work 'fake-vmrun.sh'
        Set-Content -LiteralPath $wrapper -Value "#!/bin/sh`nexec `"$pwsh`" -NoProfile -File `"$stub`" `"`$@`"`n"
        chmod +x $wrapper
    }

    $guestDir = Join-Path $work 'VSS - Test'
    New-Item -ItemType Directory -Path $guestDir | Out-Null
    $vm = Join-Path $guestDir 'VSS - Test.vmx'
    Set-Content -LiteralPath $vm -Value 'guestOS = "windows9-64"'
    $linuxVm = Join-Path $guestDir 'VSS-Linux.vmx'
    Set-Content -LiteralPath $linuxVm -Value 'guestOS = "ubuntu-64"'
    $foreign = Join-Path $work 'Atmos Driver Test.vmx'
    Set-Content -LiteralPath $foreign -Value 'guestOS = "windows11-64"'

    $env:FAKE_LOG = $log
    $env:VSS_VMRUN = $wrapper
    $env:VSS_VM_USER = 'placeholder-user'
    $env:VSS_VM_PASSWORD = 'placeholder-not-a-credential'
    $env:FAKE_SNAPSHOTS = 'clean'
    $env:FAKE_MARKER = '0'

    function Invoke-Script([string[]] $Arguments) {
        $output = & $pwsh -NoProfile -File $vmrun @Arguments 2>&1 | Out-String
        return [pscustomobject]@{ Code = $LASTEXITCODE; Output = $output }
    }
    function Reset-Log { if (Test-Path $log) { Remove-Item $log } }

    # 1. Other guests are refused, and vmrun is not called.
    Reset-Log
    $r = Invoke-Script @('start', '-Vm', $foreign)
    Assert-That 'a guest that is not a VSS guest is refused' ($r.Code -ne 0 -and $r.Output -match 'VSS guests only')
    Assert-That 'vmrun is not called for a refused guest' (-not (Test-Path $log))

    # 2. The password never reaches the output, in a dry run or with -Verbose.
    $r = Invoke-Script @('run', '-Vm', $vm, '-CommandLine', 'echo hi', '-DryRun')
    Assert-That 'a dry run succeeds' ($r.Code -eq 0)
    Assert-That 'a dry run hides the password' ($r.Output -notmatch 'placeholder-not-a-credential' -and $r.Output -match '<hidden>')
    Reset-Log
    $r = Invoke-Script @('copy-to', '-Vm', $vm, '-Local', $stub, '-Guest', 'C:\vss\x', '-Verbose')
    Assert-That 'verbose output hides the password' ($r.Output -notmatch 'placeholder-not-a-credential')
    Assert-That 'vmrun receives the credentials before the command' ((Get-Content $log -Raw) -match '^-T ws -gu placeholder-user -gp placeholder-not-a-credential copyFileFromHostToGuest')

    # 3. The exit code of the guest command is the exit code of the script.
    foreach ($code in 0, 7, 255) {
        $env:FAKE_MARKER = "$code "
        $r = Invoke-Script @('run', '-Vm', $vm, '-CommandLine', 'whatever')
        Assert-That "run returns the guest exit code $code" ($r.Code -eq $code) "got $($r.Code): $($r.Output)"
    }
    $env:FAKE_MARKER = 'not a number'
    $r = Invoke-Script @('run', '-Vm', $vm, '-CommandLine', 'whatever')
    Assert-That 'run fails when the guest reports no exit code' ($r.Code -ne 0)
    $env:FAKE_MARKER = '0'

    # 4. The shell follows the guest family.
    Reset-Log
    $null = Invoke-Script @('run', '-Vm', $vm, '-CommandLine', 'ver')
    Assert-That 'a Windows guest runs cmd with delayed expansion' ((Get-Content $log -Raw) -match 'cmd\.exe /v:on /c')
    Reset-Log
    $null = Invoke-Script @('run', '-Vm', $linuxVm, '-CommandLine', 'uname')
    Assert-That 'a Linux guest runs bash' ((Get-Content $log -Raw) -match '/bin/bash -c')

    # 5. Snapshots are never created behind the caller's back.
    Reset-Log
    $r = Invoke-Script @('revert', '-Vm', $vm, '-Name', 'missing')
    Assert-That 'revert to a missing snapshot fails' ($r.Code -ne 0)
    Assert-That 'revert does not create the snapshot' (-not ((Get-Content $log -Raw) -match ' snapshot '))
    $r = Invoke-Script @('snapshot', '-Vm', $vm, '-Name', 'clean')
    Assert-That 'snapshot with an existing name fails' ($r.Code -ne 0)
    Reset-Log
    $r = Invoke-Script @('snapshot', '-Vm', $vm, '-Name', 'later')
    Assert-That 'snapshot with a new name succeeds' ($r.Code -eq 0 -and (Get-Content $log -Raw) -match ' snapshot ')
    $r = Invoke-Script @('revert', '-Vm', $vm)
    Assert-That 'revert to clean succeeds' ($r.Code -eq 0)

    # 6. Guest actions need the credentials.
    Remove-Item Env:VSS_VM_PASSWORD
    $r = Invoke-Script @('copy-to', '-Vm', $vm, '-Local', $stub, '-Guest', 'C:\x')
    Assert-That 'a guest action without credentials fails' ($r.Code -ne 0 -and $r.Output -match 'VSS_VM_PASSWORD')
    $r = Invoke-Script @('snapshots', '-Vm', $vm)
    Assert-That 'a host action needs no credentials' ($r.Code -eq 0 -and $r.Output -match 'clean')
}
finally {
    Remove-Item -Recurse -Force -LiteralPath $work -ErrorAction SilentlyContinue
}

Write-Host ''
Write-Host "$($count - $failures.Count) of $count checks passed"
if ($failures.Count -gt 0) { exit 1 }
exit 0
