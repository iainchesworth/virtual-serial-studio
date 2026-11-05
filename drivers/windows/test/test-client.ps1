# Copyright (c) 2026 Virtual Serial Studio contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Runs the studio's own client against the installed driver: the one unit test
# that needs a driver (create_driver_port, the transport, the lines, and the
# port going when the transport closes). Run inside a guest, as an administrator,
# after install-driver.ps1, with a vss_unit_tests.exe built for Windows next to
# this script or named by -Exe. A static release build has no runtime to install.
#
#   test-client.ps1 [-Exe <vss_unit_tests.exe>] [-ResultPath <json>]
#
# Exit code 0 when the case ran and passed; a skipped case fails here, because it
# means the driver was not seen.

[CmdletBinding()]
param(
    [string] $Exe = (Join-Path $PSScriptRoot 'vss_unit_tests.exe'),
    [string] $ResultPath
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')

$tests = @()
try {
    if (-not (Test-Path $Exe)) { throw "$Exe is not there" }
    $env:VSS_TEST_DRIVER = '1'
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $output = (& $Exe '--filter=against the real driver' 2>&1 | Out-String).Trim()
    $code = $LASTEXITCODE
    $detail = ($output -split "\r?\n" | Select-Object -Last 6) -join '; '
    $ran = $output -match '1 tests?, 1 passed, 0 failed, 0 skipped'
    $status = if ($code -eq 0 -and $ran) { 'pass' } else { 'fail' }
    $tests += New-TestRecord 'C1' 'the studio client makes a port, carries bytes both ways and removes it' $status "$detail ($($watch.Elapsed.TotalSeconds.ToString('0.0')) s)"
} catch {
    $tests += New-TestRecord 'C1' 'the studio client against the driver' 'fail' $_.Exception.Message
}

Write-Result -Path $ResultPath -Phase 'client' -Tests $tests
$tests | ForEach-Object { '{0,-6} {1,-4} {2}' -f $_.status, $_.id, $_.detail }
if ($tests | Where-Object { $_.status -ne 'pass' }) { exit 1 }
exit 0
