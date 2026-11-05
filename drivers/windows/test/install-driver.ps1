# Copyright (c) 2026 Virtual Serial Studio contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Installs the vsscom package and creates -Count ports (two by default). Run inside a guest, as an
# administrator, with the package (vsscom.sys, vsscom.inf, vsscom.cat) next to
# this script or in -PackageDir.
#
#   install-driver.ps1 [-PackageDir <dir>] [-ResultPath <json>] [-Count <n>]
#
# The package must be signed by something the guest trusts: a catalog signed by
# a certificate whose root is in LocalMachine\Root, with test signing on. The
# guest preparation is the VM harness's job, not this script's.
#
# Exit code 0 when a port appeared; 1 otherwise.

[CmdletBinding()]
param(
    [string] $PackageDir = $PSScriptRoot,
    [string] $ResultPath,
    [ValidateRange(1, 16)] [int] $Count = 2
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')

$tests = @()
try {
    $inf = Join-Path $PackageDir 'vsscom.inf'
    if (-not (Test-Path $inf)) { throw "vsscom.inf is not in $PackageDir" }

    # Put the package in the driver store first, so a signature problem is
    # reported here and not as an unexplained failure to bind.
    $store = & pnputil.exe /add-driver $inf 2>&1
    $tests += New-TestRecord 'install.store' 'pnputil /add-driver' ($(if ($LASTEXITCODE -eq 0) { 'pass' } else { 'fail' })) (($store | Out-String).Trim())
    if ($LASTEXITCODE -ne 0) { throw 'the package was not added to the driver store' }

    $reboot = $false
    for ($n = 0; $n -lt $Count; $n++) {
        if ([VssSetup]::CreateDevice($PortsClassGuid, $HardwareId, (Resolve-Path $inf).Path)) { $reboot = $true }
    }
    $tests += New-TestRecord 'install.device' "create $Count root devices and bind the driver" 'pass' "restart required: $reboot"

    $ports = @()
    for ($i = 0; $i -lt 30 -and $ports.Count -lt $Count; $i++) {
        Start-Sleep -Milliseconds 500
        $ports = @(Get-VssComPorts)
    }
    if ($ports.Count -ge $Count) {
        $tests += New-TestRecord 'install.port' 'the COM ports appeared' 'pass' ($ports -join ', ')
    } else {
        $tests += New-TestRecord 'install.port' 'the COM ports appeared' 'fail' "wanted $Count, found $($ports.Count): $($ports -join ', ')"
    }

    $hubs = Get-VssHubPaths
    $missing = @($ports | Where-Object { -not $hubs.ContainsKey($_) })
    if ($ports.Count -ge $Count -and $missing.Count -eq 0) {
        $tests += New-TestRecord 'install.hub' 'every port has a hub interface' 'pass' (($ports | ForEach-Object { "$_ -> " + $hubs[$_] }) -join '; ')
    } else {
        $tests += New-TestRecord 'install.hub' 'every port has a hub interface' 'fail' "no hub interface for: $($missing -join ', ')"
    }
} catch {
    $tests += New-TestRecord 'install.error' 'install' 'fail' $_.Exception.Message
}

Write-Result -Path $ResultPath -Phase 'install' -Tests $tests
$tests | ForEach-Object { '{0,-6} {1,-18} {2}' -f $_.status, $_.id, $_.detail }
if ($tests | Where-Object { $_.status -ne 'pass' }) { exit 1 }
exit 0
