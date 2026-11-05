# Copyright (c) 2026 Virtual Serial Studio contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Installs the vsscom package, creates the control device and -Count ports made by
# hand (two by default). Run inside a guest, as an administrator, with the package
# (vsscom.sys, vsscom.inf, vsscomctl.inf and their catalogs) next to this script or
# in -PackageDir.
#
#   install-driver.ps1 [-PackageDir <dir>] [-ResultPath <json>] [-Count <n>] [-Flavour kmdf|umdf]
#
# With -Flavour umdf it installs the user-mode driver (vsscom_um.dll, vsscom_um.inf
# and its catalog) and creates its ports, and no control device, which that driver
# does not have.
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
    [ValidateRange(1, 16)] [int] $Count = 2,
    [ValidateSet('kmdf', 'umdf')] [string] $Flavour
)

$ErrorActionPreference = 'Stop'
if ($Flavour) { $env:VSS_DRIVER = $Flavour }
. (Join-Path $PSScriptRoot 'common.ps1')

$tests = @()
try {
    $inf = Join-Path $PackageDir $InfName
    $ctlInf = Join-Path $PackageDir 'vsscomctl.inf'
    $packages = if ($Flavour -eq 'umdf' -or $env:VSS_DRIVER -eq 'umdf') { @($inf) } else { @($inf, $ctlInf) }
    foreach ($file in $packages) {
        if (-not (Test-Path $file)) { throw "$(Split-Path -Leaf $file) is not in $PackageDir" }
    }

    # Put the packages in the driver store first, so a signature problem is
    # reported here and not as an unexplained failure to bind, and so that the
    # ports the control device makes later find their driver.
    $store = @()
    $storeOk = $true
    foreach ($file in $packages) {
        $store += & pnputil.exe /add-driver $file 2>&1
        if ($LASTEXITCODE -ne 0) { $storeOk = $false }
    }
    $tests += New-TestRecord 'install.store' 'pnputil /add-driver' ($(if ($storeOk) { 'pass' } else { 'fail' })) (($store | Out-String).Trim())
    if (-not $storeOk) { throw 'the package was not added to the driver store' }

    $reboot = $false
    for ($n = 0; $n -lt $Count; $n++) {
        if ([VssSetup]::CreateDevice($PortsClassGuid, $HardwareId, (Resolve-Path $inf).Path, 'Ports')) { $reboot = $true }
    }
    $tests += New-TestRecord 'install.device' "create $Count root devices and bind the driver" 'pass' "restart required: $reboot"

    # The control device, once. The user-mode driver has none.
    if ($Flavour -ne 'umdf' -and $env:VSS_DRIVER -ne 'umdf') {
        if ([VssSetup]::CreateDevice($SystemClassGuid, $ControlHardwareId, (Resolve-Path $ctlInf).Path, 'VSSCOMCTL')) { $reboot = $true }
        $control = $null
        for ($i = 0; $i -lt 30 -and -not $control; $i++) {
            $control = Get-VssControlPath
            if (-not $control) { Start-Sleep -Milliseconds 500 }
        }
        $tests += New-TestRecord 'install.control' 'the control device has an interface' ($(if ($control) { 'pass' } else { 'fail' })) $control
    }

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
