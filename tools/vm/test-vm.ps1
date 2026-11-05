#!/usr/bin/env pwsh
#
# Copyright (c) 2026 Virtual Serial Studio contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
<#
.SYNOPSIS
    Tests tools/vm/new-vm.ps1 in its dry-run mode. No disk, no VM and no ISO is involved.
#>

$ErrorActionPreference = 'Stop'
$script = Join-Path $PSScriptRoot 'new-vm.ps1'
$pwsh = (Get-Process -Id $PID).Path
$work = Join-Path ([IO.Path]::GetTempPath()) ("vss-vm-test-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null

$failures = @()
$count = 0
function Assert-That([string] $Label, [bool] $Condition, [string] $Detail = '') {
    $script:count++
    if ($Condition) { Write-Host "ok   $Label" }
    else { Write-Host "FAIL $Label $Detail"; $script:failures += $Label }
}

function Read-Vmx([string] $Path) {
    $table = @{}
    foreach ($line in Get-Content -LiteralPath $Path) {
        if ($line -match '^(\S+)\s*=\s*"(.*)"$') { $table[$Matches[1]] = $Matches[2] }
    }
    return $table
}

try {
    $expected = @{
        windows10  = @{ Name = 'VSS - Windows 10'; Os = 'windows9-64'; Efi = $true; Cpus = '2'; Memory = '4096' }
        windows11  = @{ Name = 'VSS - Windows 11'; Os = 'windows11-64'; Efi = $true; Cpus = '4'; Memory = '8192' }
        ubuntu2604 = @{ Name = 'VSS - Ubuntu 26.04'; Os = 'ubuntu-64'; Efi = $false; Cpus = '2'; Memory = '4096' }
    }
    foreach ($guest in $expected.Keys) {
        $want = $expected[$guest]
        $output = & $pwsh -NoProfile -File $script $guest -DryRun -Root $work 2>&1 | Out-String
        Assert-That "${guest}: the script succeeds" ($LASTEXITCODE -eq 0) $output
        $vmx = Join-Path (Join-Path $work $want.Name) "$($want.Name).vmx"
        Assert-That "${guest}: the VMX is named for the guest" (Test-Path -LiteralPath $vmx)
        if (-not (Test-Path -LiteralPath $vmx)) { continue }
        $v = Read-Vmx $vmx
        Assert-That "${guest}: display name, system, CPUs and memory" ($v['displayName'] -eq $want.Name -and $v['guestOS'] -eq $want.Os -and $v['numvcpus'] -eq $want.Cpus -and $v['memsize'] -eq $want.Memory)
        Assert-That "${guest}: it boots the disk first" ($v['bios.bootOrder'] -eq 'hdd,cdrom')
        Assert-That "${guest}: firmware follows the matrix" (($v['firmware'] -eq 'efi') -eq $want.Efi)
        Assert-That "${guest}: Secure Boot is off where there is UEFI" (-not $want.Efi -or $v['uefi.secureBoot.enabled'] -eq 'FALSE')
        Assert-That "${guest}: there is no virtual TPM and no encryption" (-not ($v.Keys | Where-Object { $_ -match 'vtpm|encryption' }))
        Assert-That "${guest}: the install disc and the answer disc are attached" ($v['sata0:1.fileName'] -like '*.iso' -and $v['sata0:2.fileName'] -like '*-seed.iso')
        Assert-That "${guest}: it uses NAT" ($v['ethernet0.connectionType'] -eq 'nat')
        Assert-That "${guest}: the guests that wait for a key boot a patched ISO" (($guest -ne 'windows11') -eq ($v['sata0:1.fileName'] -like '*-autoinstall.iso' -or $v['sata0:1.fileName'] -like '*-noprompt.iso' -and $v['sata0:1.fileName'] -notlike '*v2-noprompt.iso'))
        Assert-That "${guest}: the PCIe root ports are present" ($v['pciBridge0.present'] -eq 'TRUE' -and (4..7 | Where-Object { $v["pciBridge$_.virtualDev"] -eq 'pcieRootPort' -and $v["pciBridge$_.functions"] -eq '8' }).Count -eq 4)

        Assert-That "${guest}: the script refuses to overwrite a guest" ($(& $pwsh -NoProfile -File $script $guest -DryRun -Root $work 2>&1 | Out-String) -match 'never overwrites')

        & $pwsh -NoProfile -File $script $guest -Root $work -DetachMedia 2>&1 | Out-Null
        $v = Read-Vmx $vmx
        Assert-That "${guest}: DetachMedia disconnects both discs" ($v['sata0:1.present'] -eq 'FALSE' -and $v['sata0:2.present'] -eq 'FALSE')
        Assert-That "${guest}: DetachMedia leaves the disk" ($v['displayName'] -eq $want.Name -and ($v['sata0:0.fileName'] -or $v['scsi0:0.fileName']))
    }

    Assert-That 'both Windows guests put the disk on SATA' (
        (Read-Vmx (Join-Path (Join-Path $work 'VSS - Windows 10') 'VSS - Windows 10.vmx')).ContainsKey('sata0:0.fileName') -and
        (Read-Vmx (Join-Path (Join-Path $work 'VSS - Windows 11') 'VSS - Windows 11.vmx')).ContainsKey('sata0:0.fileName'))
}
finally {
    Remove-Item -Recurse -Force -LiteralPath $work -ErrorAction SilentlyContinue
}

Write-Host ''
Write-Host "$($count - $failures.Count) of $count checks passed"
if ($failures.Count -gt 0) { exit 1 }
exit 0
