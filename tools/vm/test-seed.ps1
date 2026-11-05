#!/usr/bin/env pwsh
#
# Copyright (c) 2026 Virtual Serial Studio contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
<#
.SYNOPSIS
    Tests tools/vm/new-seed.ps1 with placeholder credentials. No VM is involved.

.DESCRIPTION
    Builds the answer disc of each guest and reads it back: the volume label, the files, the
    well-formedness of the answer file, and that no @FIELD@ is left unfilled. Windows only,
    because the disc is built with IMAPI2.
#>

if (-not ($IsWindows -or $env:OS -eq 'Windows_NT')) {
    Write-Host 'skipped: the seed disc is built with IMAPI2, which is Windows only'
    exit 0
}

$ErrorActionPreference = 'Stop'
$script = Join-Path $PSScriptRoot 'new-seed.ps1'
$pwsh = (Get-Process -Id $PID).Path
$work = Join-Path ([IO.Path]::GetTempPath()) ("vss-seed-test-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null

$failures = @()
$count = 0
function Assert-That([string] $Label, [bool] $Condition, [string] $Detail = '') {
    $script:count++
    if ($Condition) { Write-Host "ok   $Label" }
    else { Write-Host "FAIL $Label $Detail"; $script:failures += $Label }
}

function Read-Disc([string] $Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    return [pscustomobject]@{
        Bytes = $bytes
        Text  = [Text.Encoding]::UTF8.GetString($bytes)
        Label = [Text.Encoding]::ASCII.GetString($bytes, 32808, 32).Trim()
    }
}

try {
    foreach ($guest in 'windows10', 'windows11', 'ubuntu2604') {
        $output = & $pwsh -NoProfile -File $script $guest -DryRun -OutDir $work 2>&1 | Out-String
        Assert-That "${guest}: the script succeeds" ($LASTEXITCODE -eq 0) $output
        Assert-That "${guest}: nothing secret is printed" ($output -notmatch 'placeholder-not-a-credential')

        $iso = Join-Path $work "$guest-seed.iso"
        Assert-That "${guest}: the disc exists" (Test-Path $iso)
        if (-not (Test-Path $iso)) { continue }
        $disc = Read-Disc $iso
        Assert-That "${guest}: no field is left unfilled" ($disc.Text -notmatch '@[A-Z_]{4,}@')

        if ($guest -like 'windows*') {
            Assert-That "${guest}: the volume is VSSSEED" ($disc.Label -eq 'VSSSEED') $disc.Label
            $start = $disc.Text.IndexOf('<?xml')
            $end = $disc.Text.IndexOf('</unattend>') + '</unattend>'.Length
            Assert-That "${guest}: the answer file is on the disc" ($start -ge 0 -and $end -gt $start)
            $document = $null
            try { $document = [xml]$disc.Text.Substring($start, $end - $start) } catch { }
            Assert-That "${guest}: the answer file is well formed" ($null -ne $document)
            Assert-That "${guest}: setup-guest.ps1 is on the disc" ($disc.Text.Contains('Prepares a Windows VSS guest'))
            Assert-That "${guest}: the account and the logon use the user" (([regex]::Matches($disc.Text, '<(Name|Username)>placeholder-user</')).Count -eq 2)
            $edition = if ($guest -eq 'windows11') { 'Windows 11 Pro' } else { 'Windows 10 Pro' }
            Assert-That "${guest}: the edition is $edition" ($disc.Text.Contains("<Value>$edition</Value>"))
            Assert-That "${guest}: the product key page is answered" ($disc.Text.Contains('<ProductKey>') -and $disc.Text -match '<Key>[A-Z0-9]{5}(-[A-Z0-9]{5}){4}</Key>')
            Assert-That "${guest}: the disk layout follows the firmware" (($guest -eq 'windows11') -eq $disc.Text.Contains('<Type>EFI</Type>'))
            Assert-That "${guest}: the TPM check is bypassed only on Windows 11" (($guest -eq 'windows11') -eq $disc.Text.Contains('BypassTPMCheck'))
        } else {
            Assert-That "${guest}: the volume is CIDATA" ($disc.Label -eq 'CIDATA') $disc.Label
            Assert-That "${guest}: user-data is on the disc" ($disc.Text.Contains('#cloud-config') -and $disc.Text.Contains('autoinstall:'))
            Assert-That "${guest}: meta-data is on the disc" ($disc.Text.Contains('instance-id: vss-ubuntu2604'))
            Assert-That "${guest}: the test tools are listed" ($disc.Text.Contains('python3-serial') -and $disc.Text.Contains('picocom'))
            Assert-That "${guest}: user-data has no carriage returns" (-not $disc.Text.Contains("`r"))
        }
    }

    $env:VSS_VM_USER = 'Bad User'
    $env:VSS_VM_PASSWORD = 'placeholder-not-a-credential'
    $output = & $pwsh -NoProfile -File $script windows10 -OutDir $work 2>&1 | Out-String
    Assert-That 'a user name with a space is refused' ($LASTEXITCODE -ne 0 -and $output -match 'lower-case user name')
    Remove-Item Env:VSS_VM_PASSWORD
    $env:VSS_VM_USER = 'vss'
    $output = & $pwsh -NoProfile -File $script windows10 -OutDir $work 2>&1 | Out-String
    Assert-That 'a missing password is refused' ($LASTEXITCODE -ne 0 -and $output -match 'VSS_VM_PASSWORD')
}
finally {
    Remove-Item -Recurse -Force -LiteralPath $work -ErrorAction SilentlyContinue
}

Write-Host ''
Write-Host "$($count - $failures.Count) of $count checks passed"
if ($failures.Count -gt 0) { exit 1 }
exit 0
