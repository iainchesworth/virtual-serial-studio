# SPDX-License-Identifier: GPL-3.0-or-later
<#
.SYNOPSIS
    Prepares a Windows VSS guest after the unattended install. It runs once, at the first logon.

.DESCRIPTION
    Installs VMware Tools from the tools disc, turns on test signing, creates C:\vss, keeps the
    screen on, writes C:\vss\ready.txt and powers the guest off so the owner can take the
    "clean" snapshot. The script holds no credentials.
#>

$ErrorActionPreference = 'Continue'
$log = 'C:\vss-setup.log'
New-Item -ItemType Directory -Force -Path 'C:\vss' | Out-Null
Start-Transcript -Path $log -Force | Out-Null

# VMware Tools: the tools disc is the drive that holds setup64.exe.
$tools = Get-PSDrive -PSProvider FileSystem |
    ForEach-Object { Join-Path $_.Root 'setup64.exe' } |
    Where-Object { Test-Path $_ } | Select-Object -First 1
if ($tools) {
    Write-Host "installing VMware Tools from $tools"
    $process = Start-Process -FilePath $tools -ArgumentList '/S /v"/qn REBOOT=R"' -Wait -PassThru
    Write-Host "VMware Tools installer exit code $($process.ExitCode)"
} else {
    Write-Host 'no VMware Tools disc found'
}

# Test signing is what lets the test-signed driver load. It needs Secure Boot off.
bcdedit /set testsigning on | Out-Host

# Keep the desktop on, so a screenshot shows something.
powercfg /change monitor-timeout-ac 0
powercfg /change standby-timeout-ac 0
powercfg /hibernate off
reg add 'HKLM\SOFTWARE\Policies\Microsoft\Windows\Personalization' /v NoLockScreen /t REG_DWORD /d 1 /f | Out-Host

Set-Content -Path 'C:\vss\ready.txt' -Value (Get-Date -Format o)
Stop-Transcript | Out-Null

# Power off after a short delay; the snapshot is taken from the powered-off guest.
shutdown /s /t 45 /c 'VSS guest is ready; take the clean snapshot.'
