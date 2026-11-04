//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// vsscom - a KMDF virtual serial port.
//
// The device is root-enumerated from vsscom.inf with the hardware ID
// VSSCOM\VSSCOM0. The Ports class installer gives it a COM<n> name; this
// driver implements a loopback port: bytes written are returned by the next
// read, and the common IOCTL_SERIAL_* requests are answered so ordinary
// terminal software can configure it.
//
// Scope of this first slice:
//   * one loopback port per device instance
//   * reads never pend: an empty read completes with zero bytes, which is
//     what a polling application expects and keeps cancellation out of scope
//   * a paired-port mode and pending reads come with the ring in
//     docs/windows-com-driver.md
//

#include <ntddk.h>
#include <wdf.h>
#include <ntddser.h>

#include <initguid.h>
#include "public.h"

#define VSSCOM_RING_SIZE 4096
#define VSSCOM_TAG 'mssv'

// Modem status bits returned by IOCTL_SERIAL_GET_MODEMSTATUS.
#define VSSCOM_MS_CTS_ON 0x0010
#define VSSCOM_MS_DSR_ON 0x0020
#define VSSCOM_MS_RLSD_ON 0x0080

typedef struct _VSSCOM_DEVICE_CONTEXT
{
    WDFSPINLOCK Lock;
    UCHAR Ring[VSSCOM_RING_SIZE];
    ULONG Head;
    ULONG Tail;
    ULONG BaudRate;
    SERIAL_LINE_CONTROL LineControl;
    SERIAL_HANDFLOW HandFlow;
    SERIAL_TIMEOUTS Timeouts;
    SERIAL_CHARS Chars;
    ULONG WaitMask;
} VSSCOM_DEVICE_CONTEXT, *PVSSCOM_DEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(VSSCOM_DEVICE_CONTEXT, VssComGetContext)

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD VssComEvtDeviceAdd;
EVT_WDF_IO_QUEUE_IO_READ VssComEvtIoRead;
EVT_WDF_IO_QUEUE_IO_WRITE VssComEvtIoWrite;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL VssComEvtIoDeviceControl;

static ULONG VssComAvailable(PVSSCOM_DEVICE_CONTEXT Context)
{
    return (Context->Tail + VSSCOM_RING_SIZE - Context->Head) % VSSCOM_RING_SIZE;
}

static void VssComCompleteOut(WDFREQUEST Request, const void* Data, size_t Length)
{
    PVOID output = NULL;
    NTSTATUS status =
        WdfRequestRetrieveOutputBuffer(Request, Length, &output, NULL);
    if (!NT_SUCCESS(status))
    {
        WdfRequestComplete(Request, status);
        return;
    }

    RtlCopyMemory(output, Data, Length);
    WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, Length);
}

static BOOLEAN VssComFetchIn(WDFREQUEST Request, void* Data, size_t Length)
{
    PVOID input = NULL;
    if (!NT_SUCCESS(WdfRequestRetrieveInputBuffer(Request, Length, &input, NULL)))
        return FALSE;

    RtlCopyMemory(Data, input, Length);
    return TRUE;
}

static void VssComFillStatus(PVSSCOM_DEVICE_CONTEXT Context, PSERIAL_STATUS Status)
{
    RtlZeroMemory(Status, sizeof(*Status));

    WdfSpinLockAcquire(Context->Lock);
    Status->AmountInInQueue = VssComAvailable(Context);
    WdfSpinLockRelease(Context->Lock);
}

static void VssComPush(PVSSCOM_DEVICE_CONTEXT Context, UCHAR Byte)
{
    const ULONG next = (Context->Tail + 1) % VSSCOM_RING_SIZE;
    if (next == Context->Head)
        return; // full: drop, a real UART overruns too

    Context->Ring[Context->Tail] = Byte;
    Context->Tail = next;
}

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

    WdfDeviceInitSetDeviceType(DeviceInit, FILE_DEVICE_SERIAL_PORT);
    WdfDeviceInitSetExclusive(DeviceInit, FALSE);

    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, VSSCOM_DEVICE_CONTEXT);

    WDFDEVICE device;
    NTSTATUS status =
        WdfDeviceCreate(&DeviceInit, &attributes, &device);
    if (!NT_SUCCESS(status))
        return status;

    PVSSCOM_DEVICE_CONTEXT context = VssComGetContext(device);
    context->BaudRate = 115200;
    context->LineControl.StopBits = STOP_BIT_1;
    context->LineControl.Parity = NO_PARITY;
    context->LineControl.WordLength = 8;

    status = WdfSpinLockCreate(WDF_NO_OBJECT_ATTRIBUTES, &context->Lock);
    if (!NT_SUCCESS(status))
        return status;

    status = WdfDeviceCreateDeviceInterface(device, &GUID_DEVINTERFACE_VSSCOM,
                                            NULL);
    if (!NT_SUCCESS(status))
        return status;

    WDF_IO_QUEUE_CONFIG queueConfig;
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig,
                                           WdfIoQueueDispatchSequential);
    queueConfig.EvtIoRead = VssComEvtIoRead;
    queueConfig.EvtIoWrite = VssComEvtIoWrite;
    queueConfig.EvtIoDeviceControl = VssComEvtIoDeviceControl;

    WDFQUEUE queue;
    return WdfIoQueueCreate(device, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES,
                            &queue);
}

