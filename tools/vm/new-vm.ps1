#!/usr/bin/env pwsh
#
# Copyright (c) 2026 Virtual Serial Studio contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
<#
.SYNOPSIS
    Creates a VSS guest from its install ISO and the answer disc made by new-seed.ps1.

.DESCRIPTION
    Writes the disk and the VMX only; it does not start the guest. The guest installs
    unattended on its first start and powers itself off when it is ready. It never touches
    an existing folder: if the guest's folder exists, the script stops.

    Steps for one guest:
        ./tools/vm/new-seed.ps1 -Guest windows11          # your shell, with the credentials
        ./tools/vm/new-vm.ps1   -Guest windows11
        vmrun -T ws start "<vmx>" nogui                    # or gui, to watch
        ... the guest installs and powers off ...
        ./tools/vm/new-vm.ps1   -Guest windows11 -DetachMedia
        ./tools/vm/vmrun.ps1 snapshot -Vm "<vmx>" -Name clean

.PARAMETER Guest
    The id of a guest in matrix.json: windows10, windows11 or ubuntu2604.

.PARAMETER DetachMedia
    Disconnect the install ISO and the answer disc from an installed guest. The guest must be
    powered off.

.PARAMETER DryRun
    Write the VMX under -Root and make no disk and no hash check. For the tests.
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory, Position = 0)]
    [ValidateSet('windows10', 'windows11', 'ubuntu2604')]
    [string] $Guest,

    [string] $IsoDir = 'E:\ISOs',
    [string] $SeedDir = (Join-Path $HOME '.vss/seeds'),
    [string] $Root,
    [string] $ToolsIso = 'C:\Program Files\VMware\VMware Workstation\windows.iso',
    [switch] $DetachMedia,
    [switch] $SkipHash,
    [switch] $DryRun
)

$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$matrix = Get-Content (Join-Path $here 'matrix.json') -Raw | ConvertFrom-Json
$entry = $matrix.guests | Where-Object { $_.id -eq $Guest }
if (-not $entry) { throw "no guest '$Guest' in matrix.json" }
if (-not $Root) { $Root = $matrix.root }

$name = $entry.name
if ($name -notmatch '^VSS[ -]') { throw "refusing '$name': a VSS guest name starts with 'VSS '" }
# Path.Combine does not check that the drive exists, which Join-Path does.
$directory = [IO.Path]::Combine($Root, $name)
$vmx = [IO.Path]::Combine($directory, "$name.vmx")

