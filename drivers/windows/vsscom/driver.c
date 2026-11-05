//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// vsscom - a KMDF virtual serial port.
//
// A port is either root-enumerated from vsscom.inf with the hardware ID
// VSSCOM\VSSCOM0, or a child that the control device made (control.c) with the
// hardware ID VSSCOM\PORT. The Ports class installer gives it a COM<n> name,
// and port.c makes that name visible to applications. The one device that
// vsscomctl.inf installs is the control device and not a port; EvtDeviceAdd
// tells them apart. io.c carries out reads, writes and the serial control
// requests; core/vsscom_core.c holds the rules they follow and is unit-tested
// on the host.
//
// Each port has an application end, the COM port, and a hub end that the studio
// opens through a second device interface (hub.c). Until a hub opens, the port
// loops back: bytes written are returned by the next read.
//
// This file is what only the kernel-mode framework has: a name for the device
// object, its device type and characteristics, the control device, and the
// check of where a port came from.
//

#define VSSCOM_DEFINE_GUIDS
#include "vsscom.h"

// The name of the device object, so that the symbolic link has a target.
#define VSSCOM_DEVICE_NAME_PREFIX L"\\Device\\VssCom"

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

// True when the hardware key says this device is the control device.
static BOOLEAN VssComIsControlDevice(_In_ PWDFDEVICE_INIT DeviceInit)
{
    WDFKEY key = NULL;
    ULONG role = 0;
    DECLARE_CONST_UNICODE_STRING(roleValue, VSSCOM_ROLE_VALUE);

    if (!NT_SUCCESS(WdfFdoInitOpenRegistryKey(DeviceInit, PLUGPLAY_REGKEY_DEVICE,
                                              KEY_QUERY_VALUE,
                                              WDF_NO_OBJECT_ATTRIBUTES, &key)))
        return FALSE;
    if (!NT_SUCCESS(WdfRegistryQueryULong(key, &roleValue, &role)))
        role = 0;
    WdfRegistryClose(key);
    return role == VSSCOM_ROLE_CONTROL;
}

// Where a port came from. A port that the control device made has the hardware
// ID VSSCOM\PORT, and its slot is the address the control device gave it. A port
// made by hand has another ID. The hub end opens by a different rule for each
// (access.c). When the ID cannot be read the port counts as made on demand, with
// no slot, so nobody is its owner and the hub end stays shut.
static VOID VssComFindOrigin(_In_ PWDFDEVICE_INIT DeviceInit,
                             _Out_ PBOOLEAN OnDemand, _Out_ PULONG Slot)
{
    WDFMEMORY memory = NULL;
    size_t size = 0;
    ULONG addressLength = 0;
    UNICODE_STRING first;
    DECLARE_CONST_UNICODE_STRING(portId, VSSCOM_PORT_HARDWARE_ID);

    *OnDemand = TRUE;
    *Slot = VssComMaxUlong;

    if (!NT_SUCCESS(WdfFdoInitAllocAndQueryProperty(
            DeviceInit, DevicePropertyHardwareID, NonPagedPoolNx,
            WDF_NO_OBJECT_ATTRIBUTES, &memory)))
        return;

    // The first string of the multi-string list is the device's own ID.
    {
        PCWSTR ids = (PCWSTR)WdfMemoryGetBuffer(memory, &size);
        if (ids != NULL && size >= sizeof(WCHAR))
        {
            RtlInitUnicodeString(&first, ids);
            *OnDemand = RtlEqualUnicodeString(&first, &portId, TRUE);
        }
    }
    WdfObjectDelete(memory);

    if (*OnDemand &&
        !NT_SUCCESS(WdfFdoInitQueryProperty(DeviceInit, DevicePropertyAddress,
                                            sizeof(*Slot), Slot,
                                            &addressLength)))
        *Slot = VssComMaxUlong;
}

static NTSTATUS VssComPortAdd(_Inout_ PWDFDEVICE_INIT DeviceInit)
{
    static volatile LONG instance = 0;
    WCHAR nameBuffer[40];
    UNICODE_STRING deviceName;
    ULONG slot = VssComMaxUlong;
    BOOLEAN onDemand = FALSE;
    NTSTATUS status;

    VssComFindOrigin(DeviceInit, &onDemand, &slot);

    RtlInitEmptyUnicodeString(&deviceName, nameBuffer, sizeof(nameBuffer));
    status = RtlUnicodeStringPrintf(&deviceName,
                                    VSSCOM_DEVICE_NAME_PREFIX L"%ld",
                                    InterlockedIncrement(&instance) - 1);
    if (!NT_SUCCESS(status))
        return status;
    status = WdfDeviceInitAssignName(DeviceInit, &deviceName);
    if (!NT_SUCCESS(status))
        return status;

    WdfDeviceInitSetDeviceType(DeviceInit, FILE_DEVICE_SERIAL_PORT);
    WdfDeviceInitSetCharacteristics(DeviceInit, FILE_DEVICE_SECURE_OPEN, FALSE);

    return VssComCreatePort(DeviceInit, onDemand, slot);
}

NTSTATUS VssComEvtDeviceAdd(_In_ WDFDRIVER Driver,
                            _Inout_ PWDFDEVICE_INIT DeviceInit)
{
    UNREFERENCED_PARAMETER(Driver);

    if (VssComIsControlDevice(DeviceInit))
        return VssComControlAdd(DeviceInit);
    return VssComPortAdd(DeviceInit);
}
