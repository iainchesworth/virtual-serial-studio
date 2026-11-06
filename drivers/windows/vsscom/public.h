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

// Takes the port back from the program that has it open: the program's requests
// fail from then on, its waiting requests end, and a new program can open the
// port. An enumerator and not a macro, because the preprocessor policy counts the
// directives of this header.
enum
{
    IOCTL_VSSCOM_HUB_REVOKE =
        CTL_CODE(FILE_DEVICE_VSSCOM, 0x804, METHOD_BUFFERED, FILE_ANY_ACCESS)
};

// 2 added the hub end. 3 added AppProcessId to the hub state, the revoke request
// and the effect of the DTR and RTS settings of SET_HANDFLOW.
#define VSSCOM_VERSION 3

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
    ULONG AppProcessId; // the process that has the port open; 0 when none (version 3)
} VSSCOM_HUB_STATE, *PVSSCOM_HUB_STATE;

// ---------------------------------------------------------------------------
// The control device
//
// A second device, installed once from vsscomctl.inf, makes ports on demand.
// Open its interface GUID_DEVINTERFACE_VSSCOMCTL (no reference string), ask for
// a port with IOCTL_VSSCOM_CTL_CREATE_PORT, and wait for the port to appear:
// its device instance id is
//
//     VSSCOM\PORT\<slot as four hexadecimal digits>
//
// and it carries the same two interfaces as any other port. A port lives as
// long as the handle that made it. Closing that handle, or the process ending,
// removes it. The slot is the lowest free number, so a port keeps its slot,
// and with it its COM name, from one run to the next.
// ---------------------------------------------------------------------------

// {b3d7e0a4-2f81-4c59-8e16-7a5d9c3f0b62}
DEFINE_GUID(GUID_DEVINTERFACE_VSSCOMCTL, 0xb3d7e0a4, 0x2f81, 0x4c59, 0x8e, 0x16,
            0x7a, 0x5d, 0x9c, 0x3f, 0x0b, 0x62);

// The hardware ID of a port the control device made, and the format of the
// instance id that follows it (a ULONG slot, as for wsprintf or printf).
#define VSSCOM_PORT_HARDWARE_ID L"VSSCOM\\PORT"
#define VSSCOM_PORT_INSTANCE_FORMAT L"%04lX"

#define VSSCOM_CTL_VERSION 1
// The ports that can exist at once, for all callers together.
#define VSSCOM_CTL_MAX_PORTS 32
// The ports one user can hold at once, so that one user cannot take every slot.
// CREATE_PORT fails with STATUS_QUOTA_EXCEEDED beyond it. The hub end of a port
// opens only for the user who made the port. An enumerator and not a macro,
// because the preprocessor policy counts the directives of this header.
enum
{
    VSSCOM_CTL_MAX_PORTS_PER_USER = 16
};

#define IOCTL_VSSCOM_CTL_GET_VERSION                                        \
    CTL_CODE(FILE_DEVICE_VSSCOM, 0x810, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_VSSCOM_CTL_CREATE_PORT                                        \
    CTL_CODE(FILE_DEVICE_VSSCOM, 0x811, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_VSSCOM_CTL_DESTROY_PORT                                       \
    CTL_CODE(FILE_DEVICE_VSSCOM, 0x812, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_VSSCOM_CTL_LIST_PORTS                                         \
    CTL_CODE(FILE_DEVICE_VSSCOM, 0x813, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _VSSCOM_CTL_VERSION_INFO
{
    ULONG Version;  // VSSCOM_CTL_VERSION
    ULONG MaxPorts; // VSSCOM_CTL_MAX_PORTS
} VSSCOM_CTL_VERSION_INFO, *PVSSCOM_CTL_VERSION_INFO;

// Input of CREATE_PORT. Both fields must be zero; they are for options that a later
// version may add. A wanted COM number is not one of them: the number belongs to the
// Ports class installer and the COM name database, which only a user-mode program
// with administrator rights can change, so the helper service does it
// (vsscom_helper/protocol.h, VSSCOM_HELPER_OP_SET_COM_NUMBER).
typedef struct _VSSCOM_CTL_CREATE
{
    ULONG Flags;
    ULONG Reserved;
} VSSCOM_CTL_CREATE, *PVSSCOM_CTL_CREATE;

// Output of CREATE_PORT and input of DESTROY_PORT. CREATE_PORT fails with
// STATUS_INSUFFICIENT_RESOURCES when every slot is taken, which includes slots
// whose port is still being removed.
typedef struct _VSSCOM_CTL_PORT
{
    ULONG Slot;
} VSSCOM_CTL_PORT, *PVSSCOM_CTL_PORT;

// Output of LIST_PORTS: the slots in use. Mine is 1 for a port this handle made,
// and only that handle can destroy it.
typedef struct _VSSCOM_CTL_LIST_ENTRY
{
    ULONG Slot;
    ULONG Mine;
} VSSCOM_CTL_LIST_ENTRY, *PVSSCOM_CTL_LIST_ENTRY;

typedef struct _VSSCOM_CTL_LIST
{
    ULONG Count;
    VSSCOM_CTL_LIST_ENTRY Ports[VSSCOM_CTL_MAX_PORTS];
} VSSCOM_CTL_LIST, *PVSSCOM_CTL_LIST;

// Who made a port, for the helper service. Only SYSTEM and administrators may ask: the
// helper compares the user with the one who wants to change the COM number of the port.
// Input: VSSCOM_CTL_PORT. Output: VSSCOM_CTL_OWNER. Fails with STATUS_NOT_FOUND for a
// slot with no port, and STATUS_ACCESS_DENIED for any other caller. Added in version 3
// of the ports; the control version stays 1, and an older driver answers
// STATUS_INVALID_DEVICE_REQUEST.
enum
{
    IOCTL_VSSCOM_CTL_GET_OWNER =
        CTL_CODE(FILE_DEVICE_VSSCOM, 0x814, METHOD_BUFFERED, FILE_ANY_ACCESS)
};
enum
{
    VSSCOM_CTL_MAX_SID_BYTES = 68 // SECURITY_MAX_SID_SIZE
};

typedef struct _VSSCOM_CTL_OWNER
{
    ULONG Valid;     // 1 when Sid holds the user
    ULONG SidLength; // bytes of Sid in use
    UCHAR Sid[VSSCOM_CTL_MAX_SID_BYTES];
} VSSCOM_CTL_OWNER, *PVSSCOM_CTL_OWNER;

#endif // VSSCOM_PUBLIC_H