if ($DetachMedia) {
    if (-not (Test-Path -LiteralPath $vmx)) { throw "no such guest: $vmx" }
    $lines = Get-Content -LiteralPath $vmx
    $out = foreach ($line in $lines) {
        if ($line -match '^(sata0:[12])\.present\s*=') { "$($Matches[1]).present = `"FALSE`"" } else { $line }
    }
    Set-Content -LiteralPath $vmx -Value $out -Encoding ascii
    Write-Host "disconnected the install disc and the answer disc of $name"
    return
}

if (Test-Path -LiteralPath $directory) {
    throw "'$directory' exists; this script never overwrites a guest. Delete the guest first, from the approved list."
}

$iso = [IO.Path]::Combine($IsoDir, $entry.iso)
$seed = [IO.Path]::Combine($SeedDir, "$Guest-seed.iso")
if (-not $DryRun) {
    if (-not (Test-Path -LiteralPath $iso)) { throw "install ISO not found: $iso" }
    if (-not (Test-Path -LiteralPath $seed)) { throw "answer disc not found: $seed (run new-seed.ps1)" }

    if (-not $SkipHash) {
        $known = (Get-Content (Join-Path $here 'iso-hashes.json') -Raw | ConvertFrom-Json).isos
        $expected = $known.PSObject.Properties[$entry.iso].Value
        if ($expected) {
            Write-Host "checking $($entry.iso) ..."
            $actual = (Get-FileHash -LiteralPath $iso -Algorithm SHA256).Hash.ToLowerInvariant()
            if ($actual -ne $expected) { throw "SHA-256 of $iso is $actual, expected $expected" }
        } else {
            Write-Warning "no known SHA-256 for $($entry.iso)"
        }
    }
}

# Two installers wait for a key that nobody can press at a test guest's console. Subiquity asks
# "Continue with autoinstall?" unless the boot line says so, and the Windows 10 media ask "Press any key
# to boot from CD". A patched copy of the ISO answers each. The copies are large, so they go next to
# the VMs, outside the repository.
$isoCache = [IO.Path]::Combine($Root, '_iso')
function Get-PatchedIso([string] $Suffix, [string] $Script) {
    $patched = [IO.Path]::Combine($isoCache, [IO.Path]::GetFileNameWithoutExtension($entry.iso) + $Suffix)
    if (-not $DryRun -and -not (Test-Path -LiteralPath $patched)) {
        New-Item -ItemType Directory -Force -Path $isoCache | Out-Null
        $python = (Get-Command python3, python -ErrorAction SilentlyContinue | Select-Object -First 1).Source
        if (-not $python) { throw "python is needed to patch $($entry.iso)" }
        & $python (Join-Path $here $Script) --iso $iso --out $patched
        if ($LASTEXITCODE -ne 0) { throw "patching $($entry.iso) failed: $LASTEXITCODE" }
    }
    return $patched
}
if ($entry.PSObject.Properties['autoinstall_iso'] -and $entry.autoinstall_iso) {
    $iso = Get-PatchedIso '-autoinstall.iso' 'patch_ubuntu_iso.py'
}
if ($entry.PSObject.Properties['noprompt_iso'] -and $entry.noprompt_iso) {
    $iso = Get-PatchedIso '-noprompt.iso' 'patch_windows_iso.py'
}

$windows = $entry.kind -eq 'windows'
$efi = $entry.firmware -eq 'efi'
$guestOs = switch ($Guest) { 'windows10' { 'windows9-64' } 'windows11' { 'windows11-64' } default { 'ubuntu-64' } }
$diskFile = "$name.vmdk"

$lines = @(
    '.encoding = "UTF-8"',
    'config.version = "8"',
    'virtualHW.version = "21"',
    "displayName = `"$name`"",
    "guestOS = `"$guestOs`"",
    "numvcpus = `"$($entry.cpus)`"",
    "memsize = `"$($entry.memory_mb)`"",
    'bios.bootOrder = "hdd,cdrom"',
    'tools.syncTime = "TRUE"',
    'mks.enable3d = "FALSE"',
    'floppy0.present = "FALSE"',
    'usb.present = "TRUE"',
    'usb_xhci.present = "TRUE"',
    'ethernet0.present = "TRUE"',
    'ethernet0.connectionType = "nat"',
    'ethernet0.addressType = "generated"',
    'sound.present = "FALSE"',
    'uuid.action = "create"',
    # The PCIe root ports and the host bridge. VMware adds these when it makes a VM itself;
    # without them, a hand-written VMX stops at power-on with "No PCIe slot available".
    'pciBridge0.present = "TRUE"',
    'hpet0.present = "TRUE"',
    'vmci0.present = "TRUE"'
)
foreach ($port in 4..7) {
    $lines += "pciBridge$port.present = `"TRUE`"", "pciBridge$port.virtualDev = `"pcieRootPort`"", "pciBridge$port.functions = `"8`""
}
if ($efi) {
    $lines += 'firmware = "efi"'
    $lines += 'uefi.secureBoot.enabled = "FALSE"'
}
if ($windows) {
    $lines += 'ethernet0.virtualDev = "e1000e"'
    # SATA for both Windows guests. The virtual NVMe disk stalled Windows Setup at 65 percent of the
    # image apply, with the controller reset in the VMware log, on every try.
    $lines += 'sata0:0.present = "TRUE"', "sata0:0.fileName = `"$diskFile`""
    $lines += 'sata0.present = "TRUE"'
    $lines += 'sata0:1.present = "TRUE"', 'sata0:1.deviceType = "cdrom-image"', "sata0:1.fileName = `"$iso`"", 'sata0:1.startConnected = "TRUE"'
    $lines += 'sata0:2.present = "TRUE"', 'sata0:2.deviceType = "cdrom-image"', "sata0:2.fileName = `"$seed`"", 'sata0:2.startConnected = "TRUE"'
    $lines += 'sata0:3.present = "TRUE"', 'sata0:3.deviceType = "cdrom-image"', "sata0:3.fileName = `"$ToolsIso`"", 'sata0:3.startConnected = "TRUE"'
} else {
    $lines += 'ethernet0.virtualDev = "e1000"'
    $lines += 'scsi0.present = "TRUE"', 'scsi0.virtualDev = "lsilogic"', 'scsi0:0.present = "TRUE"', "scsi0:0.fileName = `"$diskFile`""
    $lines += 'sata0.present = "TRUE"'
    $lines += 'sata0:1.present = "TRUE"', 'sata0:1.deviceType = "cdrom-image"', "sata0:1.fileName = `"$iso`"", 'sata0:1.startConnected = "TRUE"'
    $lines += 'sata0:2.present = "TRUE"', 'sata0:2.deviceType = "cdrom-image"', "sata0:2.fileName = `"$seed`"", 'sata0:2.startConnected = "TRUE"'
}

New-Item -ItemType Directory -Path $directory | Out-Null
if (-not $DryRun) {
    $vdisk = 'C:\Program Files\VMware\VMware Workstation\vmware-vdiskmanager.exe'
    if (-not (Test-Path $vdisk)) { throw "vmware-vdiskmanager not found at $vdisk" }
    # A growable single file, so the disk takes only what the guest uses.
    & $vdisk -c -s "$($entry.disk_gb)GB" -a lsilogic -t 0 (Join-Path $directory $diskFile) | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "disk creation failed: $LASTEXITCODE" }
}
Set-Content -LiteralPath $vmx -Value $lines -Encoding ascii
Write-Host "created $vmx"
Write-Host "start it with: vmrun -T ws start `"$vmx`" gui"
