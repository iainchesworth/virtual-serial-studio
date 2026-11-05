#!/usr/bin/env pwsh
#
# Copyright (c) 2026 Virtual Serial Studio contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
<#
.SYNOPSIS
    Tests tools/vm/guest-cell.ps1 with -DryRun. No VM is involved and vmrun is not called.
#>

$ErrorActionPreference = 'Stop'
$script = Join-Path $PSScriptRoot 'guest-cell.ps1'
$pwsh = (Get-Process -Id $PID).Path
$failures = @()
$count = 0

function Assert-That([string] $Label, [bool] $Condition, [string] $Detail = '') {
    $script:count++
    if ($Condition) { Write-Host "ok   $Label" } else { Write-Host "FAIL $Label $Detail"; $script:failures += $Label }
}

function Invoke-Cell([string[]] $Arguments) {
    $output = & $pwsh -NoProfile -File $script @Arguments -DryRun 2>&1 | Out-String
    return [pscustomobject]@{ Code = $LASTEXITCODE; Output = $output }
}

$env:VSS_VM_USER = 'placeholder-user'
$env:VSS_VM_PASSWORD = 'placeholder-not-a-credential'

foreach ($guest in 'windows10', 'windows11', 'ubuntu2604') {
    $r = Invoke-Cell @('-Guest', $guest, '-Bundle', 'nowhere', '-Package', 'package.bin', '-Commit', 'abc1234')
    $lines = @($r.Output -split "`r?`n" | Where-Object { $_ -match '^vmrun ' })
    $verbs = @($lines | ForEach-Object { if ($_ -match ' (revertToSnapshot|start|checkToolsState|copyFileFromHostToGuest|runProgramInGuest|copyFileFromGuestToHost|stop) ') { $Matches[1] } })
    Assert-That "${guest}: a dry run succeeds" ($r.Code -eq 0) $r.Output
    Assert-That "${guest}: reverts first, then starts, waits and stops last" (
        $verbs[0] -eq 'revertToSnapshot' -and $verbs[1] -eq 'start' -and $verbs[2] -eq 'checkToolsState' -and $verbs[-1] -eq 'stop') ($verbs -join ' ')
    Assert-That "${guest}: copies in, runs and copies out" (
        ($verbs -contains 'copyFileFromHostToGuest') -and ($verbs -contains 'runProgramInGuest') -and ($verbs -contains 'copyFileFromGuestToHost'))
    Assert-That "${guest}: reverts to clean" ($r.Output -match 'revertToSnapshot .*clean')
    Assert-That "${guest}: names the VSS guest" ($r.Output -match [regex]::Escape("VSS - " + @{ windows10 = 'Windows 10'; windows11 = 'Windows 11'; ubuntu2604 = 'Ubuntu 26.04' }[$guest]))
    Assert-That "${guest}: the package is copied in" ($r.Output -match 'package\.bin')
    Assert-That "${guest}: the password is not printed" ($r.Output -notmatch 'placeholder-not-a-credential')
}

$r = Invoke-Cell @('-Guest', 'windows11', '-Bundle', 'nowhere')
Assert-That 'without a package, none is copied' ($r.Output -notmatch 'package\.')

$bad = & $pwsh -NoProfile -File $script -Guest 'Atmos Driver Test' -Bundle x -DryRun 2>&1 | Out-String
Assert-That 'a guest outside the list is refused' ($LASTEXITCODE -ne 0)

Write-Host ''
Write-Host "$($count - $failures.Count) of $count checks passed"
if ($failures.Count -gt 0) { exit 1 }
exit 0
