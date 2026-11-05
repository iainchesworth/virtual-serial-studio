# Copyright (c) 2026 Virtual Serial Studio contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds the vsscom driver packages. There are two drivers for the same port, and
# the user picks one:
#
#   kmdf   vsscom.sys, the two INF files (vsscom.inf for the ports, vsscomctl.inf
#          for the control device) and a catalog for each;
#   umdf   vsscom_um.dll, vsscom_um.inf and its catalog: a user-mode driver, which
#          needs no kernel-mode code signature.
#
# It needs Visual Studio 2026 with the C++ desktop workload (the driver build
# targets ship with it) and NuGet access. The Windows SDK and the WDK come from
# the NuGet packages named in the project files, so nothing else is installed. CI
# and a developer run the same script.
#
#   ./drivers/windows/build.ps1 [-Platform x64|ARM64] [-Configuration Release|Debug]
#                               [-Driver all|kmdf|umdf]
#
# The kernel-mode package is written to drivers/windows/artifacts/package/<platform>/
# and the user-mode one to drivers/windows/artifacts/package-um/<platform>/. They
# are not signed here: see docs/windows-com-driver.md for the signing steps.

[CmdletBinding()]
param(
    [ValidateSet('x64', 'ARM64')]
    [string] $Platform = 'x64',
    [ValidateSet('Release', 'Debug')]
    [string] $Configuration = 'Release',
    [ValidateSet('all', 'kmdf', 'umdf')]
    [string] $Driver = 'all'
)

$ErrorActionPreference = 'Stop'

$root = $PSScriptRoot
$kmProject = Join-Path $root 'vsscom\vsscom.vcxproj'
$wdkVersion = ([xml](Get-Content $kmProject)).SelectNodes("//*[local-name()='PackageReference'][@Include='Microsoft.Windows.WDK.x64']")[0].Version
$sdkTools = '10.0.28000.0'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw 'vswhere not found; install Visual Studio 2026' }
$msbuild = & $vswhere -latest -products * -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (-not $msbuild) { throw 'MSBuild not found' }

# The WDK tools come from the restored package.
$packages = if ($env:NUGET_PACKAGES) { $env:NUGET_PACKAGES } else { Join-Path $env:USERPROFILE '.nuget\packages' }
$wdk = Join-Path $packages "microsoft.windows.wdk.x64\$wdkVersion\c"
$infverif = Join-Path $wdk "tools\$sdkTools\x64\infverif.exe"
$inf2cat = Join-Path $wdk "bin\$sdkTools\x86\Inf2Cat.exe"
$os = if ($Platform -eq 'ARM64') { '10_RS4_ARM64' } else { '10_RS4_X64' }

# One driver: build it, copy it with its INF files into a package folder, check
# the INF files and make the catalogs.
function Build-Driver {
    param(
        [string] $Name,         # for the messages
        [string] $Project,
        [string] $OutSubdir,    # under artifacts\ ('' for the kernel-mode driver)
        [string] $Binary,       # vsscom.sys or vsscom_um.dll
        [string] $InfDir,       # the folder of the INF files
        [string[]] $Infs,
        [string] $PackageSubdir,
        [string[]] $Expected
    )

    Write-Host "== Build $Name ($Platform, $Configuration)"
    & $msbuild $Project -restore -m "-p:Configuration=$Configuration" "-p:Platform=$Platform" -nologo -v:minimal
    if ($LASTEXITCODE -ne 0) { throw "msbuild failed for $Name with exit code $LASTEXITCODE" }

    $out = Join-Path $root (Join-Path 'artifacts' (Join-Path $OutSubdir "$Platform\$Configuration"))
    $binary = Join-Path $out $Binary
    if (-not (Test-Path $binary)) { throw "$Binary was not produced in $out" }
    foreach ($tool in $infverif, $inf2cat) {
        if (-not (Test-Path $tool)) { throw "missing $tool" }
    }

    $package = Join-Path $root "artifacts\$PackageSubdir\$Platform"
    if (Test-Path $package) { Remove-Item $package -Recurse -Force }
    New-Item -ItemType Directory $package | Out-Null
    Copy-Item $binary $package
    foreach ($inf in $Infs) { Copy-Item (Join-Path $InfDir $inf) $package }

    Write-Host "== InfVerif $Name"
    foreach ($inf in $Infs) {
        & $infverif /v /w (Join-Path $package $inf)
        if ($LASTEXITCODE -ne 0) { throw "InfVerif failed for $inf with exit code $LASTEXITCODE" }
    }

    Write-Host "== Inf2Cat $Name"
    & $inf2cat "/driver:$package" "/os:$os" /verbose
    if ($LASTEXITCODE -ne 0) { throw "Inf2Cat failed with exit code $LASTEXITCODE" }

    foreach ($file in $Expected) {
        if (-not (Test-Path (Join-Path $package $file))) { throw "$file is missing from the $Name package" }
    }
    Get-ChildItem $package | Select-Object Name, Length | Format-Table -AutoSize
    Write-Host "Package: $package"
}

if ($Driver -in 'all', 'kmdf') {
    Build-Driver -Name 'kernel-mode driver' -Project $kmProject -OutSubdir '' -Binary 'vsscom.sys' `
        -InfDir (Join-Path $root 'vsscom') -Infs 'vsscom.inf', 'vsscomctl.inf' -PackageSubdir 'package' `
        -Expected 'vsscom.sys', 'vsscom.inf', 'vsscom.cat', 'vsscomctl.inf', 'vsscomctl.cat'
}

if ($Driver -in 'all', 'umdf') {
    Build-Driver -Name 'user-mode driver' -Project (Join-Path $root 'vsscom_um\vsscom_um.vcxproj') -OutSubdir 'um' -Binary 'vsscom_um.dll' `
        -InfDir (Join-Path $root 'vsscom_um') -Infs 'vsscom_um.inf' -PackageSubdir 'package-um' `
        -Expected 'vsscom_um.dll', 'vsscom_um.inf', 'vsscom_um.cat'
}
