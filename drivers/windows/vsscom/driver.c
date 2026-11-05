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
// and this file makes that name visible to applications. The one device that
// vsscomctl.inf installs is the control device and not a port; EvtDeviceAdd
// tells them apart. io.c carries out reads, writes
// and the serial control requests; core/vsscom_core.c holds the rules they
// follow and is unit-tested on the host.
//
// Each port has an application end, the COM port, and a hub end that the studio
// opens through a second device interface (hub.c). Until a hub opens, the port
// loops back: bytes written are returned by the next read.
//

#define VSSCOM_DEFINE_GUIDS
#include "vsscom.h"

// The names that make the port visible to applications.
#define VSSCOM_DEVICE_NAME_PREFIX L"\\Device\\VssCom"
#define VSSCOM_LINK_PREFIX L"\\DosDevices\\Global\\"
#define VSSCOM_SERIALCOMM_KEY L"SERIALCOMM"
#define VSSCOM_PORTNAME_VALUE L"PortName"

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD VssComEvtDeviceAdd;
EVT_WDF_OBJECT_CONTEXT_CLEANUP VssComEvtDeviceCleanup;
EVT_WDF_DEVICE_FILE_CREATE VssComEvtDeviceFileCreate;
EVT_WDF_FILE_CLEANUP VssComEvtFileCleanup;

NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject,
                     _In_ PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG config;
    WDF_DRIVER_CONFIG_INIT(&config, VssComEvtDeviceAdd);

    return WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES,
                           &config, WDF_NO_HANDLE);
}

// Gives applications the COM name the Ports class installer chose.
//
// The installer writes PortName under the device's hardware key. A serial
// driver then has three jobs of its own, the same three the Microsoft
// VirtualSerial2 sample does: expose GUID_DEVINTERFACE_COMPORT, make the
// \DosDevices\Global\COMn symbolic link that CreateFile("\\.\COMn") resolves,
// and list the port under HARDWARE\DEVICEMAP\SERIALCOMM, which is what
// SerialPort.GetPortNames and most port pickers read.
NTSTATUS VssComRegisterPort(_In_ PVSSCOM_DEVICE_CONTEXT Context)
{
    WDFDEVICE device = Context->Device;
    WDFKEY key = NULL;
    WCHAR portNameBuffer[16];
    WCHAR linkBuffer[48];
    UNICODE_STRING portName;
    UNICODE_STRING link;
    UNICODE_STRING pdoName;
    WDFMEMORY memory = NULL;
    WDF_OBJECT_ATTRIBUTES attributes;
    DECLARE_CONST_UNICODE_STRING(portNameValue, VSSCOM_PORTNAME_VALUE);
    DECLARE_CONST_UNICODE_STRING(serialCommKey, VSSCOM_SERIALCOMM_KEY);

    NTSTATUS status = WdfDeviceCreateDeviceInterface(
        device, &GUID_DEVINTERFACE_COMPORT, NULL);
    if (!NT_SUCCESS(status))
        return status;

    status = WdfDeviceOpenRegistryKey(device, PLUGPLAY_REGKEY_DEVICE,
                                      KEY_QUERY_VALUE, WDF_NO_OBJECT_ATTRIBUTES,
                                      &key);
    if (!NT_SUCCESS(status))
        return status;

    RtlInitEmptyUnicodeString(&portName, portNameBuffer, sizeof(portNameBuffer));
    status = WdfRegistryQueryUnicodeString(key, &portNameValue, NULL, &portName);
    WdfRegistryClose(key);
    key = NULL;
    if (!NT_SUCCESS(status))
        return status;

    RtlInitEmptyUnicodeString(&link, linkBuffer, sizeof(linkBuffer));
    status = RtlAppendUnicodeToString(&link, VSSCOM_LINK_PREFIX);
    if (NT_SUCCESS(status))
        status = RtlAppendUnicodeStringToString(&link, &portName);
    if (!NT_SUCCESS(status))
        return status;

    status = WdfDeviceCreateSymbolicLink(device, &link);
    if (!NT_SUCCESS(status))
        return status;

    // The legacy list is keyed by the device object's name.
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = device;
    status = WdfDeviceAllocAndQueryProperty(
        device, DevicePropertyPhysicalDeviceObjectName, NonPagedPoolNx,
        &attributes, &memory);
    if (!NT_SUCCESS(status))
        return status;
    Context->PdoName = (PWSTR)WdfMemoryGetBuffer(memory, NULL);
    RtlInitUnicodeString(&pdoName, Context->PdoName);

    status = WdfDeviceOpenDevicemapKey(device, &serialCommKey, KEY_SET_VALUE,
                                       WDF_NO_OBJECT_ATTRIBUTES, &key);
    if (!NT_SUCCESS(status))
        return status;
    status = WdfRegistryAssignUnicodeString(key, &pdoName, &portName);
    WdfRegistryClose(key);
    if (NT_SUCCESS(status))
        Context->CreatedSerialCommValue = TRUE;
    return status;
}

