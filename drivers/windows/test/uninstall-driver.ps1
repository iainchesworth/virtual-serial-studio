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
    $port = Get-VssComPort
    $removed = [VssSetup]::RemoveDevices($PortsClassGuid, $HardwareId)
    $tests += New-TestRecord 'uninstall.device' 'remove the devices' 'pass' "removed $removed"

    # Find the package in the driver store by its original name.
    $listing = (& pnputil.exe /enum-drivers | Out-String) -split "(\r?\n){2,}"
    $published = @()
    foreach ($block in $listing) {
        if ($block -match 'Original Name:\s+vsscom\.inf' -and $block -match 'Published Name:\s+(oem\d+\.inf)') {
            $published += $Matches[1]
        }
    }
    foreach ($name in $published) {
        $out = & pnputil.exe /delete-driver $name /uninstall /force 2>&1
        $tests += New-TestRecord 'uninstall.package' "pnputil /delete-driver $name" ($(if ($LASTEXITCODE -eq 0) { 'pass' } else { 'fail' })) (($out | Out-String).Trim())
    }

    $left = Get-CimInstance Win32_PnPEntity | Where-Object { $_.HardwareID -contains $HardwareId }
    $tests += New-TestRecord 'uninstall.devices_gone' 'no vsscom device remains' ($(if (-not $left) { 'pass' } else { 'fail' })) ($left.Name -join ', ')

    # The port's name must be off the legacy list too.
    if ($port) {
        $listed = @()
        $serialComm = Get-ItemProperty 'HKLM:\HARDWARE\DEVICEMAP\SERIALCOMM' -ErrorAction SilentlyContinue
        if ($serialComm) {
            $listed = $serialComm.PSObject.Properties |
                Where-Object { $_.Value -eq $port } | ForEach-Object { $_.Name }
        }
        $tests += New-TestRecord 'uninstall.serialcomm' "$port is not in SERIALCOMM" ($(if (-not $listed) { 'pass' } else { 'fail' })) ($listed -join ', ')
    }
} catch {
    $tests += New-TestRecord 'uninstall.error' 'uninstall' 'fail' $_.Exception.Message
}

Write-Result -Path $ResultPath -Phase 'uninstall' -Tests $tests
$tests | ForEach-Object { '{0,-6} {1,-24} {2}' -f $_.status, $_.id, $_.detail }
if ($tests | Where-Object { $_.status -ne 'pass' }) { exit 1 }
exit 0
