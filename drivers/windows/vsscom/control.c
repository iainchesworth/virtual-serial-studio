//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The control device: the bus that makes ports on demand.
//
// It is a root-enumerated device (vsscomctl.inf) that the driver recognises by
// the VssRole value in its hardware key. It exposes the interface
// GUID_DEVINTERFACE_VSSCOMCTL. A handle on that interface can ask for a port.
// The driver answers by reporting a child device, VSSCOM\PORT\<slot>, to the
// Plug and Play manager.
// The manager installs vsscom.inf on it, which loads this same driver as the
// function driver of the child, and that is a port like any other (driver.c).
//
// A port lives as long as the handle that made it: when the last reference to
// that file object goes, the child is reported missing and the manager removes
// it. A studio that crashes therefore leaves no ports behind.
//
// A slot is the child's instance number. The lowest free one is used, so a
// port that comes back gets the device node it had, and with it its COM name.
//

#include "vsscom.h"

typedef struct _VSSCOM_PORT_DESCRIPTION
{
    WDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER Header;
    ULONG Slot;
    // Which use of the slot this describes, so that a late cleanup of an old
    // description cannot free a slot that has been given to somebody else.
    ULONG Token;
} VSSCOM_PORT_DESCRIPTION, *PVSSCOM_PORT_DESCRIPTION;

typedef struct _VSSCOM_CONTROL_CONTEXT
{
    WDFDEVICE Device;
    WDFSPINLOCK Lock;
    // Everything below is guarded by Lock. A slot is in use from the request
    // that made it until the framework drops the child's description, which is
    // after the port device is gone.
    BOOLEAN InUse[VSSCOM_CTL_MAX_PORTS];
    // The port has been reported missing and is on its way out.
    BOOLEAN Dying[VSSCOM_CTL_MAX_PORTS];
    // The handle that made the port; NULL once it is dying.
    WDFFILEOBJECT Owner[VSSCOM_CTL_MAX_PORTS];
    // The user who made it, for as long as the slot is in use: the hub end
    // opens for that user only (access.c).
    VSSCOM_OPENER Maker[VSSCOM_CTL_MAX_PORTS];
    ULONG Token[VSSCOM_CTL_MAX_PORTS];
} VSSCOM_CONTROL_CONTEXT, *PVSSCOM_CONTROL_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(VSSCOM_CONTROL_CONTEXT,
                                   VssComGetControlContext)

// What a handle on the control device remembers: who opened it.
typedef struct _VSSCOM_CTL_FILE_CONTEXT
{
    VSSCOM_OPENER Opener;
} VSSCOM_CTL_FILE_CONTEXT, *PVSSCOM_CTL_FILE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(VSSCOM_CTL_FILE_CONTEXT,
                                   VssComCtlFileContext)

// The one control device, for the ports to ask who made them. Set while it is
// started. Its ports are removed before it is, so a port that reads this has a
// control device behind it.
static volatile PVSSCOM_CONTROL_CONTEXT g_Control = NULL;

static EVT_WDF_CHILD_LIST_CREATE_DEVICE VssComCtlCreateChild;
static EVT_WDF_CHILD_LIST_IDENTIFICATION_DESCRIPTION_COMPARE VssComCtlCompare;
static EVT_WDF_CHILD_LIST_IDENTIFICATION_DESCRIPTION_CLEANUP VssComCtlCleanup;
static EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL VssComCtlIoControl;
static EVT_WDF_DEVICE_FILE_CREATE VssComCtlFileCreate;
static EVT_WDF_FILE_CLEANUP VssComCtlFileCleanup;

// -- The children -----------------------------------------------------------

static BOOLEAN VssComCtlCompare(
    _In_ WDFCHILDLIST ChildList,
    _In_ PWDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER First,
    _In_ PWDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER Second)
{
    UNREFERENCED_PARAMETER(ChildList);
    return CONTAINING_RECORD(First, VSSCOM_PORT_DESCRIPTION, Header)->Slot ==
           CONTAINING_RECORD(Second, VSSCOM_PORT_DESCRIPTION, Header)->Slot;
}

// Frees a slot, but only for the use that the description belongs to.
static VOID VssComCtlFreeSlot(_In_ PVSSCOM_CONTROL_CONTEXT Context,
                              _In_ ULONG Slot, _In_ ULONG Token)
{
    if (Slot >= VSSCOM_CTL_MAX_PORTS)
        return;

    WdfSpinLockAcquire(Context->Lock);
    if (Context->InUse[Slot] && Context->Token[Slot] == Token)
    {
        Context->InUse[Slot] = FALSE;
        Context->Dying[Slot] = FALSE;
        Context->Owner[Slot] = NULL;
        RtlZeroMemory(&Context->Maker[Slot], sizeof(Context->Maker[Slot]));
    }
    WdfSpinLockRelease(Context->Lock);
}

