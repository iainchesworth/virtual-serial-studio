//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the driver shares with the studio: the interface the hub opens and the
// requests it sends. Included by the driver and by user-mode code.
//
// Each port has two ends. The application end is the COM port, \\.\COMn. The
// hub end is a second handle on the same device, opened through the device
// interface GUID_DEVINTERFACE_VSSCOM with the reference string "hub":
//
//     <interface path>\hub
//
// Bytes the application writes are read from the hub handle with ReadFile;
// bytes written to the hub handle are what the application reads. Open the hub
// handle with FILE_FLAG_OVERLAPPED. Each end has one handle at a time. While
// no hub handle is open the port loops back, as a port with a loopback plug
// does.
//

#ifndef VSSCOM_PUBLIC_H
#define VSSCOM_PUBLIC_H

// {7c9a1f62-3d54-4a6b-9d3e-5f0c2b8a4d11}
DEFINE_GUID(GUID_DEVINTERFACE_VSSCOM, 0x7c9a1f62, 0x3d54, 0x4a6b, 0x9d, 0x3e,
            0x5f, 0x0c, 0x2b, 0x8a, 0x4d, 0x11);

// The reference string of the hub end, and the file name the driver sees.
#define VSSCOM_HUB_REFERENCE L"hub"
#define VSSCOM_HUB_FILE_NAME L"\\hub"

#define FILE_DEVICE_VSSCOM 0x8000

// Either end.
#define IOCTL_VSSCOM_GET_VERSION                                            \
    CTL_CODE(FILE_DEVICE_VSSCOM, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)

// Hub end only.
#define IOCTL_VSSCOM_HUB_GET_STATE                                          \
    CTL_CODE(FILE_DEVICE_VSSCOM, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_VSSCOM_HUB_WAIT_STATE                                         \
    CTL_CODE(FILE_DEVICE_VSSCOM, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_VSSCOM_HUB_SET_LINES                                          \
    CTL_CODE(FILE_DEVICE_VSSCOM, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)

// 2 added the hub end.
#define VSSCOM_VERSION 2

typedef struct _VSSCOM_VERSION_INFO
{
    ULONG Version;
    // The bytes each direction holds.
    ULONG BufferSize;
} VSSCOM_VERSION_INFO, *PVSSCOM_VERSION_INFO;

// Input line levels, the same bits as SERIAL_MSR_* in ntddser.h. The hub sets
// the lines a real device would drive towards the application.
#define VSSCOM_LINE_CTS 0x10u
#define VSSCOM_LINE_DSR 0x20u
#define VSSCOM_LINE_RI 0x40u
#define VSSCOM_LINE_DCD 0x80u
#define VSSCOM_LINE_MASK                                                    \
    (VSSCOM_LINE_CTS | VSSCOM_LINE_DSR | VSSCOM_LINE_RI | VSSCOM_LINE_DCD)

// What the application has set, for the hub to mirror. Every change of any
// field but TxQueued adds one to Generation.
typedef struct _VSSCOM_HUB_STATE
{
    ULONG Generation;
    ULONG AppOpen;  // 1 while an application has the port open
    ULONG BaudRate;
    ULONG DataBits; // 5 to 8
    ULONG Parity;   // 0 none, 1 odd, 2 even, 3 mark, 4 space
    ULONG StopBits; // 0 one, 1 one and a half, 2 two
    ULONG Dtr;      // 1 when the application holds DTR high
    ULONG Rts;
    ULONG Break;    // 1 while the application holds a break
    ULONG Lines;    // the input lines the hub last set
    ULONG TxQueued; // bytes the application wrote that the hub has not read
} VSSCOM_HUB_STATE, *PVSSCOM_HUB_STATE;

#endif // VSSCOM_PUBLIC_H
