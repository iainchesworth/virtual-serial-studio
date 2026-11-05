#!/usr/bin/env pwsh
#
# Copyright (c) 2026 Virtual Serial Studio contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
<#
.SYNOPSIS
    Writes the answer disc for an unattended install of a VSS guest.

.DESCRIPTION
    The disc is an ISO image: autounattend.xml and setup-guest.ps1 for Windows (volume
    label VSSSEED), or user-data and meta-data for Ubuntu (volume label CIDATA, the NoCloud
    seed). It holds the guest password, so it is written outside every repository, by
    default to %USERPROFILE%\.vss\seeds.

    The user name and password come from VSS_VM_USER and VSS_VM_PASSWORD in the environment
    of the shell that runs this script. The script never prints them. For Ubuntu it hashes
    the password with "openssl passwd -6" in WSL Debian, passing it on standard input.

    Run it yourself, in your own shell. The scripts are checked with placeholders only.

.PARAMETER Guest
    The id of a guest in matrix.json: windows10, windows11 or ubuntu2604.

.PARAMETER DryRun
    Use placeholder credentials and a placeholder hash, and write to -OutDir. For the tests.
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory, Position = 0)]
    [ValidateSet('windows10', 'windows11', 'ubuntu2604')]
    [string] $Guest,

    [string] $OutDir = (Join-Path $HOME '.vss/seeds'),
    [string] $PublicKeyFile = (Join-Path $HOME '.vss/vss_vm_ed25519.pub'),
    [string] $WslDistro = 'Debian',
    [switch] $DryRun
)

$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$matrix = Get-Content (Join-Path $here 'matrix.json') -Raw | ConvertFrom-Json
$entry = $matrix.guests | Where-Object { $_.id -eq $Guest }
if (-not $entry) { throw "no guest '$Guest' in matrix.json" }

if ($DryRun) {
    $user = 'placeholder-user'
    $password = 'placeholder-not-a-credential'
} else {
    if (-not $env:VSS_VM_USER -or -not $env:VSS_VM_PASSWORD) {
        throw 'set VSS_VM_USER and VSS_VM_PASSWORD in this shell first'
    }
    $user = $env:VSS_VM_USER
    $password = $env:VSS_VM_PASSWORD
}

# The user name goes into XML and YAML, so it is kept to plain characters.
if ($user -notmatch '^[a-z][a-z0-9_-]{0,30}$') {
    throw 'VSS_VM_USER must be a lower-case user name of letters, digits, - and _'
}

function Protect-Xml([string] $Text) { return [System.Security.SecurityElement]::Escape($Text) }

function Get-PasswordHash {
    # SHA-512 crypt of the password, made by openssl in WSL. The password goes to openssl on
    # standard input, never on a command line, and ends in a line feed only: a carriage return
    # from the pipeline would be part of the password and the hash would not match it.
    param([string] $Password, [string] $Distro)

    $info = New-Object Diagnostics.ProcessStartInfo
    $info.FileName = 'wsl.exe'
    foreach ($argument in '-d', $Distro, '--', 'openssl', 'passwd', '-6', '-stdin') { $info.ArgumentList.Add($argument) }
    $info.RedirectStandardInput = $true
    $info.RedirectStandardOutput = $true
    $info.UseShellExecute = $false
    $process = [Diagnostics.Process]::Start($info)
    $process.StandardInput.NewLine = "`n"
    $process.StandardInput.WriteLine($Password)
    $process.StandardInput.Close()
    $hash = $process.StandardOutput.ReadToEnd().Trim()
    $process.WaitForExit()
    if ($process.ExitCode -ne 0 -or $hash -notmatch '^\$6\$') { throw 'openssl could not hash the password in WSL' }
    return $hash
}

function Get-Template([string] $Name) {
    return Get-Content (Join-Path $here "templates/$Name") -Raw
}