BOOLEAN VssComControlOwnerOf(_In_ ULONG Slot, _Out_ PVSSCOM_OPENER Owner)
{
    PVSSCOM_CONTROL_CONTEXT context = g_Control;
    BOOLEAN found = FALSE;

    RtlZeroMemory(Owner, sizeof(*Owner));
    if (context == NULL || Slot >= VSSCOM_CTL_MAX_PORTS)
        return FALSE;

    WdfSpinLockAcquire(context->Lock);
    if (context->InUse[Slot])
    {
        *Owner = context->Maker[Slot];
        found = TRUE;
    }
    WdfSpinLockRelease(context->Lock);
    return found;
}

static VOID VssComCtlCleanup(
    _In_ WDFCHILDLIST ChildList,
    _In_ PWDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER Description)
{
    PVSSCOM_PORT_DESCRIPTION description =
        CONTAINING_RECORD(Description, VSSCOM_PORT_DESCRIPTION, Header);
    VssComCtlFreeSlot(VssComGetControlContext(WdfChildListGetDevice(ChildList)),
                      description->Slot, description->Token);
}

// Makes the device object of one port: the physical device object that the
// manager puts vsscom.inf on.
static NTSTATUS VssComCtlCreateChild(
    _In_ WDFCHILDLIST ChildList,
    _In_ PWDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER Description,
    _Inout_ PWDFDEVICE_INIT ChildInit)
{
    PVSSCOM_PORT_DESCRIPTION description =
        CONTAINING_RECORD(Description, VSSCOM_PORT_DESCRIPTION, Header);
    WCHAR instanceBuffer[16];
    UNICODE_STRING instance;
    WDF_DEVICE_PNP_CAPABILITIES capabilities;
    WDFDEVICE child = NULL;
    DECLARE_CONST_UNICODE_STRING(hardwareId, VSSCOM_PORT_HARDWARE_ID);
    NTSTATUS status;
    UNREFERENCED_PARAMETER(ChildList);

    status = WdfPdoInitAssignDeviceID(ChildInit, &hardwareId);
    if (!NT_SUCCESS(status))
        return status;
    status = WdfPdoInitAddHardwareID(ChildInit, &hardwareId);
    if (!NT_SUCCESS(status))
        return status;

    RtlInitEmptyUnicodeString(&instance, instanceBuffer,
                              sizeof(instanceBuffer));
    status = RtlUnicodeStringPrintf(&instance, VSSCOM_PORT_INSTANCE_FORMAT,
                                    description->Slot);
    if (!NT_SUCCESS(status))
        return status;
    status = WdfPdoInitAssignInstanceID(ChildInit, &instance);
    if (!NT_SUCCESS(status))
        return status;

    status = WdfDeviceCreate(&ChildInit, WDF_NO_OBJECT_ATTRIBUTES, &child);
    if (!NT_SUCCESS(status))
        return status;

    // A port goes away without warning when its creator closes the handle, so
    // it is removable and surprise removal is expected.
    WDF_DEVICE_PNP_CAPABILITIES_INIT(&capabilities);
    capabilities.Removable = WdfTrue;
    capabilities.SurpriseRemovalOK = WdfTrue;
    capabilities.UniqueID = WdfTrue;
    capabilities.Address = description->Slot;
    capabilities.UINumber = description->Slot;
    WdfDeviceSetPnpCapabilities(child, &capabilities);
    return STATUS_SUCCESS;
}

// Reports a port missing. The manager then removes the device.
static NTSTATUS VssComCtlReportMissing(_In_ PVSSCOM_CONTROL_CONTEXT Context,
                                       _In_ ULONG Slot, _In_ ULONG Token)
{
    VSSCOM_PORT_DESCRIPTION description;

    WDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER_INIT(&description.Header,
                                                     sizeof(description));
    description.Slot = Slot;
    description.Token = Token;
    return WdfChildListUpdateChildDescriptionAsMissing(
        WdfFdoGetDefaultChildList(Context->Device), &description.Header);
}

// -- The handle -------------------------------------------------------------

