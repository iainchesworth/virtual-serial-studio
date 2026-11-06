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
    $verbs = @($lines | ForEach-Object { if ($_ -match ' (revertToSnapshot|start|listProcessesInGuest|copyFileFromHostToGuest|runProgramInGuest|copyFileFromGuestToHost|stop) ') { $Matches[1] } })
    Assert-That "${guest}: a dry run succeeds" ($r.Code -eq 0) $r.Output
    Assert-That "${guest}: reverts first, then starts, waits and stops last" (
        $verbs[0] -eq 'revertToSnapshot' -and $verbs[1] -eq 'start' -and $verbs[2] -eq 'listProcessesInGuest' -and $verbs[-1] -eq 'stop') ($verbs -join ' ')
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

# The driver tests: a Windows guest runs them from the clean snapshot, with or without a cell.
foreach ($guest in 'windows10', 'windows11') {
    $r = Invoke-Cell @('-Guest', $guest, '-DriverBundle', 'driver-bundle.zip')
    $lines = @($r.Output -split "`r?`n" | Where-Object { $_ -match '^vmrun ' })
    $verbs = @($lines | ForEach-Object { if ($_ -match ' (revertToSnapshot|start|listProcessesInGuest|copyFileFromHostToGuest|runProgramInGuest|copyFileFromGuestToHost|stop) ') { $Matches[1] } })
    Assert-That "${guest}: a driver-only dry run succeeds" ($r.Code -eq 0) $r.Output
    Assert-That "${guest}: a driver-only run reverts first and stops last" (
        $verbs[0] -eq 'revertToSnapshot' -and $verbs[-1] -eq 'stop') ($verbs -join ' ')
    Assert-That "${guest}: the driver bundle and its script are copied in" (
        $r.Output -match 'driver-bundle\.zip' -and $r.Output -match 'win-driver\.ps1')
    Assert-That "${guest}: a driver-only run copies no cell bundle" (
        $r.Output -notmatch 'vss\\bundle\.zip' -and $r.Output -notmatch 'win-cell\.ps1')
    Assert-That "${guest}: both drivers run by default, without the suite" (
        $r.Output -match "win-driver\.ps1 -Flavour 'kmdf','umdf' " -and $r.Output -notmatch ' -Suite')
    Assert-That "${guest}: the driver evidence is copied back" ($r.Output -match 'driver-out\.zip')
    Assert-That "${guest}: the password is not printed" ($r.Output -notmatch 'placeholder-not-a-credential')
}

$r = Invoke-Cell @('-Guest', 'windows11', '-DriverBundle', 'driver-bundle.zip', '-DriverFlavour', 'umdf', '-DriverSuite')
Assert-That 'one driver and the suite can be asked for' ($r.Output -match "win-driver\.ps1 -Flavour 'umdf' -Suite")

$r = Invoke-Cell @('-Guest', 'windows11', '-Bundle', 'nowhere', '-DriverBundle', 'driver-bundle.zip')
$cellAt = $r.Output.IndexOf('win-cell.ps1 -Cell')
$driverAt = $r.Output.IndexOf('win-driver.ps1 -Flavour')
Assert-That 'with both bundles the cell runs first, then the driver tests' ($cellAt -ge 0 -and $driverAt -gt $cellAt)

$bad = & $pwsh -NoProfile -File $script -Guest ubuntu2604 -DriverBundle x.zip -DryRun 2>&1 | Out-String
Assert-That 'a driver bundle for the Ubuntu guest is refused' ($LASTEXITCODE -ne 0)
$bad = & $pwsh -NoProfile -File $script -Guest windows11 -DryRun 2>&1 | Out-String
Assert-That 'a run with neither bundle is refused' ($LASTEXITCODE -ne 0)

Write-Host ''
Write-Host "$($count - $failures.Count) of $count checks passed"
if ($failures.Count -gt 0) { exit 1 }
exit 0
