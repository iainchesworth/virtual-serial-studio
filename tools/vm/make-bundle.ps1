#!/usr/bin/env pwsh
#
# Copyright (c) 2026 Virtual Serial Studio contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
<#
.SYNOPSIS
    Builds the programs for a Windows guest cell outside the guest and packs what the guest needs.

.DESCRIPTION
    Needs a Windows build environment: Visual Studio with the C++ tools, CMake, Boost and Qt, found
    the way `cmake --preset windows-msvc-release-gui` finds them (BOOST_ROOT for the Boost headers,
    ICL_QT_ROOT or VSS_QT_ROOT for Qt). It builds in -Work, runs windeployqt on the Qt programs, and writes into -Out:

      bundle.zip   the source and the build tree, and ctest.exe
      bundle.json  where it was built, which tools/vm/guest/win-cell.ps1 reads
      py.zip       the Python on this machine, for run_cell.py and the matrix tool tests
      pwsh.zip     PowerShell 7, which the vm_* ctest suites need
      rt.zip       the Visual C++ runtime and the Qt offscreen platform plugin

    The guest has no compiler, so a guest cell proves that the build runs and passes on that
    system, not that the guest can build it. -Work must be on a drive with several gigabytes free.
    The tests read sources through the absolute path they were built with, so the guest is given
    the same path (win-cell.ps1 does that with a junction and a drive letter).
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $Repository,
    [Parameter(Mandatory)] [string] $Out,
    [string] $Work = 'D:\vss-build'
)

$ErrorActionPreference = 'Stop'
$preset = 'windows-msvc-release-gui'
$Repository = (Resolve-Path -LiteralPath $Repository).Path
New-Item -ItemType Directory -Force $Out | Out-Null
$Out = (Resolve-Path -LiteralPath $Out).Path

$qtRoot = @($env:VSS_QT_ROOT, $env:ICL_QT_ROOT, $env:QT_ROOT_DIR, $env:QTDIR) | Where-Object { $_ } | Select-Object -First 1
if (-not $qtRoot -or -not (Test-Path (Join-Path $qtRoot 'bin\windeployqt.exe'))) {
    throw 'set VSS_QT_ROOT to the Qt installation that holds bin\windeployqt.exe'
}

$source = Join-Path $Work 'src'
Remove-Item -Recurse -Force $Work -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $source | Out-Null
robocopy $Repository $source /E /XD .git build /NFL /NDL /NJH /NJS /NP | Out-Null

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -property installationPath
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
$env:ICL_QT_ROOT = $qtRoot
cmd /c "`"$vcvars`" >nul && cd /d $source && cmake --preset $preset && cmake --build --preset $preset"
if ($LASTEXITCODE -ne 0) { throw "the build failed with exit code $LASTEXITCODE" }

$bin = Join-Path $source "build\$preset\bin"
$env:PATH = "$qtRoot\bin;$env:PATH"
foreach ($program in Get-ChildItem $bin -Filter 'vss_gui*.exe') {
    & "$qtRoot\bin\windeployqt.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw $program.FullName | Out-Null
}

$ctest = (Get-Command ctest -ErrorAction Stop).Source
Copy-Item $ctest (Join-Path $Work 'ctest.exe') -Force
Remove-Item (Join-Path $Out 'bundle.zip') -ErrorAction SilentlyContinue
Push-Location $Work
& "$env:SystemRoot\System32\tar.exe" -a -c -f (Join-Path $Out 'bundle.zip') --exclude=*.obj --exclude=*.pdb --exclude=*.ilk --exclude=*.pch --exclude=*.lib --exclude=*.exp --exclude=*.o --exclude=CMakeFiles ctest.exe src
Pop-Location

# The Python that configured the build is the one its test files name.
$python = $null
if (Get-Command py -ErrorAction SilentlyContinue) { $python = (& py -c 'import sys; print(sys.executable)') }
if (-not $python) { $python = (Get-Command python -ErrorAction Stop).Source }
$pythonRoot = Split-Path -Parent $python
Remove-Item (Join-Path $Out 'py.zip') -ErrorAction SilentlyContinue
& "$env:SystemRoot\System32\tar.exe" -a -c -f (Join-Path $Out 'py.zip') -C $pythonRoot .

$pwshRoot = Join-Path $env:ProgramFiles 'PowerShell\7'
Remove-Item (Join-Path $Out 'pwsh.zip') -ErrorAction SilentlyContinue
if (Test-Path $pwshRoot) {
    & "$env:SystemRoot\System32\tar.exe" -a -c -f (Join-Path $Out 'pwsh.zip') -C $pwshRoot .
} else {
    Write-Warning 'PowerShell 7 is not installed here; the vm_* suites will not run in the guest'
}

$runtime = Join-Path $Work 'rt'
New-Item -ItemType Directory -Force $runtime, "$runtime\platforms" | Out-Null
foreach ($name in 'msvcp140', 'msvcp140_1', 'msvcp140_2', 'vcruntime140', 'vcruntime140_1', 'concrt140') {
    Copy-Item "$env:SystemRoot\System32\$name.dll" $runtime
}
Copy-Item (Join-Path $qtRoot 'plugins\platforms\qoffscreen.dll') "$runtime\platforms"
Remove-Item (Join-Path $Out 'rt.zip') -ErrorAction SilentlyContinue
Compress-Archive -Path "$runtime\*" -DestinationPath (Join-Path $Out 'rt.zip') -Force

# The path the build bakes into its test files for Python, as CMake writes it.
$baked = $null
$testfile = Get-ChildItem $source -Recurse -Filter CTestTestfile.cmake | Select-String -Pattern '"([A-Za-z]:/[^"]*python[^"]*\.exe)"' | Select-Object -First 1
if ($testfile) { $baked = $testfile.Matches[0].Groups[1].Value }
[pscustomobject]@{
    sourceRoot = $source
    buildDir   = "build\$preset"
    python     = if ($baked) { $baked } else { $python.Replace('\', '/') }
} | ConvertTo-Json | Set-Content -Path (Join-Path $Out 'bundle.json') -Encoding utf8

Get-ChildItem $Out | ForEach-Object { '{0,-14} {1,8:N0} MB' -f $_.Name, ($_.Length / 1MB) }