// Only the bare interface path opens the control device.
static VOID VssComCtlFileCreate(_In_ WDFDEVICE Device, _In_ WDFREQUEST Request,
                                _In_ WDFFILEOBJECT FileObject)
{
    PUNICODE_STRING name = WdfFileObjectGetFileName(FileObject);
    NTSTATUS status;
    UNREFERENCED_PARAMETER(Device);

    if (name != NULL && name->Length != 0 &&
        !(name->Length == sizeof(WCHAR) && name->Buffer[0] == L'\\'))
    {
        WdfRequestComplete(Request, STATUS_OBJECT_NAME_NOT_FOUND);
        return;
    }

    // Remember who opened it: the ports it makes belong to that user.
    status = VssComCaptureOpener(Request,
                                 &VssComCtlFileContext(FileObject)->Opener);
    WdfRequestComplete(Request, status);
}

// The handle is closed, by its owner or by the system when the process ends:
// the ports it made go with it.
static VOID VssComCtlFileCleanup(_In_ WDFFILEOBJECT FileObject)
{
    PVSSCOM_CONTROL_CONTEXT context =
        VssComGetControlContext(WdfFileObjectGetDevice(FileObject));
    ULONG slot;

    for (slot = 0; slot < VSSCOM_CTL_MAX_PORTS; ++slot)
    {
        BOOLEAN mine;
        ULONG token = 0;

        WdfSpinLockAcquire(context->Lock);
        mine = context->InUse[slot] && !context->Dying[slot] &&
               context->Owner[slot] == FileObject;
        if (mine)
        {
            context->Dying[slot] = TRUE;
            context->Owner[slot] = NULL;
            token = context->Token[slot];
        }
        WdfSpinLockRelease(context->Lock);

        if (mine)
            (void)VssComCtlReportMissing(context, slot, token);
    }
}

// -- The requests -----------------------------------------------------------

static VOID VssComCtlCreatePort(_In_ PVSSCOM_CONTROL_CONTEXT Context,
                                _In_ WDFFILEOBJECT File,
                                _In_ WDFREQUEST Request)
{
    VSSCOM_CTL_CREATE input;
    VSSCOM_CTL_PORT output;
    VSSCOM_PORT_DESCRIPTION description;
    ULONG slot;
    ULONG token = 0;
    BOOLEAN found = FALSE;
    BOOLEAN over_quota = FALSE;
    const VSSCOM_OPENER* maker;
    NTSTATUS status;

    if (!VssComFetchInput(Request, &input, sizeof(input)))
    {
        WdfRequestComplete(Request, STATUS_BUFFER_TOO_SMALL);
        return;
    }
    if (input.Flags != 0 || input.Reserved != 0)
    {
        WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
        return;
    }

    maker = &VssComCtlFileContext(File)->Opener;

    WdfSpinLockAcquire(Context->Lock);
    // One user cannot take every slot: a user holds at most
    // VSSCOM_CTL_MAX_PORTS_PER_USER ports that are not on their way out.
    if (maker->Valid)
    {
        ULONG held = 0;
        for (slot = 0; slot < VSSCOM_CTL_MAX_PORTS; ++slot)
        {
            if (Context->InUse[slot] && !Context->Dying[slot] &&
                Context->Maker[slot].Valid &&
                RtlEqualSid((PSID)Context->Maker[slot].Sid, (PSID)maker->Sid))
                ++held;
        }
        over_quota = held >= VSSCOM_CTL_MAX_PORTS_PER_USER;
    }
    for (slot = 0; !over_quota && slot < VSSCOM_CTL_MAX_PORTS; ++slot)
    {
        if (!Context->InUse[slot])
        {
            Context->InUse[slot] = TRUE;
            Context->Dying[slot] = FALSE;
            Context->Owner[slot] = File;
            Context->Maker[slot] = *maker;
            token = ++Context->Token[slot];
            found = TRUE;
            break;
        }
    }
    WdfSpinLockRelease(Context->Lock);
    if (over_quota)
    {
        WdfRequestComplete(Request, STATUS_QUOTA_EXCEEDED);
        return;
    }
    if (!found)
    {
        WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
        return;
    }

    WDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER_INIT(&description.Header,
                                                     sizeof(description));
    description.Slot = slot;
    description.Token = token;
    status = WdfChildListAddOrUpdateChildDescriptionAsPresent(
        WdfFdoGetDefaultChildList(Context->Device), &description.Header, NULL);
    if (!NT_SUCCESS(status))
    {
        VssComCtlFreeSlot(Context, slot, token);
        WdfRequestComplete(Request, status);
        return;
    }

    output.Slot = slot;
    VssComCompleteWithData(Request, &output, sizeof(output));
}

