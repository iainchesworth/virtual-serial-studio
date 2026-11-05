# Copyright (c) 2026 Virtual Serial Studio contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Makes a machine trust a driver package or helper program that was signed by the
# owner's own certificate authority (CA), so the package installs with test signing
# OFF. Run it as an administrator. It handles public certificates only: it never
# needs, asks for or touches a private key.
#
#   install-trust.ps1 -RootCertificate <root.pem> -IntermediateCertificate <intermediate.pem>
#                     [-Package <folder, .cat, .dll or .exe>] [-ExpectedRootSha256 <hex>]
#                     [-AllowTestSigning] [-WhatIf]
#   install-trust.ps1 -Uninstall -RootCertificate <root.pem> -IntermediateCertificate <intermediate.pem>
#                     [-Package <...> | -SignerThumbprint <hex>]
#
# What it changes on the machine (and what -Uninstall takes away again):
#   - the root certificate     -> Local Machine, Trusted Root Certification Authorities
#   - the intermediate         -> Local Machine, Intermediate Certification Authorities
#   - the signer's certificate -> Local Machine, Trusted Publishers
# The signer's certificate is read from the signature of -Package. It is added only
# after the signature checks out against the root you named, so a package signed by
# anyone else adds nothing. The Trusted Publishers entry is needed because pnputil
# cannot ask the question "do you trust this publisher?" and refuses without it.
#
# Trusting a root means trusting everything that CA ever signs, for every program
# on the machine. Do this only on machines you own, and check the printed
# fingerprint of the root against the one you hold. -ExpectedRootSha256 makes the
# script refuse a root whose fingerprint differs.
#
# The driver is not installed here. Afterwards:  pnputil /add-driver vsscom_um.inf /install
#
# Exit codes: 0 done, 1 a check failed, 2 not an administrator, 3 test signing is on.

[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [Parameter(Mandatory = $true)] [string] $RootCertificate,
    [Parameter(Mandatory = $true)] [string] $IntermediateCertificate,
    [string] $Package,
    [string] $SignerThumbprint,
    [string] $ExpectedRootSha256,
    [switch] $AllowTestSigning,
    [switch] $Uninstall
)

$ErrorActionPreference = 'Stop'

function Read-Certificate([string] $Path) {
    # Accepts PEM (with or without text before the BEGIN line) or DER.
    $bytes = [IO.File]::ReadAllBytes($Path)
    $text = [Text.Encoding]::ASCII.GetString($bytes)
    $match = [regex]::Match($text, '-----BEGIN CERTIFICATE-----(?<body>.*?)-----END CERTIFICATE-----',
        [Text.RegularExpressions.RegexOptions]::Singleline)
    if ($match.Success) {
        $bytes = [Convert]::FromBase64String(($match.Groups['body'].Value -replace '\s', ''))
    }
    New-Object Security.Cryptography.X509Certificates.X509Certificate2 -ArgumentList (, $bytes)
}

function Get-Sha256([Security.Cryptography.X509Certificates.X509Certificate2] $Certificate) {
    $hash = [Security.Cryptography.SHA256]::Create().ComputeHash($Certificate.RawData)
    ($hash | ForEach-Object { $_.ToString('X2') }) -join ''
}

function Test-IsCa([Security.Cryptography.X509Certificates.X509Certificate2] $Certificate) {
    $constraints = $Certificate.Extensions | Where-Object { $_ -is [Security.Cryptography.X509Certificates.X509BasicConstraintsExtension] }
    $constraints -and $constraints.CertificateAuthority
}

function Open-Store([string] $StoreName, [bool] $Write) {
    $store = New-Object Security.Cryptography.X509Certificates.X509Store -ArgumentList $StoreName, 'LocalMachine'
    $mode = if ($Write) { 'ReadWrite' } else { 'ReadOnly' }
    $store.Open($mode)
    $store
}

function Test-InStore([string] $StoreName, [string] $Thumbprint) {
    $store = Open-Store $StoreName $false
    try { [bool]($store.Certificates | Where-Object { $_.Thumbprint -eq $Thumbprint }) } finally { $store.Close() }
}

function Add-ToStore([string] $StoreName, [Security.Cryptography.X509Certificates.X509Certificate2] $Certificate, [string] $What) {
    if (Test-InStore $StoreName $Certificate.Thumbprint) { Write-Host "already in $StoreName : $What"; return }
    if (-not $PSCmdlet.ShouldProcess("Local Machine\$StoreName", "add $What")) { return }
    $store = Open-Store $StoreName $true
    try { $store.Add($Certificate) } finally { $store.Close() }
    Write-Host "added to $StoreName : $What"
}

function Remove-FromStore([string] $StoreName, [string] $Thumbprint, [string] $What) {
    if (-not (Test-InStore $StoreName $Thumbprint)) { Write-Host "not in $StoreName : $What"; return }
    if (-not $PSCmdlet.ShouldProcess("Local Machine\$StoreName", "remove $What")) { return }
    $store = Open-Store $StoreName $true
    try {
        $store.Certificates | Where-Object { $_.Thumbprint -eq $Thumbprint } | ForEach-Object { $store.Remove($_) }
    } finally { $store.Close() }
    Write-Host "removed from $StoreName : $What"
}

