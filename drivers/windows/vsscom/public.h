//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSSCOM_PUBLIC_H
#define VSSCOM_PUBLIC_H

#ifdef __cplusplus
extern "C" {
#endif

// {7c9a1f62-3d54-4a6b-9d3e-5f0c2b8a4d11}
DEFINE_GUID(GUID_DEVINTERFACE_VSSCOM, 0x7c9a1f62, 0x3d54, 0x4a6b, 0x9d, 0x3e,
            0x5f, 0x0c, 0x2b, 0x8a, 0x4d, 0x11);

#define FILE_DEVICE_VSSCOM 0x8000

#define IOCTL_VSSCOM_GET_VERSION                                            \
    CTL_CODE(FILE_DEVICE_VSSCOM, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define VSSCOM_VERSION 1

typedef struct _VSSCOM_VERSION_INFO
{
    ULONG Version;
    ULONG BufferSize;
} VSSCOM_VERSION_INFO, *PVSSCOM_VERSION_INFO;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // VSSCOM_PUBLIC_H