static VOID VssComCtlDestroyPort(_In_ PVSSCOM_CONTROL_CONTEXT Context,
                                 _In_ WDFFILEOBJECT File,
                                 _In_ WDFREQUEST Request)
{
    VSSCOM_CTL_PORT input;
    ULONG token = 0;
    NTSTATUS status = STATUS_SUCCESS;

    if (!VssComFetchInput(Request, &input, sizeof(input)))
    {
        WdfRequestComplete(Request, STATUS_BUFFER_TOO_SMALL);
        return;
    }
    if (input.Slot >= VSSCOM_CTL_MAX_PORTS)
    {
        WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
        return;
    }

    WdfSpinLockAcquire(Context->Lock);
    if (!Context->InUse[input.Slot] || Context->Dying[input.Slot])
    {
        status = STATUS_NOT_FOUND;
    }
    else if (Context->Owner[input.Slot] != File)
    {
        status = STATUS_ACCESS_DENIED;
    }
    else
    {
        Context->Dying[input.Slot] = TRUE;
        Context->Owner[input.Slot] = NULL;
        token = Context->Token[input.Slot];
    }
    WdfSpinLockRelease(Context->Lock);

    if (NT_SUCCESS(status))
        status = VssComCtlReportMissing(Context, input.Slot, token);
    WdfRequestComplete(Request, status);
}

static VOID VssComCtlListPorts(_In_ PVSSCOM_CONTROL_CONTEXT Context,
                               _In_ WDFFILEOBJECT File, _In_ WDFREQUEST Request)
{
    VSSCOM_CTL_LIST list;
    ULONG slot;

    RtlZeroMemory(&list, sizeof(list));
    WdfSpinLockAcquire(Context->Lock);
    for (slot = 0; slot < VSSCOM_CTL_MAX_PORTS; ++slot)
    {
        if (Context->InUse[slot] && !Context->Dying[slot])
        {
            list.Ports[list.Count].Slot = slot;
            list.Ports[list.Count].Mine = Context->Owner[slot] == File;
            ++list.Count;
        }
    }
    WdfSpinLockRelease(Context->Lock);
    VssComCompleteWithData(Request, &list, sizeof(list));
}

// Who made a port. The helper service asks, to check that the user who wants the COM
// number of a port changed is the user who made it. Administrators and SYSTEM only:
// the control device is open to every signed-in user, and the owner of a port is
// not for them to read.
static VOID VssComCtlGetOwner(_In_ PVSSCOM_CONTROL_CONTEXT Context,
                              _In_ WDFFILEOBJECT File, _In_ WDFREQUEST Request)
{
    VSSCOM_CTL_PORT input;
    VSSCOM_CTL_OWNER output;
    VSSCOM_OPENER maker;
    const VSSCOM_OPENER* caller = &VssComCtlFileContext(File)->Opener;
    BOOLEAN found;

    if (!VssComFetchInput(Request, &input, sizeof(input)))
    {
        WdfRequestComplete(Request, STATUS_BUFFER_TOO_SMALL);
        return;
    }
    if (input.Slot >= VSSCOM_CTL_MAX_PORTS)
    {
        WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
        return;
    }
    if (!caller->Admin && !caller->Kernel)
    {
        WdfRequestComplete(Request, STATUS_ACCESS_DENIED);
        return;
    }

    RtlZeroMemory(&maker, sizeof(maker));
    WdfSpinLockAcquire(Context->Lock);
    found = Context->InUse[input.Slot] && !Context->Dying[input.Slot];
    if (found)
        maker = Context->Maker[input.Slot];
    WdfSpinLockRelease(Context->Lock);
    if (!found)
    {
        WdfRequestComplete(Request, STATUS_NOT_FOUND);
        return;
    }

    RtlZeroMemory(&output, sizeof(output));
    if (maker.Valid)
    {
        output.Valid = 1;
        output.SidLength = RtlLengthSid((PSID)maker.Sid);
        RtlCopyMemory(output.Sid, maker.Sid, output.SidLength);
    }
    VssComCompleteWithData(Request, &output, sizeof(output));
}