// Takes the port off the legacy list again. The symbolic link and the device
// interface go with the device object.
VOID VssComEvtDeviceCleanup(_In_ WDFOBJECT Object)
{
    WDFDEVICE device = (WDFDEVICE)Object;
    PVSSCOM_DEVICE_CONTEXT context = VssComGetContext(device);
    WDFKEY key = NULL;
    UNICODE_STRING pdoName;
    DECLARE_CONST_UNICODE_STRING(serialCommKey, VSSCOM_SERIALCOMM_KEY);

    if (!context->CreatedSerialCommValue || context->PdoName == NULL)
        return;

    if (NT_SUCCESS(WdfDeviceOpenDevicemapKey(device, &serialCommKey,
                                             KEY_SET_VALUE,
                                             WDF_NO_OBJECT_ATTRIBUTES, &key)))
    {
        RtlInitUnicodeString(&pdoName, context->PdoName);
        (void)WdfRegistryRemoveValue(key, &pdoName);
        WdfRegistryClose(key);
    }
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
    *Slot = MAXULONG;

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
        *Slot = MAXULONG;
}

static NTSTATUS VssComPortAdd(_Inout_ PWDFDEVICE_INIT DeviceInit)
{
    static volatile LONG instance = 0;
    WCHAR nameBuffer[40];
    UNICODE_STRING deviceName;
    WDF_FILEOBJECT_CONFIG fileConfig;
    WDF_OBJECT_ATTRIBUTES fileAttributes;
    WDF_OBJECT_ATTRIBUTES attributes;
    DECLARE_CONST_UNICODE_STRING(hubReference, VSSCOM_HUB_REFERENCE);
    WDFDEVICE device;
    PVSSCOM_DEVICE_CONTEXT context;
    ULONG slot = MAXULONG;
    BOOLEAN onDemand = FALSE;
    NTSTATUS status;

    VssComFindOrigin(DeviceInit, &onDemand, &slot);

    // A name for the device object, so that the symbolic link has a target.
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
    // Buffered I/O: a waiting read can be filled later from another thread.
    WdfDeviceInitSetIoType(DeviceInit, WdfDeviceIoBuffered);
    // Each end has one handle at a time. The device is not marked exclusive,
    // because that would shut the hub out while an application has the port;
    // VssComEvtDeviceFileCreate refuses a second handle on an end itself.
    // The file name tells the ends apart, so the framework must keep it.
    WDF_FILEOBJECT_CONFIG_INIT(&fileConfig, VssComEvtDeviceFileCreate,
                               WDF_NO_EVENT_CALLBACK, VssComEvtFileCleanup);
    fileConfig.FileObjectClass = WdfFileObjectWdfCanUseFsContext2;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&fileAttributes, VSSCOM_FILE_CONTEXT);
    WdfDeviceInitSetFileObjectConfig(DeviceInit, &fileConfig, &fileAttributes);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, VSSCOM_DEVICE_CONTEXT);
    attributes.EvtCleanupCallback = VssComEvtDeviceCleanup;

    status = WdfDeviceCreate(&DeviceInit, &attributes, &device);
    if (!NT_SUCCESS(status))
        return status;

    context = VssComGetContext(device);
    context->Device = device;
    context->OnDemand = onDemand;
    context->Slot = slot;
    context->BaudRate = 115200;
    context->LineControl.StopBits = STOP_BIT_1;
    context->LineControl.Parity = NO_PARITY;
    context->LineControl.WordLength = 8;
    vsscom_ring_init(&context->Rx, context->RxStorage, VSSCOM_RX_SIZE);
    vsscom_ring_init(&context->Tx, context->TxStorage, VSSCOM_TX_SIZE);
    vsscom_events_init(&context->Events);

    // Child of the device, so that it goes when the device does. Ports now come
    // and go while the driver stays loaded.
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = device;
    status = WdfSpinLockCreate(&attributes, &context->Lock);
    if (!NT_SUCCESS(status))
        return status;

    // The hub end: <interface path>\hub.
    status = WdfDeviceCreateDeviceInterface(device, &GUID_DEVINTERFACE_VSSCOM,
                                            &hubReference);
    if (!NT_SUCCESS(status))
        return status;

    status = VssComRegisterPort(context);
    if (!NT_SUCCESS(status))
        return status;

    return VssComCreateQueues(context);
}

NTSTATUS VssComEvtDeviceAdd(_In_ WDFDRIVER Driver,
                            _Inout_ PWDFDEVICE_INIT DeviceInit)
{
    UNREFERENCED_PARAMETER(Driver);

    if (VssComIsControlDevice(DeviceInit))
        return VssComControlAdd(DeviceInit);
    return VssComPortAdd(DeviceInit);
}
