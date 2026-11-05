# Copyright (c) 2026 Virtual Serial Studio contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Removes every vsscom port and the driver package, and checks nothing is left.
# Run inside a guest, as an administrator.
#
#   uninstall-driver.ps1 [-ResultPath <json>]
#
# Exit code 0 when no device, no COM name and no driver package remain.

[CmdletBinding()]
param(
    [string] $ResultPath
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')

$tests = @()
try {
    # Every COM name the driver has, to check afterwards that none is left.
    $names = @((Get-VssHubPaths).Keys)

    # The control device first: its ports go with it. Then the entries that
    # absent devices leave behind, which keep their COM names reserved.
    $removed = [VssSetup]::RemoveDevices($SystemClassGuid, $ControlHardwareId, $true)
    for ($i = 0; $i -lt 60 -and @([VssSetup]::Interfaces($VssInterfaceGuid) | Where-Object { $_ -like '*\vsscom#port#*' }).Count -gt 0; $i++) {
        Start-Sleep -Milliseconds 500
    }
    $removed += [VssSetup]::RemoveDevices($PortsClassGuid, $DemandHardwareId, $true)
    $removed += [VssSetup]::RemoveDevices($PortsClassGuid, $HardwareId, $true)
    $tests += New-TestRecord 'uninstall.device' 'remove the devices' 'pass' "removed $removed"

    # Find the packages in the driver store by their original names.
    $listing = (& pnputil.exe /enum-drivers | Out-String) -split "(\r?\n){2,}"
    $published = @()
    foreach ($block in $listing) {
        if ($block -match 'Original Name:\s+vsscom(ctl)?\.inf' -and $block -match 'Published Name:\s+(oem\d+\.inf)') {
            # The second -match replaced the first, so the group is number 1.
            $published += $Matches[1]
        }
    }
    foreach ($name in $published) {
        $out = & pnputil.exe /delete-driver $name /uninstall /force 2>&1
        $tests += New-TestRecord 'uninstall.package' "pnputil /delete-driver $name" ($(if ($LASTEXITCODE -eq 0) { 'pass' } else { 'fail' })) (($out | Out-String).Trim())
    }

    $left = Get-CimInstance Win32_PnPEntity |
        Where-Object { @($_.HardwareID | Where-Object { $_ -like 'VSSCOM\*' }).Count -gt 0 }
    $tests += New-TestRecord 'uninstall.devices_gone' 'no vsscom device remains' ($(if (-not $left) { 'pass' } else { 'fail' })) ($left.Name -join ', ')

    # The names must be off the legacy list too.
    $listed = @($names | Where-Object { Test-VssSerialComm $_ })
    $tests += New-TestRecord 'uninstall.serialcomm' "no COM name is left in SERIALCOMM ($($names -join ', '))" ($(if ($listed.Count -eq 0) { 'pass' } else { 'fail' })) ($listed -join ', ')
} catch {
    $tests += New-TestRecord 'uninstall.error' 'uninstall' 'fail' $_.Exception.Message
}

Write-Result -Path $ResultPath -Phase 'uninstall' -Tests $tests
$tests | ForEach-Object { '{0,-6} {1,-24} {2}' -f $_.status, $_.id, $_.detail }
if ($tests | Where-Object { $_.status -ne 'pass' }) { exit 1 }
exit 0