VOID VssComEvtIoRead(WDFQUEUE Queue, WDFREQUEST Request, size_t Length)
{
    WDFDEVICE device = WdfIoQueueGetDevice(Queue);
    PVSSCOM_DEVICE_CONTEXT context = VssComGetContext(device);

    PVOID buffer = NULL;
    NTSTATUS status =
        WdfRequestRetrieveOutputBuffer(Request, Length, &buffer, NULL);
    if (!NT_SUCCESS(status))
    {
        WdfRequestComplete(Request, status);
        return;
    }

    ULONG copied = 0;
    WdfSpinLockAcquire(context->Lock);
    while (copied < Length && context->Head != context->Tail)
    {
        ((UCHAR*)buffer)[copied++] = context->Ring[context->Head];
        context->Head = (context->Head + 1) % VSSCOM_RING_SIZE;
    }
    WdfSpinLockRelease(context->Lock);

    WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, copied);
}

VOID VssComEvtIoWrite(WDFQUEUE Queue, WDFREQUEST Request, size_t Length)
{
    WDFDEVICE device = WdfIoQueueGetDevice(Queue);
    PVSSCOM_DEVICE_CONTEXT context = VssComGetContext(device);

    PVOID buffer = NULL;
    NTSTATUS status =
        WdfRequestRetrieveInputBuffer(Request, Length, &buffer, NULL);
    if (!NT_SUCCESS(status))
    {
        WdfRequestComplete(Request, status);
        return;
    }

    ULONG written = 0;
    WdfSpinLockAcquire(context->Lock);
    while (written < Length)
    {
        VssComPush(context, ((const UCHAR*)buffer)[written]);
        ++written;
    }
    WdfSpinLockRelease(context->Lock);

    WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, written);
}

