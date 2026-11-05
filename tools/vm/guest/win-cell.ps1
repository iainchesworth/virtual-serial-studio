#!/usr/bin/env pwsh
#
# Copyright (c) 2026 Virtual Serial Studio contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
<#
.SYNOPSIS
    Runs one platform-matrix cell inside a Windows guest. Tools/vm/guest-cell.ps1 copies this
    file and the bundle in and runs it; run it by hand only to repeat a cell.

.DESCRIPTION
    The bundle (see tools/vm/make-bundle.ps1) is in C:\vss: bundle.json, bundle.zip (the source
    and the build tree, built outside the guest), py.zip (a Python), pwsh.zip (PowerShell 7,
    which the vm_* ctest suites need), rt.zip (the Visual C++ runtime and the offscreen Qt
    platform plugin) and, for T9, package.zip.

    It runs ctest (T1), then tools/matrix/run_cell.py, and leaves C:\vss\cell-out.zip.
#>

param(
    [Parameter(Mandatory)] [string] $Cell,
    [Parameter(Mandatory)] [string] $VmName,
    [string] $Commit = ''
)

$ErrorActionPreference = 'Continue'
$root = 'C:\vss'
New-Item -ItemType Directory -Force $root | Out-Null
Set-Location $root
$info = Get-Content "$root\bundle.json" -Raw | ConvertFrom-Json

if (-not (Test-Path "$root\py\python.exe")) { Expand-Archive -Path "$root\py.zip" -DestinationPath "$root\py" -Force }
if (-not (Test-Path "$root\src")) { Expand-Archive -Path "$root\bundle.zip" -DestinationPath $root -Force }
$pwsh7 = 'C:\Program Files\PowerShell\7'
if (-not (Test-Path "$pwsh7\pwsh.exe") -and (Test-Path "$root\pwsh.zip")) {
    Expand-Archive -Path "$root\pwsh.zip" -DestinationPath $pwsh7 -Force
}
$build = Join-Path "$root\src" $info.buildDir
if (Test-Path "$root\rt.zip") { Expand-Archive -Path "$root\rt.zip" -DestinationPath "$build\bin" -Force }

# Some tests read source files through the absolute path they were built with. Give the guest
# that path: a drive letter that points at a folder holding a junction to the source.
$sourceRoot = $info.sourceRoot
$drive = $sourceRoot.Substring(0, 2)
$tail = $sourceRoot.Substring(2).Trim('\')
$parent = "C:\vssd\" + (Split-Path -Parent $tail)
New-Item -ItemType Directory -Force $parent | Out-Null
$junction = "C:\vssd\$tail"
if (-not (Test-Path $junction)) { New-Item -ItemType Junction -Path $junction -Target "$root\src" | Out-Null }
if (-not (Test-Path "$drive\")) { subst $drive C:\vssd }

# The build holds the path of the Python that configured it; point it at the one in the bundle.
$guestPython = ("$root\py\python.exe").Replace('\', '/')
Get-ChildItem "$root\src" -Recurse -Filter CTestTestfile.cmake | ForEach-Object {
    $text = Get-Content $_.FullName -Raw
    $text = $text.Replace($info.python, $guestPython)
    Set-Content -Path $_.FullName -Value $text -NoNewline
}

$src = $sourceRoot
$buildDir = Join-Path $src $info.buildDir
$env:QT_QPA_PLATFORM = 'offscreen'
# The GUI tests write a picture of each window and dialog they check, in both themes, when this
# is set. They come back with the cell output, rendered with this system's own fonts and style.
$env:VSS_QA_SHOTS = "$root\gui-shots"
Remove-Item $env:VSS_QA_SHOTS -Recurse -Force -ErrorAction SilentlyContinue
$env:PATH = "$buildDir\bin;$env:PATH"
Set-Location $src
& "$root\ctest.exe" --test-dir $buildDir -C Release --output-on-failure -j2 2>&1 |
    Out-File -Encoding utf8 "$root\t1.log"
$t1 = $LASTEXITCODE
"ctest exit $t1"
Get-Content "$root\t1.log" -Tail 12

# A few GUI tests wait a fixed few milliseconds for an event, which a slow guest can miss. Run the
# failed suites once more, and say so in the result rather than hide it.
$note = ''
if ($t1 -ne 0) {
    $failed = @(Select-String -Path "$root\t1.log" -Pattern '^\s+\d+ - (\S+) \(' | ForEach-Object { $_.Matches[0].Groups[1].Value })
    "rerunning the failed suites once: $($failed -join ', ')"
    & "$root\ctest.exe" --test-dir $buildDir -C Release --output-on-failure --rerun-failed 2>&1 |
        Out-File -Encoding utf8 -Append "$root\t1.log"
    $t1 = $LASTEXITCODE
    $note = "first pass failed $($failed -join ', '); the rerun " + $(if ($t1 -eq 0) { 'passed' } else { 'failed too' })
}

Remove-Item "$root\cell-out" -Recurse -Force -ErrorAction SilentlyContinue
$arguments = @(
    'tools\matrix\run_cell.py', '--cell', $Cell, '--kind', 'vm', '--vm', $VmName, '--snapshot', 'clean',
    '--build-dir', $buildDir, '--out', "$root\cell-out", '--t1-exit', $t1, '--t1-log', "$root\t1.log"
)
if ($Commit) { $arguments += @('--commit', $Commit) }
if ($note) { $arguments += @('--t1-note', $note) }
if (Test-Path "$root\package.zip") { $arguments += @('--package', "$root\package.zip") }
& "$root\py\python.exe" @arguments
"cell exit $LASTEXITCODE"
if (Test-Path "$root\gui-shots") { Copy-Item "$root\gui-shots" "$root\cell-out\gui-shots" -Recurse -Force }
Compress-Archive -Path "$root\cell-out" -DestinationPath "$root\cell-out.zip" -Force
exit 0
