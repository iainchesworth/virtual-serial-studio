//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the helper service shares with the studio and with the user-mode driver.
//
// The kernel-mode driver has a control device that makes ports on demand. A
// user-mode driver cannot be a bus, so it has no such device. Its ports are
// software devices, and Windows lets only an administrator or SYSTEM make one.
// The helper is a service that runs as SYSTEM and makes them for ordinary
// users, with the same rules as the control device:
//
//   - a user holds at most VSSCOM_HELPER_MAX_PORTS_PER_USER ports, and all
//     users together at most VSSCOM_HELPER_MAX_PORTS;
//   - the hub end of a port opens only for the user who asked for the port
//     (the driver reads the owner from the device property below);
//   - a port goes when the pipe that asked for it closes, whether the caller
//     closed it or died;
//   - a request to make a port is answered when the port's driver has started,
//     so its COM name and hub interface exist by then.
//
// The caller opens the named pipe, then sends one request and reads one reply
// per exchange. A request and a reply are one message each. Open the pipe with
// SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION: the helper needs to know the
// user and nothing more.
//
// The constants are enumerators and constant objects and not macros, because the
// preprocessor policy counts the directives of a header. Include <windows.h> and
// <devpropdef.h> before this file.
//

#pragma once

// The pipe. Authenticated users may open it; remote machines may not.
static const WCHAR VssComHelperPipeName[] = L"\\\\.\\pipe\\vsscom_helper";

// The name of the service.
static const WCHAR VssComHelperServiceName[] = L"vsscom_helper";

// The enumerator of the software devices, the hardware ID of a port, and the
// format of its instance id: the slot, as %04lX, so the port that holds a slot
// keeps its COM name from one run to the next. The instance id of the whole
// device is SWD\VSSCOMUM\<slot>.
static const WCHAR VssComUmEnumerator[] = L"VSSCOMUM";
static const WCHAR VssComUmPortHardwareId[] = L"VSSCOMUM\\PORT";
static const WCHAR VssComUmPortInstanceFormat[] = L"%04lX";
static const WCHAR VssComUmPortInstancePrefix[] = L"SWD\\VSSCOMUM\\";

// The device property that holds the owner of a port: the user's SID in its binary
// form. The helper sets it when it makes the device and the driver reads it; a
// device without it is a port made by hand.
// {5e0b8a77-1c3d-4f29-b6a4-8d2f7c91e305}, property 2.
static const DEVPROPKEY VssComUmOwnerKey = {
    {0x5e0b8a77, 0x1c3d, 0x4f29, {0xb6, 0xa4, 0x8d, 0x2f, 0x7c, 0x91, 0xe3, 0x05}},
    2};

enum
{
    // 1 is the first version. A request with another version is refused.
    VSSCOM_HELPER_VERSION = 1,
    // The ports that can exist at once, for all callers together, and the most
    // one user can hold. The same numbers as the control device.
    VSSCOM_HELPER_MAX_PORTS = 32,
    VSSCOM_HELPER_MAX_PORTS_PER_USER = 16,
    // A SID is at most this many bytes (SECURITY_MAX_SID_SIZE).
    VSSCOM_HELPER_MAX_SID_BYTES = 68
};

enum
{
    // Make a port. The reply carries its slot.
    VSSCOM_HELPER_OP_CREATE = 1,
    // Remove a port that this pipe made.
    VSSCOM_HELPER_OP_DESTROY = 2
};

typedef struct _VSSCOM_HELPER_REQUEST
{
    ULONG Version;  // VSSCOM_HELPER_VERSION
    ULONG Op;       // VSSCOM_HELPER_OP_*
    ULONG Slot;     // for DESTROY; zero for CREATE
    ULONG Reserved; // must be zero
} VSSCOM_HELPER_REQUEST, *PVSSCOM_HELPER_REQUEST;

// Status is a Win32 error code:
//   ERROR_SUCCESS             done;
//   ERROR_NOT_ENOUGH_QUOTA    this user holds the most ports they may;
//   ERROR_NO_SYSTEM_RESOURCES every slot is taken, or the system refused;
//   ERROR_NOT_FOUND           DESTROY named a slot this pipe does not hold;
//   ERROR_DEV_NOT_EXIST       the device was made and no driver started on it:
//                             the user-mode driver is not installed;
//   ERROR_TIMEOUT             the driver did not start in 30 seconds;
//   ERROR_NOT_SUPPORTED       the request has a version or a code that the helper
//                             does not know;
//   ERROR_ACCESS_DENIED       the helper could not tell who the caller is.
typedef struct _VSSCOM_HELPER_REPLY
{
    ULONG Version; // VSSCOM_HELPER_VERSION
    ULONG Status;
    ULONG Slot;
    ULONG Reserved;
} VSSCOM_HELPER_REPLY, *PVSSCOM_HELPER_REPLY;
