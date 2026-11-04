# Copyright (c) 2026 Virtual Serial Studio contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Shared by install-driver.ps1, uninstall-driver.ps1 and test-driver.ps1. Run
# inside a Windows guest, as an administrator. Dot-source it:  . $PSScriptRoot\common.ps1

$HardwareId = 'VSSCOM\VSSCOM0'
$PortsClassGuid = [guid]'4D36E978-E325-11CE-BFC1-08002BE10318'

Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;

public static class VssSetup
{
    const uint DIF_REGISTERDEVICE = 0x19;
    const uint DICD_GENERATE_ID = 1;
    const uint SPDRP_HARDWAREID = 1;
    const uint DIGCF_PRESENT = 2;
    const uint DIREG_DEV = 1;
    const uint DIF_REMOVE = 5;
    const uint INSTALLFLAG_FORCE = 1;
    static readonly IntPtr InvalidHandle = new IntPtr(-1);

    [StructLayout(LayoutKind.Sequential)]
    struct SP_DEVINFO_DATA
    {
        public uint cbSize;
        public Guid ClassGuid;
        public uint DevInst;
        public IntPtr Reserved;
    }

    [DllImport("setupapi.dll", SetLastError = true)]
    static extern IntPtr SetupDiCreateDeviceInfoList(ref Guid classGuid, IntPtr parent);
    [DllImport("setupapi.dll", SetLastError = true)]
    static extern IntPtr SetupDiGetClassDevs(ref Guid classGuid, IntPtr enumerator, IntPtr parent, uint flags);
    [DllImport("setupapi.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern bool SetupDiCreateDeviceInfo(IntPtr set, string name, ref Guid classGuid, string description,
                                               IntPtr parent, uint flags, ref SP_DEVINFO_DATA data);
    [DllImport("setupapi.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern bool SetupDiSetDeviceRegistryProperty(IntPtr set, ref SP_DEVINFO_DATA data, uint property,
                                                        byte[] buffer, uint size);
    [DllImport("setupapi.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern bool SetupDiGetDeviceRegistryProperty(IntPtr set, ref SP_DEVINFO_DATA data, uint property,
                                                        out uint type, byte[] buffer, uint size, out uint required);
    [DllImport("setupapi.dll", SetLastError = true)]
    static extern bool SetupDiCallClassInstaller(uint function, IntPtr set, ref SP_DEVINFO_DATA data);
    [DllImport("setupapi.dll", SetLastError = true)]
    static extern bool SetupDiEnumDeviceInfo(IntPtr set, uint index, ref SP_DEVINFO_DATA data);
    [DllImport("setupapi.dll", SetLastError = true)]
    static extern bool SetupDiDestroyDeviceInfoList(IntPtr set);
    [DllImport("newdev.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern bool UpdateDriverForPlugAndPlayDevices(IntPtr owner, string hardwareId, string infPath,
                                                         uint flags, out bool rebootRequired);

    static byte[] MultiSz(string value)
    {
        return Encoding.Unicode.GetBytes(value + "\0\0");
    }

    /// Creates the root-enumerated device and binds the driver in infPath to it.
    /// Returns true when Windows asks for a restart.
    public static bool CreateDevice(Guid classGuid, string hardwareId, string infPath)
    {
        IntPtr set = SetupDiCreateDeviceInfoList(ref classGuid, IntPtr.Zero);
        if (set == InvalidHandle) throw new Win32Exception(Marshal.GetLastWin32Error(), "SetupDiCreateDeviceInfoList");
        try
        {
            var data = new SP_DEVINFO_DATA { cbSize = (uint)Marshal.SizeOf(typeof(SP_DEVINFO_DATA)) };
            if (!SetupDiCreateDeviceInfo(set, "Ports", ref classGuid, null, IntPtr.Zero, DICD_GENERATE_ID, ref data))
                throw new Win32Exception(Marshal.GetLastWin32Error(), "SetupDiCreateDeviceInfo");
            byte[] id = MultiSz(hardwareId);
            if (!SetupDiSetDeviceRegistryProperty(set, ref data, SPDRP_HARDWAREID, id, (uint)id.Length))
                throw new Win32Exception(Marshal.GetLastWin32Error(), "SetupDiSetDeviceRegistryProperty");
            if (!SetupDiCallClassInstaller(DIF_REGISTERDEVICE, set, ref data))
                throw new Win32Exception(Marshal.GetLastWin32Error(), "DIF_REGISTERDEVICE");
        }
        finally { SetupDiDestroyDeviceInfoList(set); }

        bool reboot;
        if (!UpdateDriverForPlugAndPlayDevices(IntPtr.Zero, hardwareId, infPath, INSTALLFLAG_FORCE, out reboot))
            throw new Win32Exception(Marshal.GetLastWin32Error(), "UpdateDriverForPlugAndPlayDevices");
        return reboot;
    }

    /// Removes every present device with the hardware id. Returns how many.
    public static int RemoveDevices(Guid classGuid, string hardwareId)
    {
        IntPtr set = SetupDiGetClassDevs(ref classGuid, IntPtr.Zero, IntPtr.Zero, DIGCF_PRESENT);
        if (set == InvalidHandle) throw new Win32Exception(Marshal.GetLastWin32Error(), "SetupDiGetClassDevs");
        int removed = 0;
        try
        {
            for (uint index = 0; ; index++)
            {
                var data = new SP_DEVINFO_DATA { cbSize = (uint)Marshal.SizeOf(typeof(SP_DEVINFO_DATA)) };
                if (!SetupDiEnumDeviceInfo(set, index, ref data)) break;
                var buffer = new byte[1024];
                uint type, required;
                if (!SetupDiGetDeviceRegistryProperty(set, ref data, SPDRP_HARDWAREID, out type, buffer,
                                                      (uint)buffer.Length, out required))
                    continue;
                string ids = Encoding.Unicode.GetString(buffer, 0, (int)required);
                if (ids.IndexOf(hardwareId, StringComparison.OrdinalIgnoreCase) < 0) continue;
                if (!SetupDiCallClassInstaller(DIF_REMOVE, set, ref data))
                    throw new Win32Exception(Marshal.GetLastWin32Error(), "DIF_REMOVE");
                removed++;
            }
        }
        finally { SetupDiDestroyDeviceInfoList(set); }
        return removed;
    }
}
'@

function Write-Result {
    param([string] $Path, [string] $Phase, [object[]] $Tests)
    if (-not $Path) { return }
    $os = Get-CimInstance Win32_OperatingSystem
    $result = [ordered]@{
        phase   = $Phase
        host    = $env:COMPUTERNAME
        windows = "$($os.Caption) $($os.Version)"
        when    = (Get-Date).ToUniversalTime().ToString('o')
        tests   = $Tests
    }
    New-Item -ItemType Directory -Force (Split-Path -Parent $Path) | Out-Null
    $result | ConvertTo-Json -Depth 6 | Set-Content -Path $Path -Encoding UTF8
}

function New-TestRecord {
    param([string] $Id, [string] $Name, [string] $Status, [string] $Detail = '')
    [ordered]@{ id = $Id; name = $Name; status = $Status; detail = $Detail }
}

# The COM name of the first present vsscom port, or $null.
function Get-VssComPort {
    $device = Get-CimInstance Win32_PnPEntity |
        Where-Object { $_.HardwareID -contains $HardwareId } |
        Select-Object -First 1
    if (-not $device) { return $null }
    if ($device.Name -match '\((COM\d+)\)') { return $Matches[1] }
    return $null
}
