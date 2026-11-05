#!/usr/bin/env pwsh
#
# Copyright (c) 2026 Virtual Serial Studio contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
<#
.SYNOPSIS
    Loads the credentials of the VSS test guests into VSS_VM_USER and VSS_VM_PASSWORD.

.DESCRIPTION
    Dot-source it: . ./tools/vm/credentials.ps1

    The credentials live in a JSON file outside every repository, by default
    $HOME/.vss/guest-credentials.json, readable by the current user only. With -Create the
    file is written once, with a random password, if it does not exist. The script never
    prints the password. The guests are disposable machines on the local VMware host that
    only exist to test the software; the file is not a place for any other secret.

.PARAMETER Path
    The credentials file.

.PARAMETER Create
    Create the file with a random password if it is missing. An existing file is never replaced.
#>

[CmdletBinding()]
param(
    [string] $Path = (Join-Path $HOME '.vss/guest-credentials.json'),
    [switch] $Create
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Path)) {
    if (-not $Create) { throw "no credentials file at $Path; run with -Create to make one" }

    # Upper case, lower case and digits only: this satisfies the Windows complexity rule and
    # needs no quoting on a command line or in an answer file.
    $alphabet = 'ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789'
    $generator = [Security.Cryptography.RandomNumberGenerator]::Create()
    do {
        $bytes = New-Object byte[] 24
        $generator.GetBytes($bytes)
        $password = -join ($bytes | ForEach-Object { $alphabet[$_ % $alphabet.Length] })
    } until ($password -cmatch '[A-Z]' -and $password -cmatch '[a-z]' -and $password -match '\d')

    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Path) | Out-Null
    $json = [pscustomobject]@{ user = 'vss'; password = $password } | ConvertTo-Json
    [IO.File]::WriteAllText($Path, $json, (New-Object Text.UTF8Encoding($false)))
    if ($IsWindows -or $env:OS -eq 'Windows_NT') {
        & icacls.exe $Path /inheritance:r /grant:r "$($env:USERNAME):(R,W)" | Out-Null
    } else {
        chmod 600 $Path
    }
    Remove-Variable password, json, bytes
}

$stored = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
if (-not $stored.user -or -not $stored.password) { throw "$Path does not hold a user and a password" }
$env:VSS_VM_USER = $stored.user
$env:VSS_VM_PASSWORD = $stored.password
Write-Host "loaded the guest credentials from $Path (user $($stored.user))"
