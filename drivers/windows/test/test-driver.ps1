# Copyright (c) 2026 Virtual Serial Studio contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Drives the installed vsscom port through the Win32 serial API, the way real
# programs do. Run inside a guest, as an administrator, after install-driver.ps1.
#
#   test-driver.ps1 [-ResultPath <json>]
#
# Tests (the driver's own, named D to keep them apart from the platform cells):
#   D1 enumeration     the Ports device, SERIALCOMM and GetPortNames agree
#   D2 configuration   baud, bits, parity and stop bits round-trip; data loops back
#   D3 timeouts        the documented SetCommTimeouts combinations
#   D4 pending I/O     a waiting read, CancelIoEx, PurgeComm, closing the handle
#   D5 events          SetCommMask and WaitCommEvent
#   D6 lines           DTR and RTS come back as DSR, DCD and CTS
#   D7 exclusive       a second CreateFile fails with access denied
#
# The port is a loopback: what is written comes back from the next read.
# Exit code 0 when every test passed.

[CmdletBinding()]
param(
    [string] $ResultPath
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')

Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;

public sealed class PendingIo : IDisposable
{
    [StructLayout(LayoutKind.Sequential)]
    struct OVERLAPPED { public IntPtr Internal, InternalHigh; public uint Offset, OffsetHigh; public IntPtr Event; }

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool ReadFile(IntPtr h, IntPtr buffer, uint length, IntPtr read, IntPtr overlapped);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool WaitCommEvent(IntPtr h, IntPtr mask, IntPtr overlapped);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool GetOverlappedResult(IntPtr h, IntPtr overlapped, out uint transferred, bool wait);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool CancelIoEx(IntPtr h, IntPtr overlapped);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern IntPtr CreateEvent(IntPtr attributes, bool manual, bool initial, string name);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern uint WaitForSingleObject(IntPtr h, uint ms);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool CloseHandle(IntPtr h);

    readonly IntPtr handle;
    IntPtr overlapped = Marshal.AllocHGlobal(Marshal.SizeOf(typeof(OVERLAPPED)));
    IntPtr buffer;
    IntPtr signal;
    public int Length;

    PendingIo(IntPtr h, int length)
    {
        handle = h;
        Length = length;
        buffer = Marshal.AllocHGlobal(Math.Max(length, 4));
        signal = CreateEvent(IntPtr.Zero, true, false, null);
        var ov = new OVERLAPPED { Event = signal };
        Marshal.StructureToPtr(ov, overlapped, false);
    }

    /// Starts a read of `length` bytes. It may already be finished.
    public static PendingIo BeginRead(IntPtr h, int length)
    {
        var io = new PendingIo(h, length);
        if (!ReadFile(h, io.buffer, (uint)length, IntPtr.Zero, io.overlapped) && Marshal.GetLastWin32Error() != 997)
            throw new Win32Exception(Marshal.GetLastWin32Error(), "ReadFile");
        return io;
    }

    /// Starts WaitCommEvent. It may already be finished.
    public static PendingIo BeginWait(IntPtr h)
    {
        var io = new PendingIo(h, 4);
        if (!WaitCommEvent(h, io.buffer, io.overlapped) && Marshal.GetLastWin32Error() != 997)
            throw new Win32Exception(Marshal.GetLastWin32Error(), "WaitCommEvent");
        return io;
    }

    public bool Completed(int ms) { return WaitForSingleObject(signal, (uint)ms) == 0; }

    /// The byte count; `error` is 0 or the Win32 error the operation ended with.
    public int Finish(bool wait, out int error)
    {
        uint n;
        error = 0;
        if (!GetOverlappedResult(handle, overlapped, out n, wait)) error = Marshal.GetLastWin32Error();
        return (int)n;
    }

    public bool Cancel() { return CancelIoEx(handle, overlapped); }

    public byte[] Data(int n)
    {
        var bytes = new byte[n];
        if (n > 0) Marshal.Copy(buffer, bytes, 0, n);
        return bytes;
    }

    public uint Mask() { return (uint)Marshal.ReadInt32(buffer); }

    public void Dispose()
    {
        if (signal != IntPtr.Zero) { CloseHandle(signal); signal = IntPtr.Zero; }
        if (overlapped != IntPtr.Zero) { Marshal.FreeHGlobal(overlapped); overlapped = IntPtr.Zero; }
        if (buffer != IntPtr.Zero) { Marshal.FreeHGlobal(buffer); buffer = IntPtr.Zero; }
    }
}

public static class VssComm
{
    [StructLayout(LayoutKind.Sequential)]
    struct DCB
    {
        public uint DCBlength, BaudRate, Flags;
        public ushort wReserved, XonLim, XoffLim;
        public byte ByteSize, Parity, StopBits;
        public sbyte XonChar, XoffChar, ErrorChar, EofChar, EvtChar;
        public ushort wReserved1;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct COMMTIMEOUTS { public uint ReadInterval, ReadMultiplier, ReadConstant, WriteMultiplier, WriteConstant; }

    [StructLayout(LayoutKind.Sequential)]
    struct OVERLAPPED { public IntPtr Internal, InternalHigh; public uint Offset, OffsetHigh; public IntPtr Event; }

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern IntPtr CreateFile(string name, uint access, uint share, IntPtr security, uint disposition, uint flags, IntPtr template);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool GetCommState(IntPtr h, ref DCB dcb);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool SetCommState(IntPtr h, ref DCB dcb);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool GetCommTimeouts(IntPtr h, out COMMTIMEOUTS t);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool SetCommTimeouts(IntPtr h, ref COMMTIMEOUTS t);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool SetCommMask(IntPtr h, uint mask);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool GetCommModemStatus(IntPtr h, out uint status);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool EscapeCommFunction(IntPtr h, uint function);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool PurgeComm(IntPtr h, uint flags);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool WriteFile(IntPtr h, byte[] buffer, uint length, out uint written, IntPtr overlapped);

    public const int InvalidHandle = -1;

    /// Opens \\.\COMn for overlapped I/O. Returns IntPtr(-1) and the error on failure.
    public static IntPtr Open(string port, out int error)
    {
        IntPtr h = CreateFile("\\\\.\\" + port, 0xC0000000, 0, IntPtr.Zero, 3, 0x40000000, IntPtr.Zero);
        error = h == new IntPtr(-1) ? Marshal.GetLastWin32Error() : 0;
        return h;
    }

    public static void Close(IntPtr h) { CloseHandle(h); }

    public static bool SetState(IntPtr h, uint baud, byte bits, byte parity, byte stop)
    {
        var dcb = new DCB { DCBlength = (uint)Marshal.SizeOf(typeof(DCB)) };
        if (!GetCommState(h, ref dcb)) return false;
        dcb.BaudRate = baud; dcb.ByteSize = bits; dcb.Parity = parity; dcb.StopBits = stop;
        return SetCommState(h, ref dcb);
    }

    /// baud, bits, parity, stop; null when the call fails.
    public static uint[] GetState(IntPtr h)
    {
        var dcb = new DCB { DCBlength = (uint)Marshal.SizeOf(typeof(DCB)) };
        if (!GetCommState(h, ref dcb)) return null;
        return new uint[] { dcb.BaudRate, dcb.ByteSize, dcb.Parity, dcb.StopBits };
    }

    public static bool SetTimeouts(IntPtr h, uint interval, uint rmul, uint rconst, uint wmul, uint wconst)
    {
        var t = new COMMTIMEOUTS { ReadInterval = interval, ReadMultiplier = rmul, ReadConstant = rconst, WriteMultiplier = wmul, WriteConstant = wconst };
        return SetCommTimeouts(h, ref t);
    }

    public static uint[] GetTimeouts(IntPtr h)
    {
        COMMTIMEOUTS t;
        if (!GetCommTimeouts(h, out t)) return null;
        return new uint[] { t.ReadInterval, t.ReadMultiplier, t.ReadConstant, t.WriteMultiplier, t.WriteConstant };
    }

    public static bool SetMask(IntPtr h, uint mask) { return SetCommMask(h, mask); }
    public static bool Escape(IntPtr h, uint function) { return EscapeCommFunction(h, function); }
    public static bool Purge(IntPtr h, uint flags) { return PurgeComm(h, flags); }

    public static uint ModemStatus(IntPtr h)
    {
        uint status;
        if (!GetCommModemStatus(h, out status)) throw new Win32Exception(Marshal.GetLastWin32Error(), "GetCommModemStatus");
        return status;
    }

    /// Writes with an overlapped handle and waits for the result.
    public static int Write(IntPtr h, byte[] data)
    {
        // Allocated with the OVERLAPPED so the write may complete after the call returns.
        IntPtr ov = Marshal.AllocHGlobal(Marshal.SizeOf(typeof(OVERLAPPED)));
        IntPtr ev = CreateEventW(IntPtr.Zero, true, false, null);
        try
        {
            Marshal.StructureToPtr(new OVERLAPPED { Event = ev }, ov, false);
            uint written;
            if (!WriteFile(h, data, (uint)data.Length, out written, ov))
            {
                if (Marshal.GetLastWin32Error() != 997) return -1;
                if (!GetOverlappedResult(h, ov, out written, true)) return -1;
            }
            return (int)written;
        }
        finally { CloseHandle(ev); Marshal.FreeHGlobal(ov); }
    }

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool GetOverlappedResult(IntPtr h, IntPtr overlapped, out uint transferred, bool wait);
    [DllImport("kernel32.dll", SetLastError = true, EntryPoint = "CreateEventW", CharSet = CharSet.Unicode)]
    static extern IntPtr CreateEventW(IntPtr attributes, bool manual, bool initial, string name);
}
'@

# Win32 constants.
$MS_CTS = 0x10; $MS_DSR = 0x20; $MS_DCD = 0x80
$EV_RXCHAR = 0x1; $EV_TXEMPTY = 0x4; $EV_CTS = 0x8; $EV_DSR = 0x10
$SETDTR = 5; $CLRDTR = 6; $SETRTS = 3; $CLRRTS = 4
$PURGE_RXABORT = 0x2; $PURGE_RXCLEAR = 0x8
$ERROR_ACCESS_DENIED = 5; $ERROR_OPERATION_ABORTED = 995
$MAX = [uint32]::MaxValue

$tests = @()
function Add-Test([string] $Id, [string] $Name, [scriptblock] $Body) {
    try {
        $detail = & $Body
        $script:tests += New-TestRecord $Id $Name 'pass' ($detail -join '; ')
    } catch {
        $script:tests += New-TestRecord $Id $Name 'fail' $_.Exception.Message
    }
}
function Assert([bool] $Condition, [string] $Message) { if (-not $Condition) { throw $Message } }

function Open-Port([string] $Name) {
    $error_ = 0
    $h = [VssComm]::Open($Name, [ref]$error_)
    if ($h -eq [IntPtr](-1)) { throw "CreateFile($Name) failed with error $error_" }
    $h
}

function Read-Now([IntPtr] $H, [int] $Length) {
    # Reads once and returns the bytes and how long the call took.
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $io = [PendingIo]::BeginRead($H, $Length)
    try {
        if (-not $io.Completed(10000)) { $io.Cancel() | Out-Null; throw 'a read did not finish in 10 seconds' }
        $err = 0
        $n = $io.Finish($true, [ref]$err)
        [pscustomobject]@{ Bytes = $io.Data($n); Count = $n; Error = $err; Ms = $watch.ElapsedMilliseconds }
    } finally { $io.Dispose() }
}

$port = Get-VssComPort
if (-not $port) {
    $tests += New-TestRecord 'D0' 'a vsscom port is installed' 'fail' 'run install-driver.ps1 first'
    Write-Result -Path $ResultPath -Phase 'test' -Tests $tests
    $tests | ForEach-Object { '{0,-6} {1,-4} {2}' -f $_.status, $_.id, $_.detail }
    exit 1
}
$tests += New-TestRecord 'D0' 'a vsscom port is installed' 'pass' $port

Add-Test 'D1' 'the Ports device, SERIALCOMM and GetPortNames agree' {
    $listed = (Get-ItemProperty 'HKLM:\HARDWARE\DEVICEMAP\SERIALCOMM').PSObject.Properties | ForEach-Object { $_.Value }
    Assert ($listed -contains $port) "$port is not in SERIALCOMM"
    $names = [System.IO.Ports.SerialPort]::GetPortNames()
    Assert ($names -contains $port) "$port is not in GetPortNames: $($names -join ',')"
    # Windows lists a port in Win32_SerialPort only for the inbox serial stack, so
    # a function driver such as this one is not in it (seen on Windows 11 25H2).
    # Programs that find ports through SetupAPI, SERIALCOMM or GetPortNames, as
    # pyserial and Qt do, see it. The test therefore checks the Ports device
    # itself and only reports what the WMI class says.
    $device = Get-PnpDevice -Class Ports -PresentOnly |
        Where-Object { $_.FriendlyName -like "*($port)" }
    Assert ($null -ne $device) "no Ports device is named for $port"
    Assert ($device.Status -eq 'OK') "the Ports device status is $($device.Status)"
    $serial = @(Get-CimInstance Win32_SerialPort | ForEach-Object { $_.DeviceID })
    $wmi = if ($serial -contains $port) { 'listed' } else { 'not listed' }
    "port $port; Win32_SerialPort $wmi"
}

Add-Test 'D2' 'line settings round-trip and bytes loop back' {
    $h = Open-Port $port
    try {
        foreach ($case in @(@(9600, 8, 0, 0), @(57600, 7, 2, 2), @(115200, 8, 1, 0))) {
            Assert ([VssComm]::SetState($h, $case[0], $case[1], $case[2], $case[3])) "SetCommState failed for $($case -join '/')"
            $got = [VssComm]::GetState($h)
            Assert ($null -ne $got) 'GetCommState failed'
            Assert (($got -join '/') -eq ($case -join '/')) "set $($case -join '/') but read $($got -join '/')"
        }
        Assert ([VssComm]::SetTimeouts($h, $MAX, 0, 0, 0, 0)) 'SetCommTimeouts failed'
        $sent = [VssComm]::Write($h, [Text.Encoding]::ASCII.GetBytes('hello'))
        Assert ($sent -eq 5) "wrote $sent bytes"
        $r = Read-Now $h 5
        Assert ($r.Count -eq 5 -and [Text.Encoding]::ASCII.GetString($r.Bytes) -eq 'hello') "read back $($r.Count) bytes"
        'round trips and loopback'
    } finally { [VssComm]::Close($h) }
}

Add-Test 'D3' 'read timeouts follow SetCommTimeouts' {
    $h = Open-Port $port
    $notes = @()
    try {
        # Interval MAXDWORD alone: return at once, even with nothing to read.
        [VssComm]::SetTimeouts($h, $MAX, 0, 0, 0, 0) | Out-Null
        $r = Read-Now $h 8
        Assert ($r.Count -eq 0 -and $r.Ms -lt 300) "immediate read took $($r.Ms) ms and got $($r.Count)"
        $notes += "immediate $($r.Ms) ms"

        # A constant total timeout: wait about that long for nothing.
        [VssComm]::SetTimeouts($h, 0, 0, 400, 0, 0) | Out-Null
        $r = Read-Now $h 8
        Assert ($r.Count -eq 0 -and $r.Ms -ge 300 -and $r.Ms -lt 1500) "400 ms timeout took $($r.Ms) ms and got $($r.Count)"
        $notes += "total 400 ms took $($r.Ms) ms"

        # Per-byte multiplier plus constant, with 5 of 20 bytes available.
        [VssComm]::SetTimeouts($h, 0, 20, 100, 0, 0) | Out-Null
        [VssComm]::Write($h, [Text.Encoding]::ASCII.GetBytes('abcde')) | Out-Null
        $r = Read-Now $h 20
        Assert ($r.Count -eq 5) "multiplier read returned $($r.Count) bytes"
        Assert ($r.Ms -ge 400 -and $r.Ms -lt 2000) "20x20+100 ms timeout took $($r.Ms) ms"
        $notes += "multiplier $($r.Ms) ms"

        # Interval timeout: the gap after the first byte ends the read.
        [VssComm]::SetTimeouts($h, 150, 0, 0, 0, 0) | Out-Null
        [VssComm]::Write($h, [Text.Encoding]::ASCII.GetBytes('xy')) | Out-Null
        $r = Read-Now $h 10
        Assert ($r.Count -eq 2 -and $r.Ms -ge 100 -and $r.Ms -lt 1500) "interval read returned $($r.Count) in $($r.Ms) ms"
        $notes += "interval $($r.Ms) ms"

        # MAXDWORD, MAXDWORD, constant: return as soon as one byte is there.
        [VssComm]::SetTimeouts($h, $MAX, $MAX, 1000, 0, 0) | Out-Null
        $io = [PendingIo]::BeginRead($h, 10)
        try {
            Start-Sleep -Milliseconds 150
            Assert (-not $io.Completed(0)) 'the read finished before any byte arrived'
            [VssComm]::Write($h, [Text.Encoding]::ASCII.GetBytes('z')) | Out-Null
            Assert ($io.Completed(800)) 'the read did not finish when a byte arrived'
            $err = 0
            $n = $io.Finish($true, [ref]$err)
            Assert ($n -eq 1) "first-byte read returned $n bytes"
        } finally { $io.Dispose() }
        $notes += 'first byte'

        # All three at MAXDWORD is refused.
        Assert (-not [VssComm]::SetTimeouts($h, $MAX, $MAX, $MAX, 0, 0)) 'SetCommTimeouts accepted all MAXDWORD'
        $got = [VssComm]::GetTimeouts($h)
        Assert (($got -join ',') -eq "$MAX,$MAX,1000,0,0") "timeouts changed to $($got -join ',')"
        $notes
    } finally { [VssComm]::Close($h) }
}

Add-Test 'D4' 'a waiting read can be cancelled, purged and closed' {
    $notes = @()
    $h = Open-Port $port
    try {
        [VssComm]::SetTimeouts($h, 0, 0, 0, 0, 0) | Out-Null   # wait forever

        $io = [PendingIo]::BeginRead($h, 4)
        try {
            Assert (-not $io.Completed(200)) 'a read with nothing to read finished'
            Assert ($io.Cancel()) 'CancelIoEx failed'
            Assert ($io.Completed(2000)) 'the cancelled read did not finish'
            $err = 0
            $null = $io.Finish($true, [ref]$err)
            Assert ($err -eq $ERROR_OPERATION_ABORTED) "cancelled read ended with error $err"
        } finally { $io.Dispose() }
        $notes += 'CancelIoEx'

        $io = [PendingIo]::BeginRead($h, 4)
        try {
            Assert (-not $io.Completed(200)) 'a read with nothing to read finished'
            Assert ([VssComm]::Purge($h, $PURGE_RXABORT)) 'PurgeComm failed'
            Assert ($io.Completed(2000)) 'PurgeComm(RXABORT) did not end the read'
        } finally { $io.Dispose() }
        $notes += 'PurgeComm'

        # Data left in the buffer is dropped by RXCLEAR.
        [VssComm]::Write($h, [Text.Encoding]::ASCII.GetBytes('stale')) | Out-Null
        Assert ([VssComm]::Purge($h, $PURGE_RXCLEAR)) 'PurgeComm(RXCLEAR) failed'
        [VssComm]::SetTimeouts($h, $MAX, 0, 0, 0, 0) | Out-Null
        $r = Read-Now $h 8
        Assert ($r.Count -eq 0) "RXCLEAR left $($r.Count) bytes"
        $notes += 'RXCLEAR'
    } finally { [VssComm]::Close($h) }

    # Closing a handle that has a read waiting must not hang.
    $h = Open-Port $port
    [VssComm]::SetTimeouts($h, 0, 0, 0, 0, 0) | Out-Null
    $io = [PendingIo]::BeginRead($h, 4)
    $watch = [Diagnostics.Stopwatch]::StartNew()
    [VssComm]::Close($h)
    Assert ($watch.ElapsedMilliseconds -lt 3000) "CloseHandle with a read waiting took $($watch.ElapsedMilliseconds) ms"
    Assert ($io.Completed(3000)) 'the read did not finish when the handle closed'
    $io.Dispose()
    $notes += "close $($watch.ElapsedMilliseconds) ms"

    # The port opens again afterwards.
    [VssComm]::Close((Open-Port $port))
    $notes
}

Add-Test 'D5' 'SetCommMask and WaitCommEvent report events once and block otherwise' {
    $notes = @()
    $h = Open-Port $port
    try {
        [VssComm]::SetTimeouts($h, $MAX, 0, 0, 0, 0) | Out-Null
        Assert ([VssComm]::SetMask($h, $EV_RXCHAR)) 'SetCommMask failed'

        $io = [PendingIo]::BeginWait($h)
        try {
            Assert (-not $io.Completed(200)) 'WaitCommEvent finished with no event'
            [VssComm]::Write($h, [Text.Encoding]::ASCII.GetBytes('e')) | Out-Null
            Assert ($io.Completed(2000)) 'WaitCommEvent did not finish when a byte arrived'
            $err = 0
            $null = $io.Finish($true, [ref]$err)
            Assert ($err -eq 0 -and ($io.Mask() -band $EV_RXCHAR) -ne 0) "event mask was 0x$('{0:x}' -f $io.Mask()), error $err"
        } finally { $io.Dispose() }
        $notes += 'EV_RXCHAR'
        $null = Read-Now $h 8

        # A new mask releases a waiting call with no events.
        $io = [PendingIo]::BeginWait($h)
        try {
            Assert (-not $io.Completed(200)) 'WaitCommEvent finished with no event'
            Assert ([VssComm]::SetMask($h, 0)) 'SetCommMask(0) failed'
            Assert ($io.Completed(2000)) 'SetCommMask(0) did not release the wait'
            $err = 0
            $null = $io.Finish($true, [ref]$err)
            Assert ($io.Mask() -eq 0) "released wait reported 0x$('{0:x}' -f $io.Mask())"
        } finally { $io.Dispose() }
        $notes += 'mask change releases'

        # A modem-line change is an event too.
        Assert ([VssComm]::SetMask($h, $EV_CTS)) 'SetCommMask(EV_CTS) failed'
        $io = [PendingIo]::BeginWait($h)
        try {
            Assert (-not $io.Completed(200)) 'WaitCommEvent finished with no event'
            [VssComm]::Escape($h, $SETRTS) | Out-Null
            Assert ($io.Completed(2000)) 'RTS did not raise EV_CTS'
            $err = 0
            $null = $io.Finish($true, [ref]$err)
            Assert (($io.Mask() -band $EV_CTS) -ne 0) "event mask was 0x$('{0:x}' -f $io.Mask())"
        } finally { $io.Dispose() }
        [VssComm]::Escape($h, $CLRRTS) | Out-Null
        $notes += 'EV_CTS'
        [VssComm]::SetMask($h, 0) | Out-Null
        $notes
    } finally { [VssComm]::Close($h) }
}

Add-Test 'D6' 'DTR and RTS come back as DSR, DCD and CTS' {
    $h = Open-Port $port
    try {
        $status = [VssComm]::ModemStatus($h)
        Assert (($status -band ($MS_CTS -bor $MS_DSR -bor $MS_DCD)) -eq 0) "lines were up at the start: 0x$('{0:x}' -f $status)"

        [VssComm]::Escape($h, $SETRTS) | Out-Null
        Assert (([VssComm]::ModemStatus($h) -band $MS_CTS) -ne 0) 'RTS did not raise CTS'
        [VssComm]::Escape($h, $SETDTR) | Out-Null
        $status = [VssComm]::ModemStatus($h)
        Assert (($status -band $MS_DSR) -ne 0 -and ($status -band $MS_DCD) -ne 0) "DTR did not raise DSR and DCD: 0x$('{0:x}' -f $status)"

        [VssComm]::Escape($h, $CLRRTS) | Out-Null
        [VssComm]::Escape($h, $CLRDTR) | Out-Null
        $status = [VssComm]::ModemStatus($h)
        Assert (($status -band ($MS_CTS -bor $MS_DSR -bor $MS_DCD)) -eq 0) "lines stayed up: 0x$('{0:x}' -f $status)"
        'RTS to CTS, DTR to DSR and DCD'
    } finally { [VssComm]::Close($h) }
}

Add-Test 'D7' 'a second open fails while the port is open' {
    $first = Open-Port $port
    try {
        $err = 0
        $second = [VssComm]::Open($port, [ref]$err)
        if ($second -ne [IntPtr](-1)) { [VssComm]::Close($second); throw 'a second CreateFile succeeded' }
        Assert ($err -eq $ERROR_ACCESS_DENIED) "second CreateFile failed with error $err, not access denied"
        "error $err"
    } finally { [VssComm]::Close($first) }
    # After the first closes, the port opens again.
    [VssComm]::Close((Open-Port $port))
}

Write-Result -Path $ResultPath -Phase 'test' -Tests $tests
$tests | ForEach-Object { '{0,-6} {1,-4} {2}' -f $_.status, $_.id, $_.detail }
if ($tests | Where-Object { $_.status -ne 'pass' }) { exit 1 }
exit 0
