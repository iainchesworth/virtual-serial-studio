//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// vsscom_um - the virtual serial port as a UMDF 2 driver.
//
// The same port as the kernel-mode driver, built from the same sources (port.c,
// io.c, hub.c, core/): a COM name, the application end and the hub end. It runs
// in a driver host process and needs no kernel-mode code signing. What it lacks
// is the control device that makes ports on demand: a user-mode driver cannot
// be a bus, so its ports are devices that were created.
//

// initguid.h after the platform headers and before the rest, so that this file
// defines the GUIDs of ntddser.h and public.h and the linker finds them. The
// other files only declare them.
#include <vsscom_platform.h>
#include <initguid.h>
#include "vsscom.h"

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD VssComEvtDeviceAdd;

NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject,
                     _In_ PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG config;
    WDF_DRIVER_CONFIG_INIT(&config, VssComEvtDeviceAdd);

    return WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES,
                           &config, WDF_NO_HANDLE);
}

NTSTATUS VssComEvtDeviceAdd(_In_ WDFDRIVER Driver,
                            _Inout_ PWDFDEVICE_INIT DeviceInit)
{
    UNREFERENCED_PARAMETER(Driver);

    // A port made by hand: no control device, so no owner to check.
    return VssComCreatePort(DeviceInit, FALSE, VssComMaxUlong);
}
