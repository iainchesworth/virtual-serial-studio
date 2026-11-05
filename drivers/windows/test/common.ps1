# Copyright (c) 2026 Virtual Serial Studio contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Shared by install-driver.ps1, uninstall-driver.ps1 and test-driver.ps1. Run
# inside a Windows guest, as an administrator. Dot-source it:  . $PSScriptRoot\common.ps1

$HardwareId = 'VSSCOM\VSSCOM0'       # a port made by hand (install-driver.ps1 -Count)
$DemandHardwareId = 'VSSCOM\PORT'    # a port that the control device made
$ControlHardwareId = 'VSSCOM\CONTROL'
$PortsClassGuid = [guid]'4D36E978-E325-11CE-BFC1-08002BE10318'
$SystemClassGuid = [guid]'4D36E97D-E325-11CE-BFC1-08002BE10318'

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
    [StructLayout(LayoutKind.Sequential)]
    struct SP_DEVICE_INTERFACE_DATA
    {
        public uint cbSize;
        public Guid InterfaceClassGuid;
        public uint Flags;
        public IntPtr Reserved;
    }

    [DllImport("setupapi.dll", SetLastError = true)]
    static extern bool SetupDiEnumDeviceInterfaces(IntPtr set, IntPtr info, ref Guid interfaceGuid, uint index,
                                                   ref SP_DEVICE_INTERFACE_DATA data);
    [DllImport("setupapi.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern bool SetupDiGetDeviceInterfaceDetail(IntPtr set, ref SP_DEVICE_INTERFACE_DATA data, IntPtr detail,
                                                       uint size, out uint required, ref SP_DEVINFO_DATA info);
    [DllImport("setupapi.dll", SetLastError = true)]
    static extern IntPtr SetupDiOpenDevRegKey(IntPtr set, ref SP_DEVINFO_DATA data, uint scope, uint profile,
                                              uint keyType, uint access);
    [DllImport("advapi32.dll", CharSet = CharSet.Unicode)]
    static extern int RegQueryValueEx(IntPtr key, string name, IntPtr reserved, out uint type, byte[] data,
                                      ref uint size);
    [DllImport("advapi32.dll")]
    static extern int RegCloseKey(IntPtr key);
    [DllImport("newdev.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern bool UpdateDriverForPlugAndPlayDevices(IntPtr owner, string hardwareId, string infPath,
                                                         uint flags, out bool rebootRequired);

    static byte[] MultiSz(string value)
    {
        return Encoding.Unicode.GetBytes(value + "\0\0");
    }

    /// Creates the root-enumerated device and binds the driver in infPath to it.
    /// Returns true when Windows asks for a restart.
    public static bool CreateDevice(Guid classGuid, string hardwareId, string infPath, string deviceName)
    {
        IntPtr set = SetupDiCreateDeviceInfoList(ref classGuid, IntPtr.Zero);
        if (set == InvalidHandle) throw new Win32Exception(Marshal.GetLastWin32Error(), "SetupDiCreateDeviceInfoList");
        try
        {
            var data = new SP_DEVINFO_DATA { cbSize = (uint)Marshal.SizeOf(typeof(SP_DEVINFO_DATA)) };
            if (!SetupDiCreateDeviceInfo(set, deviceName, ref classGuid, null, IntPtr.Zero, DICD_GENERATE_ID, ref data))
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

    /// The present device interfaces of a class, as "COMn|path" strings. The path ends
    /// in the reference string of the interface, so it opens that end of the port.
    public static string[] Interfaces(Guid interfaceGuid)
    {
        const uint DIGCF_PRESENT_INTERFACES = 0x12; // DIGCF_PRESENT | DIGCF_DEVICEINTERFACE
        IntPtr set = SetupDiGetClassDevs(ref interfaceGuid, IntPtr.Zero, IntPtr.Zero, DIGCF_PRESENT_INTERFACES);
        if (set == InvalidHandle) throw new Win32Exception(Marshal.GetLastWin32Error(), "SetupDiGetClassDevs");
        var found = new System.Collections.Generic.List<string>();
        try
        {
            for (uint index = 0; ; index++)
            {
                var data = new SP_DEVICE_INTERFACE_DATA { cbSize = (uint)Marshal.SizeOf(typeof(SP_DEVICE_INTERFACE_DATA)) };
                if (!SetupDiEnumDeviceInterfaces(set, IntPtr.Zero, ref interfaceGuid, index, ref data)) break;

                var info = new SP_DEVINFO_DATA { cbSize = (uint)Marshal.SizeOf(typeof(SP_DEVINFO_DATA)) };
                uint required;
                SetupDiGetDeviceInterfaceDetail(set, ref data, IntPtr.Zero, 0, out required, ref info);
                IntPtr detail = Marshal.AllocHGlobal((int)required);
                try
                {
                    // cbSize of SP_DEVICE_INTERFACE_DETAIL_DATA_W: 8 on 64-bit, 6 on 32-bit.
                    Marshal.WriteInt32(detail, IntPtr.Size == 8 ? 8 : 6);
                    if (!SetupDiGetDeviceInterfaceDetail(set, ref data, detail, required, out required, ref info))
                        throw new Win32Exception(Marshal.GetLastWin32Error(), "SetupDiGetDeviceInterfaceDetail");
                    string path = Marshal.PtrToStringUni(new IntPtr(detail.ToInt64() + 4));

                    string port = "";
                    IntPtr key = SetupDiOpenDevRegKey(set, ref info, 1, 0, DIREG_DEV, 0x20019);
                    if (key != InvalidHandle)
                    {
                        var buffer = new byte[64];
                        uint type, size = (uint)buffer.Length;
                        if (RegQueryValueEx(key, "PortName", IntPtr.Zero, out type, buffer, ref size) == 0)
                            port = Encoding.Unicode.GetString(buffer, 0, (int)size).TrimEnd('\0');
                        RegCloseKey(key);
                    }
                    found.Add(port + "|" + path);
                }
                finally { Marshal.FreeHGlobal(detail); }
            }
        }
        finally { SetupDiDestroyDeviceInfoList(set); }
        return found.ToArray();
    }

    /// Removes every device with the hardware id: the present ones, and with includeAbsent
    /// the ones that have gone but left their registry entries (and their COM names)
    /// behind. Returns how many.
    public static int RemoveDevices(Guid classGuid, string hardwareId, bool includeAbsent)
    {
        IntPtr set = SetupDiGetClassDevs(ref classGuid, IntPtr.Zero, IntPtr.Zero, includeAbsent ? 0u : DIGCF_PRESENT);
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

# The COM names of the present vsscom ports, in order.
function Get-VssComPorts {
    $names = foreach ($device in (Get-CimInstance Win32_PnPEntity | Where-Object { $_.HardwareID -contains $HardwareId })) {
        if ($device.Name -match '\((COM\d+)\)') { $Matches[1] }
    }
    @($names | Sort-Object { [int]($_ -replace '\D', '') })
}

# The COM name of the first present vsscom port, or $null.
function Get-VssComPort {
    $ports = Get-VssComPorts
    if ($ports.Count -eq 0) { return $null }
    $ports[0]
}

# COM name -> hub path, for the present vsscom ports. A port with no hub
# interface is missing from the table.
$VssInterfaceGuid = [guid]'7c9a1f62-3d54-4a6b-9d3e-5f0c2b8a4d11'
function Get-VssHubPaths {
    $table = @{}
    foreach ($line in [VssSetup]::Interfaces($VssInterfaceGuid)) {
        $name, $path = $line -split '\|', 2
        if ($name) { $table[$name] = $path }
    }
    $table
}

# The path of the control device's interface, or $null when it is not installed.
$VssControlGuid = [guid]'b3d7e0a4-2f81-4c59-8e16-7a5d9c3f0b62'
function Get-VssControlPath {
    foreach ($line in [VssSetup]::Interfaces($VssControlGuid)) {
        return ($line -split '\|', 2)[1]
    }
    $null
}

# The port the control device made in `$Slot`, as @{ Port; Path }, or $null while it is
# not there. Its hub path holds the device instance id: \\?\vsscom#port#<slot>#{guid}\hub
function Find-VssDemandPort([uint32] $Slot) {
    $pattern = '*\vsscom#port#{0:x4}#*' -f $Slot
    foreach ($line in [VssSetup]::Interfaces($VssInterfaceGuid)) {
        $name, $path = $line -split '\|', 2
        if ($path -like $pattern) { return @{ Port = $name; Path = $path } }
    }
    $null
}

# Waits until a port made on demand is there (or, with -Gone, until it has gone).
function Wait-VssDemandPort([uint32] $Slot, [int] $Seconds = 40, [switch] $Gone) {
    $until = (Get-Date).AddSeconds($Seconds)
    do {
        $found = Find-VssDemandPort $Slot
        if ($Gone -and -not $found) { return $true }
        if (-not $Gone -and $found) { return $found }
        Start-Sleep -Milliseconds 250
    } while ((Get-Date) -lt $until)
    if ($Gone) { return $false }
    $null
}

# True while HARDWARE\DEVICEMAP\SERIALCOMM lists the COM name.
function Test-VssSerialComm([string] $Port) {
    $key = Get-ItemProperty 'HKLM:\HARDWARE\DEVICEMAP\SERIALCOMM' -ErrorAction SilentlyContinue
    if (-not $key) { return $false }
    [bool]($key.PSObject.Properties | Where-Object { $_.Value -eq $Port })
}