function New-IsoImage {
    # Builds an ISO 9660 + Joliet image from a folder with the IMAPI2 file system image
    # that ships with Windows.
    param([string] $Folder, [string] $Label, [string] $Path)

    if (-not $IsWindows -and $PSVersionTable.PSEdition -eq 'Core') {
        throw 'building the seed disc needs Windows (IMAPI2)'
    }
    if (-not ('VssIsoWriter' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;
public static class VssIsoWriter
{
    public static void Write(object comStream, string path, int blockSize, int blockCount)
    {
        IStream stream = (IStream)comStream;
        byte[] buffer = new byte[blockSize];
        IntPtr read = Marshal.AllocHGlobal(4);
        try
        {
            using (FileStream file = File.Create(path))
            {
                for (int block = 0; block < blockCount; block++)
                {
                    stream.Read(buffer, blockSize, read);
                    int count = Marshal.ReadInt32(read);
                    if (count <= 0) break;
                    file.Write(buffer, 0, count);
                }
            }
        }
        finally { Marshal.FreeHGlobal(read); }
    }
}
'@
    }

    $image = New-Object -ComObject IMAPI2FS.MsftFileSystemImage
    $image.FileSystemsToCreate = 3   # ISO 9660 and Joliet
    $image.VolumeName = $Label
    $image.Root.AddTree($Folder, $false)
    $result = $image.CreateResultImage()
    [VssIsoWriter]::Write($result.ImageStream, $Path, $result.BlockSize, $result.TotalBlocks)
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$staging = Join-Path ([IO.Path]::GetTempPath()) ("vss-seed-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $staging | Out-Null

try {
    $hostname = 'vss-' + $Guest
    if ($entry.kind -eq 'windows') {
        $uefi = $entry.firmware -eq 'efi'
        $edition = if ($Guest -eq 'windows11') { 'Windows 11 Pro' } else { 'Windows 10 Pro' }

        if ($uefi) {
            $disk = @'
      <DiskConfiguration>
        <WillShowUI>OnError</WillShowUI>
        <Disk wcm:action="add" xmlns:wcm="http://schemas.microsoft.com/WMIConfig/2002/State">
          <DiskID>0</DiskID>
          <WillWipeDisk>true</WillWipeDisk>
          <CreatePartitions>
            <CreatePartition wcm:action="add"><Order>1</Order><Type>EFI</Type><Size>100</Size></CreatePartition>
            <CreatePartition wcm:action="add"><Order>2</Order><Type>MSR</Type><Size>16</Size></CreatePartition>
            <CreatePartition wcm:action="add"><Order>3</Order><Type>Primary</Type><Extend>true</Extend></CreatePartition>
          </CreatePartitions>
          <ModifyPartitions>
            <ModifyPartition wcm:action="add"><Order>1</Order><PartitionID>1</PartitionID><Format>FAT32</Format><Label>System</Label></ModifyPartition>
            <ModifyPartition wcm:action="add"><Order>2</Order><PartitionID>2</PartitionID></ModifyPartition>
            <ModifyPartition wcm:action="add"><Order>3</Order><PartitionID>3</PartitionID><Format>NTFS</Format><Label>Windows</Label><Letter>C</Letter></ModifyPartition>
          </ModifyPartitions>
        </Disk>
      </DiskConfiguration>
      <ImageInstall>
        <OSImage>
          <InstallTo><DiskID>0</DiskID><PartitionID>3</PartitionID></InstallTo>
          <InstallFrom><MetaData wcm:action="add" xmlns:wcm="http://schemas.microsoft.com/WMIConfig/2002/State"><Key>/IMAGE/NAME</Key><Value>@EDITION@</Value></MetaData></InstallFrom>
        </OSImage>
      </ImageInstall>
'@
        } else {
            $disk = @'
      <DiskConfiguration>
        <WillShowUI>OnError</WillShowUI>
        <Disk wcm:action="add" xmlns:wcm="http://schemas.microsoft.com/WMIConfig/2002/State">
          <DiskID>0</DiskID>
          <WillWipeDisk>true</WillWipeDisk>
          <CreatePartitions>
            <CreatePartition wcm:action="add"><Order>1</Order><Type>Primary</Type><Extend>true</Extend></CreatePartition>
          </CreatePartitions>
          <ModifyPartitions>
            <ModifyPartition wcm:action="add"><Order>1</Order><PartitionID>1</PartitionID><Active>true</Active><Format>NTFS</Format><Label>Windows</Label><Letter>C</Letter></ModifyPartition>
          </ModifyPartitions>
        </Disk>
      </DiskConfiguration>
      <ImageInstall>
        <OSImage>
          <InstallTo><DiskID>0</DiskID><PartitionID>1</PartitionID></InstallTo>
          <InstallFrom><MetaData wcm:action="add" xmlns:wcm="http://schemas.microsoft.com/WMIConfig/2002/State"><Key>/IMAGE/NAME</Key><Value>@EDITION@</Value></MetaData></InstallFrom>
        </OSImage>
      </ImageInstall>
'@
        }

        # Windows 11 setup checks for a TPM, Secure Boot and RAM that these guests do not have.
        $lab = ''
        if ($Guest -eq 'windows11') {
            $checks = 'BypassTPMCheck', 'BypassSecureBootCheck', 'BypassRAMCheck', 'BypassCPUCheck'
            $commands = for ($i = 0; $i -lt $checks.Count; $i++) {
                "        <RunSynchronousCommand wcm:action=`"add`" xmlns:wcm=`"http://schemas.microsoft.com/WMIConfig/2002/State`"><Order>$($i + 1)</Order><Path>reg add HKLM\SYSTEM\Setup\LabConfig /v $($checks[$i]) /t REG_DWORD /d 1 /f</Path></RunSynchronousCommand>"
            }
            $lab = "      <RunSynchronous>`n" + ($commands -join "`n") + "`n      </RunSynchronous>"
        }

        $xml = (Get-Template 'autounattend.xml.in')
        $xml = $xml.Replace('@LABCONFIG@', $lab).Replace('@DISKCONFIG@', $disk)
        $xml = $xml.Replace('@EDITION@', (Protect-Xml $edition))
        $xml = $xml.Replace('@LANGUAGE@', (Protect-Xml $entry.language))
        $xml = $xml.Replace('@HOSTNAME@', (Protect-Xml $hostname))
        $xml = $xml.Replace('@USER@', (Protect-Xml $user))
        $xml = $xml.Replace('@PASSWORD@', (Protect-Xml $password))
        [IO.File]::WriteAllText((Join-Path $staging 'autounattend.xml'), $xml, (New-Object Text.UTF8Encoding($false)))
        Copy-Item (Join-Path $here 'guest/setup-guest.ps1') $staging
        $label = 'VSSSEED'
    } else {
        if ($DryRun) {
            $hash = '$6$placeholder$placeholder'
            $key = 'ssh-ed25519 AAAAplaceholder placeholder'
        } else {
            if (-not (Test-Path $PublicKeyFile)) { throw "no public key at $PublicKeyFile" }
            $key = (Get-Content $PublicKeyFile -Raw).Trim()
            if ($key -notmatch '^ssh-(ed25519|rsa) ') { throw "$PublicKeyFile does not hold a public key" }
            $hash = Get-PasswordHash -Password $password -Distro $WslDistro
        }
        $data = Get-Template 'user-data.in'
        $data = $data.Replace('@HOSTNAME@', $hostname).Replace('@USER@', $user)
        $data = $data.Replace('@PASSWORD_HASH@', $hash).Replace('@PUBLIC_KEY@', $key)
        $utf8 = New-Object Text.UTF8Encoding($false)
        [IO.File]::WriteAllText((Join-Path $staging 'user-data'), $data.Replace("`r`n", "`n"), $utf8)
        [IO.File]::WriteAllText((Join-Path $staging 'meta-data'), "instance-id: $hostname`nlocal-hostname: $hostname`n", $utf8)
        $label = 'CIDATA'
    }

    $iso = Join-Path $OutDir "$Guest-seed.iso"
    New-IsoImage -Folder $staging -Label $label -Path $iso
    Write-Host "wrote $iso (volume $label)"
}
finally {
    Remove-Item -Recurse -Force -LiteralPath $staging -ErrorAction SilentlyContinue
}