VOID VssComEvtIoDeviceControl(WDFQUEUE Queue, WDFREQUEST Request,
                              size_t OutputBufferLength,
                              size_t InputBufferLength,
                              ULONG IoControlCode)
{
    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    WDFDEVICE device = WdfIoQueueGetDevice(Queue);
    PVSSCOM_DEVICE_CONTEXT context = VssComGetContext(device);

    switch (IoControlCode)
    {
    case IOCTL_VSSCOM_GET_VERSION:
    {
        VSSCOM_VERSION_INFO info = {VSSCOM_VERSION, VSSCOM_RING_SIZE};
        VssComCompleteOut(Request, &info, sizeof(info));
        return;
    }

    case IOCTL_SERIAL_GET_BAUD_RATE:
    {
        SERIAL_BAUD_RATE baud = {0};
        baud.BaudRate = context->BaudRate;
        VssComCompleteOut(Request, &baud, sizeof(baud));
        return;
    }

    case IOCTL_SERIAL_SET_BAUD_RATE:
    {
        SERIAL_BAUD_RATE baud = {0};
        if (!VssComFetchIn(Request, &baud, sizeof(baud)))
        {
            WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
            return;
        }

        WdfSpinLockAcquire(context->Lock);
        context->BaudRate = baud.BaudRate;
        WdfSpinLockRelease(context->Lock);
        WdfRequestComplete(Request, STATUS_SUCCESS);
        return;
    }

    case IOCTL_SERIAL_GET_LINE_CONTROL:
        VssComCompleteOut(Request, &context->LineControl,
                          sizeof(context->LineControl));
        return;

    case IOCTL_SERIAL_SET_LINE_CONTROL:
    {
        SERIAL_LINE_CONTROL control = {0};
        if (!VssComFetchIn(Request, &control, sizeof(control)))
        {
            WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
            return;
        }

        WdfSpinLockAcquire(context->Lock);
        context->LineControl = control;
        WdfSpinLockRelease(context->Lock);
        WdfRequestComplete(Request, STATUS_SUCCESS);
        return;
    }

    case IOCTL_SERIAL_SET_QUEUE_SIZE:
    {
        SERIAL_QUEUE_SIZE size = {0};
        if (!VssComFetchIn(Request, &size, sizeof(size)))
        {
            WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
            return;
        }
        WdfRequestComplete(Request, STATUS_SUCCESS);
        return;
    }

    case IOCTL_SERIAL_SET_DTR:
    case IOCTL_SERIAL_CLR_DTR:
    case IOCTL_SERIAL_SET_RTS:
    case IOCTL_SERIAL_CLR_RTS:
    case IOCTL_SERIAL_SET_XOFF:
    case IOCTL_SERIAL_SET_XON:
        WdfRequestComplete(Request, STATUS_SUCCESS);
        return;

    case IOCTL_SERIAL_GET_MODEMSTATUS:
    {
        ULONG modem = VSSCOM_MS_CTS_ON | VSSCOM_MS_DSR_ON | VSSCOM_MS_RLSD_ON;
        VssComCompleteOut(Request, &modem, sizeof(modem));
        return;
    }

    case IOCTL_SERIAL_GET_COMMSTATUS:
    {
        SERIAL_STATUS status = {0};
        VssComFillStatus(context, &status);
        VssComCompleteOut(Request, &status, sizeof(status));
        return;
    }

    case IOCTL_SERIAL_PURGE:
    {
        ULONG flags = 0;
        if (!VssComFetchIn(Request, &flags, sizeof(flags)))
        {
            WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
            return;
        }

        if ((flags & (SERIAL_PURGE_RXCLEAR | SERIAL_PURGE_TXCLEAR
                      | SERIAL_PURGE_RXABORT | SERIAL_PURGE_TXABORT)) != 0)
        {
            WdfSpinLockAcquire(context->Lock);
            context->Head = context->Tail = 0;
            WdfSpinLockRelease(context->Lock);
        }
        WdfRequestComplete(Request, STATUS_SUCCESS);
        return;
    }

    case IOCTL_SERIAL_GET_TIMEOUTS:
        VssComCompleteOut(Request, &context->Timeouts,
                          sizeof(context->Timeouts));
        return;

    case IOCTL_SERIAL_SET_TIMEOUTS:
    {
        SERIAL_TIMEOUTS timeouts = {0};
        if (!VssComFetchIn(Request, &timeouts, sizeof(timeouts)))
        {
            WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
            return;
        }

        WdfSpinLockAcquire(context->Lock);
        context->Timeouts = timeouts;
        WdfSpinLockRelease(context->Lock);
        WdfRequestComplete(Request, STATUS_SUCCESS);
        return;
    }

    case IOCTL_SERIAL_GET_HANDFLOW:
        VssComCompleteOut(Request, &context->HandFlow,
                          sizeof(context->HandFlow));
        return;

    case IOCTL_SERIAL_SET_HANDFLOW:
    {
        SERIAL_HANDFLOW handflow = {0};
        if (!VssComFetchIn(Request, &handflow, sizeof(handflow)))
        {
            WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
            return;
        }

        WdfSpinLockAcquire(context->Lock);
        context->HandFlow = handflow;
        WdfSpinLockRelease(context->Lock);
        WdfRequestComplete(Request, STATUS_SUCCESS);
        return;
    }

    case IOCTL_SERIAL_GET_CHARS:
        VssComCompleteOut(Request, &context->Chars, sizeof(context->Chars));
        return;

    case IOCTL_SERIAL_SET_CHARS:
    {
        SERIAL_CHARS chars = {0};
        if (!VssComFetchIn(Request, &chars, sizeof(chars)))
        {
            WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
            return;
        }

        WdfSpinLockAcquire(context->Lock);
        context->Chars = chars;
        WdfSpinLockRelease(context->Lock);
        WdfRequestComplete(Request, STATUS_SUCCESS);
        return;
    }

    case IOCTL_SERIAL_SET_WAIT_MASK:
    {
        ULONG mask = 0;
        if (!VssComFetchIn(Request, &mask, sizeof(mask)))
        {
            WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
            return;
        }

        context->WaitMask = mask;
        WdfRequestComplete(Request, STATUS_SUCCESS);
        return;
    }

    case IOCTL_SERIAL_GET_WAIT_MASK:
    {
        ULONG mask = context->WaitMask;
        VssComCompleteOut(Request, &mask, sizeof(mask));
        return;
    }

    case IOCTL_SERIAL_WAIT_ON_MASK:
    {
        // Nothing ever changes the modem lines in loopback mode; report no
        // event instead of leaving the caller blocked forever.
        ULONG mask = 0;
        VssComCompleteOut(Request, &mask, sizeof(mask));
        return;
    }

    case IOCTL_SERIAL_IMMEDIATE_CHAR:
    {
        UCHAR byte = 0;
        if (!VssComFetchIn(Request, &byte, sizeof(byte)))
        {
            WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
            return;
        }

        WdfSpinLockAcquire(context->Lock);
        VssComPush(context, byte);
        WdfSpinLockRelease(context->Lock);
        WdfRequestComplete(Request, STATUS_SUCCESS);
        return;
    }

    default:
        WdfRequestComplete(Request, STATUS_INVALID_DEVICE_REQUEST);
        return;
    }
}