static VOID VssComCtlIoControl(_In_ WDFQUEUE Queue, _In_ WDFREQUEST Request,
                               _In_ size_t OutputBufferLength,
                               _In_ size_t InputBufferLength,
                               _In_ ULONG IoControlCode)
{
    PVSSCOM_CONTROL_CONTEXT context =
        VssComGetControlContext(WdfIoQueueGetDevice(Queue));
    WDFFILEOBJECT file = WdfRequestGetFileObject(Request);
    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    switch (IoControlCode)
    {
    case IOCTL_VSSCOM_CTL_GET_VERSION:
    {
        VSSCOM_CTL_VERSION_INFO info;
        info.Version = VSSCOM_CTL_VERSION;
        info.MaxPorts = VSSCOM_CTL_MAX_PORTS;
        VssComCompleteWithData(Request, &info, sizeof(info));
        return;
    }
    case IOCTL_VSSCOM_CTL_CREATE_PORT:
        VssComCtlCreatePort(context, file, Request);
        return;
    case IOCTL_VSSCOM_CTL_DESTROY_PORT:
        VssComCtlDestroyPort(context, file, Request);
        return;
    case IOCTL_VSSCOM_CTL_LIST_PORTS:
        VssComCtlListPorts(context, file, Request);
        return;
    case IOCTL_VSSCOM_CTL_GET_OWNER:
        VssComCtlGetOwner(context, file, Request);
        return;
    default:
        WdfRequestComplete(Request, STATUS_INVALID_DEVICE_REQUEST);
        return;
    }
}

// -- The device -------------------------------------------------------------

// The control device is going: ports stop asking it who made them.
static VOID VssComCtlDeviceCleanup(_In_ WDFOBJECT Object)
{
    PVSSCOM_CONTROL_CONTEXT context =
        VssComGetControlContext((WDFDEVICE)Object);

    if (g_Control == context)
        g_Control = NULL;
}

NTSTATUS VssComControlAdd(_Inout_ PWDFDEVICE_INIT DeviceInit)
{
    WDF_CHILD_LIST_CONFIG childConfig;
    WDF_FILEOBJECT_CONFIG fileConfig;
    WDF_OBJECT_ATTRIBUTES fileAttributes;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES lockAttributes;
    WDF_IO_QUEUE_CONFIG queueConfig;
    WDFDEVICE device;
    PVSSCOM_CONTROL_CONTEXT context;
    NTSTATUS status;

    WdfDeviceInitSetCharacteristics(DeviceInit, FILE_DEVICE_SECURE_OPEN, FALSE);
    WdfDeviceInitSetIoType(DeviceInit, WdfDeviceIoBuffered);

    WDF_CHILD_LIST_CONFIG_INIT(&childConfig, sizeof(VSSCOM_PORT_DESCRIPTION),
                               VssComCtlCreateChild);
    childConfig.EvtChildListIdentificationDescriptionCompare = VssComCtlCompare;
    childConfig.EvtChildListIdentificationDescriptionCleanup = VssComCtlCleanup;
    WdfFdoInitSetDefaultChildListConfig(DeviceInit, &childConfig,
                                        WDF_NO_OBJECT_ATTRIBUTES);

    WDF_FILEOBJECT_CONFIG_INIT(&fileConfig, VssComCtlFileCreate,
                               WDF_NO_EVENT_CALLBACK, VssComCtlFileCleanup);
    fileConfig.FileObjectClass = WdfFileObjectWdfCanUseFsContext2;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&fileAttributes,
                                            VSSCOM_CTL_FILE_CONTEXT);
    WdfDeviceInitSetFileObjectConfig(DeviceInit, &fileConfig, &fileAttributes);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes,
                                            VSSCOM_CONTROL_CONTEXT);
    attributes.EvtCleanupCallback = VssComCtlDeviceCleanup;
    status = WdfDeviceCreate(&DeviceInit, &attributes, &device);
    if (!NT_SUCCESS(status))
        return status;

    context = VssComGetControlContext(device);
    context->Device = device;

    WDF_OBJECT_ATTRIBUTES_INIT(&lockAttributes);
    lockAttributes.ParentObject = device;
    status = WdfSpinLockCreate(&lockAttributes, &context->Lock);
    if (!NT_SUCCESS(status))
        return status;
    g_Control = context;

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig,
                                           WdfIoQueueDispatchParallel);
    queueConfig.EvtIoDeviceControl = VssComCtlIoControl;
    status = WdfIoQueueCreate(device, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES,
                              WDF_NO_HANDLE);
    if (!NT_SUCCESS(status))
        return status;

    return WdfDeviceCreateDeviceInterface(device, &GUID_DEVINTERFACE_VSSCOMCTL,
                                          NULL);
}