function Find-SignedFile([string] $Path) {
    if (-not (Test-Path -LiteralPath $Path)) { throw "-Package not found: $Path" }
    $item = Get-Item -LiteralPath $Path
    if (-not $item.PSIsContainer) { return $item.FullName }
    # A folder: prefer the catalog (what pnputil checks), then the helper program.
    foreach ($pattern in '*.cat', 'vsscom_helper.exe', '*.dll') {
        $found = Get-ChildItem -LiteralPath $item.FullName -Filter $pattern -File | Select-Object -First 1
        if ($found) { return $found.FullName }
    }
    throw "no .cat, .exe or .dll to read a signature from in $Path"
}

try {
    if (-not $WhatIfPreference) {
        $identity = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
        if (-not $identity.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
            Write-Error 'run this as an administrator'
            exit 2
        }
    }

    # With test signing on, an install works with any signature, so it would prove nothing
    # about the trust set up here. Say so and stop, unless the person asked for it anyway.
    if (-not $Uninstall) {
        $boot = (& bcdedit.exe /enum '{current}' 2>$null) -join "`n"
        if ($boot -match '(?im)^\s*testsigning\s+Yes') {
            if (-not $AllowTestSigning) {
                Write-Error ('test signing is ON, so a package installs here whether or not it is trusted. ' +
                    'Turn it off ("bcdedit /set testsigning off", then restart) or pass -AllowTestSigning.')
                exit 3
            }
            Write-Warning 'test signing is on; an install will not show whether the trust below is enough'
        }
    }

    $root = Read-Certificate $RootCertificate
    $intermediate = Read-Certificate $IntermediateCertificate
    $rootSha = Get-Sha256 $root

    if ($root.Subject -ne $root.Issuer) { throw 'the root certificate is not self-signed' }
    if (-not (Test-IsCa $root)) { throw 'the root certificate is not marked as a CA' }
    if (-not (Test-IsCa $intermediate)) { throw 'the intermediate certificate is not marked as a CA' }
    if ($intermediate.Issuer -ne $root.Subject) { throw 'the intermediate was not issued by this root' }
    if ($ExpectedRootSha256 -and (($ExpectedRootSha256 -replace '[^0-9A-Fa-f]', '').ToUpperInvariant() -ne $rootSha)) {
        throw "the root's SHA-256 fingerprint is $rootSha, not the one expected"
    }
    $now = Get-Date
    foreach ($c in $root, $intermediate) {
        if ($now -lt $c.NotBefore -or $now -gt $c.NotAfter) { throw "certificate $($c.Subject) is outside its validity dates" }
    }
    Write-Host "root         : $($root.Subject)"
    Write-Host "  SHA-256    : $rootSha"
    Write-Host "  valid until: $($root.NotAfter.ToString('yyyy-MM-dd'))"
    Write-Host "intermediate : $($intermediate.Subject)"

    if ($Uninstall) {
        Remove-FromStore 'Root' $root.Thumbprint 'root certificate'
        Remove-FromStore 'CA' $intermediate.Thumbprint 'intermediate certificate'
        $thumb = $SignerThumbprint
        if (-not $thumb -and $Package) {
            $sig = Get-AuthenticodeSignature -LiteralPath (Find-SignedFile $Package)
            if ($sig.SignerCertificate) { $thumb = $sig.SignerCertificate.Thumbprint }
        }
        if ($thumb) { Remove-FromStore 'TrustedPublisher' ($thumb -replace '\s', '').ToUpperInvariant() 'signer certificate' }
        else { Write-Host 'signer certificate left alone (give -Package or -SignerThumbprint to remove it)' }
        exit 0
    }

    # Read the signer before adding anything, so a package signed by the wrong party adds nothing.
    $signer = $null
    if ($Package) {
        $file = Find-SignedFile $Package
        $sig = Get-AuthenticodeSignature -LiteralPath $file
        if (-not $sig.SignerCertificate) { throw "$file has no signature" }
        $signer = $sig.SignerCertificate
        if (-not $sig.TimeStamperCertificate) { Write-Warning "$file has no timestamp; it stops being valid when the signer's certificate expires" }
        $chain = New-Object Security.Cryptography.X509Certificates.X509Chain
        $chain.ChainPolicy.RevocationMode = 'NoCheck'
        [void]$chain.ChainPolicy.ExtraStore.Add($root)
        [void]$chain.ChainPolicy.ExtraStore.Add($intermediate)
        [void]$chain.Build($signer)
        $top = $chain.ChainElements[$chain.ChainElements.Count - 1].Certificate
        if ($top.Thumbprint -ne $root.Thumbprint) {
            throw "$file is not signed under the root you named (its signer chains to $($top.Subject))"
        }
        Write-Host "signer       : $($signer.Subject)  (expires $($signer.NotAfter.ToString('yyyy-MM-dd')))"
        Write-Host "  signed file: $file"
    }

    Add-ToStore 'Root' $root 'root certificate'
    Add-ToStore 'CA' $intermediate 'intermediate certificate'
    if ($signer) {
        if ($PSCmdlet.ShouldProcess($file, 'check the signature now that the root is trusted')) {
            $after = Get-AuthenticodeSignature -LiteralPath $file
            if ($after.Status -ne 'Valid') {
                throw "the signature on $file is $($after.Status) ($($after.StatusMessage)); signer not added to Trusted Publishers"
            }
        }
        Add-ToStore 'TrustedPublisher' $signer 'signer certificate'
    } else {
        Write-Host 'no -Package given: the signer certificate was not added (pnputil needs it)'
    }
    Write-Host 'done. Next: pnputil /add-driver vsscom_um.inf /install  (and install-helper.ps1 for the helper)'
    exit 0
}
catch {
    Write-Error $_.Exception.Message
    exit 1
}
