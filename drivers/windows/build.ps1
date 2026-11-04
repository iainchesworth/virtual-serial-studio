# Copyright (c) 2026 Virtual Serial Studio contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds the vsscom driver package: vsscom.sys, vsscom.inf and the catalog.
#
# It needs Visual Studio 2026 with the C++ desktop workload (the driver build
# targets ship with it) and NuGet access. The Windows SDK and the WDK come from
# the NuGet packages named in vsscom.vcxproj, so nothing else is installed. CI
# and a developer run the same script.
#
#   ./drivers/windows/build.ps1 [-Platform x64|ARM64] [-Configuration Release|Debug]
#
# The package is written to drivers/windows/artifacts/package/<platform>/. It is
# not signed here: see docs/windows-com-driver.md for the signing steps.

[CmdletBinding()]
param(
    [ValidateSet('x64', 'ARM64')]
    [string] $Platform = 'x64',
    [ValidateSet('Release', 'Debug')]
    [string] $Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'

$root = $PSScriptRoot
$project = Join-Path $root 'vsscom\vsscom.vcxproj'
$wdkVersion = ([xml](Get-Content $project)).SelectNodes("//*[local-name()='PackageReference'][@Include='Microsoft.Windows.WDK.x64']")[0].Version
$sdkTools = '10.0.28000.0'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw 'vswhere not found; install Visual Studio 2026' }
$msbuild = & $vswhere -latest -products * -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (-not $msbuild) { throw 'MSBuild not found' }

Write-Host "== Build ($Platform, $Configuration)"
& $msbuild $project -restore -m "-p:Configuration=$Configuration" "-p:Platform=$Platform" -nologo -v:minimal
if ($LASTEXITCODE -ne 0) { throw "msbuild failed with exit code $LASTEXITCODE" }

$out = Join-Path $root "artifacts\$Platform\$Configuration"
$sys = Join-Path $out 'vsscom.sys'
if (-not (Test-Path $sys)) { throw "vsscom.sys was not produced in $out" }

# The WDK tools come from the restored package.
$packages = if ($env:NUGET_PACKAGES) { $env:NUGET_PACKAGES } else { Join-Path $env:USERPROFILE '.nuget\packages' }
$wdk = Join-Path $packages "microsoft.windows.wdk.x64\$wdkVersion\c"
$infverif = Join-Path $wdk "tools\$sdkTools\x64\infverif.exe"
$inf2cat = Join-Path $wdk "bin\$sdkTools\x86\Inf2Cat.exe"
foreach ($tool in $infverif, $inf2cat) {
    if (-not (Test-Path $tool)) { throw "missing $tool" }
}

$package = Join-Path $root "artifacts\package\$Platform"
if (Test-Path $package) { Remove-Item $package -Recurse -Force }
New-Item -ItemType Directory $package | Out-Null
Copy-Item $sys $package
Copy-Item (Join-Path $root 'vsscom\vsscom.inf') $package

Write-Host '== InfVerif'
& $infverif /v /w (Join-Path $package 'vsscom.inf')
if ($LASTEXITCODE -ne 0) { throw "InfVerif failed with exit code $LASTEXITCODE" }

Write-Host '== Inf2Cat'
$os = if ($Platform -eq 'ARM64') { '10_RS4_ARM64' } else { '10_RS4_X64' }
& $inf2cat "/driver:$package" "/os:$os" /verbose
if ($LASTEXITCODE -ne 0) { throw "Inf2Cat failed with exit code $LASTEXITCODE" }

foreach ($file in 'vsscom.sys', 'vsscom.inf', 'vsscom.cat') {
    if (-not (Test-Path (Join-Path $package $file))) { throw "$file is missing from the package" }
}
Get-ChildItem $package | Select-Object Name, Length | Format-Table -AutoSize
Write-Host "Package: $package"
