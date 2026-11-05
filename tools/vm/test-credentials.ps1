#!/usr/bin/env pwsh
#
# Copyright (c) 2026 Virtual Serial Studio contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
<#
.SYNOPSIS
    Tests tools/vm/credentials.ps1 against a temporary file. The password it makes is thrown away.
#>

$ErrorActionPreference = 'Stop'
$script = Join-Path $PSScriptRoot 'credentials.ps1'
$work = Join-Path ([IO.Path]::GetTempPath()) ("vss-cred-test-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
$file = Join-Path $work 'creds.json'

$failures = @()
$count = 0
function Assert-That([string] $Label, [bool] $Condition, [string] $Detail = '') {
    $script:count++
    if ($Condition) { Write-Host "ok   $Label" }
    else { Write-Host "FAIL $Label $Detail"; $script:failures += $Label }
}

try {
    $missing = & { try { . $script -Path $file; 'loaded' } catch { $_.Exception.Message } }
    Assert-That 'a missing file is an error without -Create' ($missing -match 'no credentials file') $missing
    Assert-That 'nothing was created without -Create' (-not (Test-Path $file))

    Remove-Item Env:VSS_VM_USER, Env:VSS_VM_PASSWORD -ErrorAction SilentlyContinue
    $output = . $script -Path $file -Create | Out-String
    $stored = Get-Content $file -Raw | ConvertFrom-Json
    Assert-That 'the file is created with -Create' (Test-Path $file)
    Assert-That 'the user is vss' ($stored.user -eq 'vss' -and $env:VSS_VM_USER -eq 'vss')
    Assert-That 'the password is 24 characters with all three classes' ($stored.password.Length -eq 24 -and $stored.password -cmatch '[A-Z]' -and $stored.password -cmatch '[a-z]' -and $stored.password -match '\d')
    Assert-That 'the environment gets the stored password' ($env:VSS_VM_PASSWORD -ceq $stored.password)
    Assert-That 'the password is never printed' (-not $output.Contains($stored.password))

    $before = $stored.password
    $null = . $script -Path $file -Create
    Assert-That 'an existing file is never replaced' ((Get-Content $file -Raw | ConvertFrom-Json).password -ceq $before)
}
finally {
    Remove-Item Env:VSS_VM_USER, Env:VSS_VM_PASSWORD -ErrorAction SilentlyContinue
    Remove-Item -Recurse -Force -LiteralPath $work -ErrorAction SilentlyContinue
}

Write-Host ''
Write-Host "$($count - $failures.Count) of $count checks passed"
if ($failures.Count -gt 0) { exit 1 }
exit 0
