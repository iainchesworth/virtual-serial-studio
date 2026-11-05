# Copyright (c) 2026 Virtual Serial Studio contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Signs the vsscom package with a throwaway certificate made in this guest, and
# makes this guest trust it. For disposable test guests only.
#
#   prepare-test-signing.ps1 -ConfirmDisposableGuest [-PackageDir <dir>] [-EnableTestSigning]
#
# Run it only in a VSS test guest that reverts to a clean snapshot. It changes
# the guest's trust stores and, with -EnableTestSigning, its boot settings, so
# it refuses to run without -ConfirmDisposableGuest. Never run it on a machine
# you care about.
#
# The certificate is made here, its private key cannot be exported, and it is
# gone when the guest reverts. Nothing about it leaves the guest: no file with a
# key is written, and nothing in the repository, a log or a build artefact holds
# one. A package signed like this is for local testing, not for release.
#
# What it does:
#   1. makes a code-signing certificate in LocalMachine\My (six months);
#   2. puts its public part in LocalMachine\Root and LocalMachine\TrustedPublisher;
#   3. signs vsscom.sys, vsscom.cat and vsscomctl.cat in -PackageDir;
#   4. reports whether test signing is on. A kernel driver whose signature does
#      not chain to a Microsoft root loads only with test signing on, which needs
#      Secure Boot off and a restart. -EnableTestSigning turns it on; restart the
#      guest afterwards, then run install-driver.ps1.
#
# Exit code 0 when the package is signed and test signing is on; 1 when it is
# signed but test signing or a restart is still needed; 2 on an error.

[CmdletBinding()]
param(
    [switch] $ConfirmDisposableGuest,
    [string] $PackageDir = $PSScriptRoot,
    [switch] $EnableTestSigning
)

$ErrorActionPreference = 'Stop'

if (-not $ConfirmDisposableGuest) {
    Write-Error 'pass -ConfirmDisposableGuest to say this is a disposable VSS test guest'
    exit 2
}

$identity = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
if (-not $identity.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Error 'run this as an administrator'
    exit 2
}

try {
    foreach ($name in 'vsscom.sys', 'vsscom.cat', 'vsscom.inf', 'vsscomctl.cat', 'vsscomctl.inf') {
        if (-not (Test-Path (Join-Path $PackageDir $name))) { throw "$name is not in $PackageDir" }
    }

    $cert = New-SelfSignedCertificate -Type CodeSigningCert `
        -Subject 'CN=Virtual Serial Studio guest test' `
        -CertStoreLocation 'Cert:\LocalMachine\My' `
        -KeyUsage DigitalSignature -KeyExportPolicy NonExportable `
        -NotAfter (Get-Date).AddMonths(6)

    # Only the public part leaves the store, for a moment, to be trusted.
    $public = Join-Path ([IO.Path]::GetTempPath()) 'vss-guest-test.cer'
    Export-Certificate -Cert $cert -FilePath $public | Out-Null
    foreach ($store in 'Root', 'TrustedPublisher') {
        Import-Certificate -FilePath $public -CertStoreLocation "Cert:\LocalMachine\$store" | Out-Null
    }
    Remove-Item $public

    foreach ($name in 'vsscom.sys', 'vsscom.cat', 'vsscomctl.cat') {
        $result = Set-AuthenticodeSignature -FilePath (Join-Path $PackageDir $name) -Certificate $cert `
            -HashAlgorithm SHA256
        if ($result.Status -ne 'Valid') { throw "signing $name gave $($result.Status): $($result.StatusMessage)" }
        Write-Host "signed $name"
    }

    $options = (& bcdedit.exe /enum '{current}' | Out-String)
    $testSigning = $options -match 'testsigning\s+Yes'
    if (-not $testSigning -and $EnableTestSigning) {
        & bcdedit.exe /set testsigning on | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw 'bcdedit /set testsigning on failed (Secure Boot is probably on)'
        }
        Write-Host 'test signing is now on: restart the guest, then run install-driver.ps1'
        exit 1
    }
    if (-not $testSigning) {
        Write-Host 'test signing is off: run again with -EnableTestSigning, restart, then install'
        exit 1
    }
    Write-Host 'the package is signed and test signing is on'
    exit 0
} catch {
    Write-Error $_.Exception.Message
    exit 2
}
