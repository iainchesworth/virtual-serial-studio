//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// vsscom: the hub end of a port.
//
// A port has two ends and one device object. The application end is opened as
// \\.\COMn. The hub end is opened through the device interface with the
// reference string "hub", so the driver sees the file name \hub. Each end has
// one handle at a time; a second open fails with access denied.
//
//   application write -> Tx ring -> hub ReadFile
//   hub WriteFile     -> Rx ring -> application read (io.c)
//
// A write that does not fit waits until the other end makes room, as it does
// on a real port with a full buffer. While no hub handle is open the port
// loops back (io.c), so a port that nobody has attached still behaves.
//
// The hub also reads what the application set (baud rate, line control, DTR,
// RTS, break) and sets the input lines the application sees (CTS, DSR, DCD,
// RI). The state request waits until something changes, so the hub never polls.
//
// Locks and completion: as in io.c, Lock guards the context and is never held
// while a request is completed or another queue is called.
//

#include "vsscom.h"

// -- Opening and closing ---------------------------------------------------

VOID VssComEvtDeviceFileCreate(_In_ WDFDEVICE Device, _In_ WDFREQUEST Request,
                               _In_ WDFFILEOBJECT FileObject)
{
    PVSSCOM_DEVICE_CONTEXT context = VssComGetContext(Device);
    PVSSCOM_FILE_CONTEXT file = VssComFileContext(FileObject);
    PUNICODE_STRING name = WdfFileObjectGetFileName(FileObject);
    DECLARE_CONST_UNICODE_STRING(hubName, VSSCOM_HUB_FILE_NAME);
    ULONG kind;
    NTSTATUS status = STATUS_SUCCESS;

    if (name == NULL || name->Length == 0 ||
        (name->Length == sizeof(WCHAR) && name->Buffer[0] == L'\\'))
    {
        kind = VSSCOM_FILE_APP;
    }
    else if (RtlEqualUnicodeString(name, &hubName, TRUE))
    {
        kind = VSSCOM_FILE_HUB;
    }
    else
    {
        WdfRequestComplete(Request, STATUS_OBJECT_NAME_NOT_FOUND);
        return;
    }

    WdfSpinLockAcquire(context->Lock);
    if (kind == VSSCOM_FILE_APP)
    {
        if (context->AppOpen)
            status = STATUS_ACCESS_DENIED;
        else
            context->AppOpen = TRUE;
    }
    else
    {
        if (context->HubOpen)
        {
            status = STATUS_ACCESS_DENIED;
        }
        else
        {
            // Whatever the loopback held is not for the hub, and it starts with
            // every input line low until it says otherwise.
            context->HubOpen = TRUE;
            context->HubLines = 0;
            vsscom_ring_clear(&context->Rx);
            vsscom_ring_clear(&context->Tx);
        }
    }
    WdfSpinLockRelease(context->Lock);

    if (!NT_SUCCESS(status))
    {
        WdfRequestComplete(Request, status);
        return;
    }

    file->Kind = kind;
    file->Granted = TRUE;

    if (kind == VSSCOM_FILE_HUB)
        VssComApplyLines(context);
    VssComBumpState(context);
    WdfRequestComplete(Request, STATUS_SUCCESS);
}

// A port goes back to quiet when its handle closes. The framework has already
// cancelled the requests that handle left waiting.
VOID VssComEvtFileCleanup(_In_ WDFFILEOBJECT FileObject)
{
    PVSSCOM_DEVICE_CONTEXT context =
        VssComGetContext(WdfFileObjectGetDevice(FileObject));
    PVSSCOM_FILE_CONTEXT file = VssComFileContext(FileObject);

    if (!file->Granted)
        return;

    if (file->Kind == VSSCOM_FILE_APP)
    {
        VssComResetPort(context);
        WdfSpinLockAcquire(context->Lock);
        context->AppOpen = FALSE;
        context->Break = FALSE;
        WdfSpinLockRelease(context->Lock);
    }
    else
    {
        WdfSpinLockAcquire(context->Lock);
        context->HubOpen = FALSE;
        context->HubLines = 0;
        vsscom_ring_clear(&context->Rx);
        vsscom_ring_clear(&context->Tx);
        WdfSpinLockRelease(context->Lock);
        VssComApplyLines(context);
    }

    VssComBumpState(context);
    // Reset and the end of hub mode both change who may go on: waiting writes
    // finish or find room.
    VssComPump(context);
}

// -- The state the hub watches ---------------------------------------------

static VOID VssComFillState(_In_ PVSSCOM_DEVICE_CONTEXT Context,
                            _Out_ PVSSCOM_HUB_STATE State)
{
    RtlZeroMemory(State, sizeof(*State));
    WdfSpinLockAcquire(Context->Lock);
    State->Generation = Context->Generation;
    State->AppOpen = Context->AppOpen ? 1 : 0;
    State->BaudRate = Context->BaudRate;
    State->DataBits = Context->LineControl.WordLength;
    State->Parity = Context->LineControl.Parity;
    State->StopBits = Context->LineControl.StopBits;
    State->Dtr = Context->Dtr ? 1 : 0;
    State->Rts = Context->Rts ? 1 : 0;
    State->Break = Context->Break ? 1 : 0;
    State->Lines = Context->HubLines;
    State->TxQueued = vsscom_ring_count(&Context->Tx);
    WdfSpinLockRelease(Context->Lock);
}

static VOID VssComCompleteStateWaiters(_In_ PVSSCOM_DEVICE_CONTEXT Context)
{
    WDFREQUEST request;

    while (NT_SUCCESS(WdfIoQueueRetrieveNextRequest(Context->StateQueue, &request)))
    {
        VSSCOM_HUB_STATE state;
        VssComFillState(Context, &state);
        VssComCompleteWithData(request, &state, sizeof(state));
    }
}

VOID VssComBumpState(_In_ PVSSCOM_DEVICE_CONTEXT Context)
{
    WdfSpinLockAcquire(Context->Lock);
    Context->Generation++;
    WdfSpinLockRelease(Context->Lock);
    VssComCompleteStateWaiters(Context);
}

// -- Requests of the hub end -----------------------------------------------

static VOID VssComFinishPipe(_In_ WDFREQUEST Request, _In_ NTSTATUS Status)
{
    PVSSCOM_PIPE_CONTEXT pipe = VssComPipeContext(Request);
    WdfRequestCompleteWithInformation(Request, Status, pipe->Done);
}

// Gives a request a pipe context and parks it in `Queue`.
static NTSTATUS VssComParkPipe(_In_ WDFREQUEST Request, _In_ WDFQUEUE Queue,
                               _In_ PVOID Buffer, _In_ ULONG Length)
{
    PVSSCOM_PIPE_CONTEXT pipe = NULL;
    WDF_OBJECT_ATTRIBUTES attributes;
    NTSTATUS status;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, VSSCOM_PIPE_CONTEXT);
    status = WdfObjectAllocateContext(Request, &attributes, (PVOID*)&pipe);
    if (!NT_SUCCESS(status))
        return status;
    pipe->Buffer = (PUCHAR)Buffer;
    pipe->Length = Length;
    pipe->Done = 0;
    return WdfRequestForwardToIoQueue(Request, Queue);
}

VOID VssComHubRead(_In_ PVSSCOM_DEVICE_CONTEXT Context, _In_ WDFREQUEST Request,
                   _In_ size_t Length)
{
    PVOID buffer = NULL;
    NTSTATUS status;

    if (Length == 0)
    {
        WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, 0);
        return;
    }
    if (Length > MAXULONG)
    {
        WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
        return;
    }
    status = WdfRequestRetrieveOutputBuffer(Request, Length, &buffer, NULL);
    if (NT_SUCCESS(status))
        status = VssComParkPipe(Request, Context->HubReadQueue, buffer,
                                (ULONG)Length);
    if (!NT_SUCCESS(status))
    {
        WdfRequestComplete(Request, status);
        return;
    }
    VssComPump(Context);
}

VOID VssComHubWrite(_In_ PVSSCOM_DEVICE_CONTEXT Context, _In_ WDFREQUEST Request,
                    _In_ size_t Length)
{
    PVOID buffer = NULL;
    NTSTATUS status;

    if (Length == 0)
    {
        WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, 0);
        return;
    }
    if (Length > MAXULONG)
    {
        WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
        return;
    }
    status = WdfRequestRetrieveInputBuffer(Request, Length, &buffer, NULL);
    if (NT_SUCCESS(status))
        status = VssComParkPipe(Request, Context->HubWriteQueue, buffer,
                                (ULONG)Length);
    if (!NT_SUCCESS(status))
    {
        WdfRequestComplete(Request, status);
        return;
    }
    VssComPump(Context);
}

VOID VssComAppWriteToHub(_In_ PVSSCOM_DEVICE_CONTEXT Context,
                         _In_ WDFREQUEST Request, _In_ size_t Length)
{
    PVOID buffer = NULL;
    NTSTATUS status = WdfRequestRetrieveInputBuffer(Request, Length, &buffer, NULL);

    if (NT_SUCCESS(status))
    {
        WdfSpinLockAcquire(Context->Lock);
        Context->AppWritesPending++;
        WdfSpinLockRelease(Context->Lock);

        status = VssComParkPipe(Request, Context->AppWriteQueue, buffer,
                                (ULONG)Length);
        if (!NT_SUCCESS(status))
        {
            WdfSpinLockAcquire(Context->Lock);
            Context->AppWritesPending--;
            WdfSpinLockRelease(Context->Lock);
        }
    }
    if (!NT_SUCCESS(status))
    {
        WdfRequestComplete(Request, status);
        return;
    }
    VssComPump(Context);
}

VOID VssComHubIoControl(_In_ PVSSCOM_DEVICE_CONTEXT Context,
                        _In_ WDFREQUEST Request, _In_ ULONG IoControlCode)
{
    switch (IoControlCode)
    {
    case IOCTL_VSSCOM_HUB_GET_STATE:
    {
        VSSCOM_HUB_STATE state;
        VssComFillState(Context, &state);
        VssComCompleteWithData(Request, &state, sizeof(state));
        return;
    }

    case IOCTL_VSSCOM_HUB_WAIT_STATE:
    {
        ULONG known = 0;
        ULONG now;
        NTSTATUS status;

        if (!VssComFetchInput(Request, &known, sizeof(known)))
        {
            WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
            return;
        }

        WdfSpinLockAcquire(Context->Lock);
        now = Context->Generation;
        WdfSpinLockRelease(Context->Lock);
        if (now != known)
        {
            VSSCOM_HUB_STATE state;
            VssComFillState(Context, &state);
            VssComCompleteWithData(Request, &state, sizeof(state));
            return;
        }

        status = WdfRequestForwardToIoQueue(Request, Context->StateQueue);
        if (!NT_SUCCESS(status))
        {
            WdfRequestComplete(Request, status);
            return;
        }

        // A change between the check and the parking found no request to
        // answer, so look again.
        WdfSpinLockAcquire(Context->Lock);
        now = Context->Generation;
        WdfSpinLockRelease(Context->Lock);
        if (now != known)
            VssComCompleteStateWaiters(Context);
        return;
    }

    case IOCTL_VSSCOM_HUB_SET_LINES:
    {
        ULONG lines = 0;
        if (!VssComFetchInput(Request, &lines, sizeof(lines)))
        {
            WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
            return;
        }
        WdfSpinLockAcquire(Context->Lock);
        Context->HubLines = lines & VSSCOM_LINE_MASK;
        WdfSpinLockRelease(Context->Lock);
        VssComApplyLines(Context);
        VssComBumpState(Context);
        WdfRequestComplete(Request, STATUS_SUCCESS);
        return;
    }

    default:
        WdfRequestComplete(Request, STATUS_INVALID_DEVICE_REQUEST);
        return;
    }
}

// -- Moving bytes ----------------------------------------------------------

// Hub writes go into Rx while it has room. The request at the head keeps its
// place, so writes stay in order.
BOOLEAN VssComServiceHubWrites(_In_ PVSSCOM_DEVICE_CONTEXT Context)
{
    BOOLEAN moved = FALSE;

    for (;;)
    {
        WDFREQUEST request;
        PVSSCOM_PIPE_CONTEXT pipe;
        ULONG stored;

        if (!NT_SUCCESS(WdfIoQueueRetrieveNextRequest(Context->HubWriteQueue,
                                                      &request)))
            return moved;

        pipe = VssComPipeContext(request);
        WdfSpinLockAcquire(Context->Lock);
        stored = vsscom_ring_write(&Context->Rx, pipe->Buffer + pipe->Done,
                                   pipe->Length - pipe->Done);
        pipe->Done += stored;
        WdfSpinLockRelease(Context->Lock);

        if (stored != 0)
        {
            moved = TRUE;
            VssComRaise(Context, VSSCOM_EV_RXCHAR);
        }
        if (pipe->Done == pipe->Length)
        {
            VssComFinishPipe(request, STATUS_SUCCESS);
            continue;
        }
        WdfRequestRequeue(request);
        return moved;
    }
}

// Hub reads take what the application wrote, as soon as there is any.
BOOLEAN VssComServiceHubReads(_In_ PVSSCOM_DEVICE_CONTEXT Context)
{
    BOOLEAN moved = FALSE;

    for (;;)
    {
        WDFREQUEST request;
        PVSSCOM_PIPE_CONTEXT pipe;
        ULONG got;
        BOOLEAN empty;
        ULONG pending;

        if (!NT_SUCCESS(WdfIoQueueRetrieveNextRequest(Context->HubReadQueue,
                                                      &request)))
            return moved;

        pipe = VssComPipeContext(request);
        WdfSpinLockAcquire(Context->Lock);
        got = vsscom_ring_read(&Context->Tx, pipe->Buffer, pipe->Length);
        empty = vsscom_ring_count(&Context->Tx) == 0;
        pending = Context->AppWritesPending;
        WdfSpinLockRelease(Context->Lock);

        if (got == 0)
        {
            WdfRequestRequeue(request);
            return moved;
        }

        moved = TRUE;
        pipe->Done = got;
        VssComFinishPipe(request, STATUS_SUCCESS);

        // The application's output queue ran dry with nothing more waiting.
        if (empty && pending == 0)
            VssComRaise(Context, VSSCOM_EV_TXEMPTY);
    }
}

// The application's writes go into Tx while it has room. If the hub end has
// gone, what is left of a write is dropped and the write completes short.
BOOLEAN VssComServiceAppWrites(_In_ PVSSCOM_DEVICE_CONTEXT Context)
{
    BOOLEAN moved = FALSE;

    for (;;)
    {
        WDFREQUEST request;
        PVSSCOM_PIPE_CONTEXT pipe;
        ULONG stored = 0;
        BOOLEAN hub;
        BOOLEAN finished;

        if (!NT_SUCCESS(WdfIoQueueRetrieveNextRequest(Context->AppWriteQueue,
                                                      &request)))
            return moved;

        pipe = VssComPipeContext(request);
        WdfSpinLockAcquire(Context->Lock);
        hub = Context->HubOpen;
        if (hub)
        {
            stored = vsscom_ring_write(&Context->Tx, pipe->Buffer + pipe->Done,
                                       pipe->Length - pipe->Done);
            pipe->Done += stored;
        }
        finished = !hub || pipe->Done == pipe->Length;
        if (finished)
            Context->AppWritesPending--;
        WdfSpinLockRelease(Context->Lock);

        if (stored != 0)
            moved = TRUE;
        if (finished)
        {
            VssComFinishPipe(request, STATUS_SUCCESS);
            continue;
        }
        WdfRequestRequeue(request);
        return moved;
    }
}

VOID VssComAbortAppWrites(_In_ PVSSCOM_DEVICE_CONTEXT Context,
                          _In_ NTSTATUS Status)
{
    WDFREQUEST request;

    while (NT_SUCCESS(WdfIoQueueRetrieveNextRequest(Context->AppWriteQueue,
                                                    &request)))
    {
        WdfSpinLockAcquire(Context->Lock);
        Context->AppWritesPending--;
        WdfSpinLockRelease(Context->Lock);
        VssComFinishPipe(request, Status);
    }
}

VOID VssComEvtAppWriteCanceled(_In_ WDFQUEUE Queue, _In_ WDFREQUEST Request)
{
    PVSSCOM_DEVICE_CONTEXT context = VssComGetContext(WdfIoQueueGetDevice(Queue));

    WdfSpinLockAcquire(context->Lock);
    context->AppWritesPending--;
    WdfSpinLockRelease(context->Lock);
    VssComFinishPipe(Request, STATUS_CANCELLED);
}

// -- Queues ----------------------------------------------------------------

NTSTATUS VssComCreateHubQueues(_In_ PVSSCOM_DEVICE_CONTEXT Context)
{
    WDFDEVICE device = Context->Device;
    WDF_IO_QUEUE_CONFIG config;
    NTSTATUS status;

    // Without a cancel callback the framework completes a cancelled request
    // itself, which is all these three need.
    WDF_IO_QUEUE_CONFIG_INIT(&config, WdfIoQueueDispatchManual);
    status = WdfIoQueueCreate(device, &config, WDF_NO_OBJECT_ATTRIBUTES,
                              &Context->HubReadQueue);
    if (!NT_SUCCESS(status))
        return status;

    WDF_IO_QUEUE_CONFIG_INIT(&config, WdfIoQueueDispatchManual);
    status = WdfIoQueueCreate(device, &config, WDF_NO_OBJECT_ATTRIBUTES,
                              &Context->HubWriteQueue);
    if (!NT_SUCCESS(status))
        return status;

    WDF_IO_QUEUE_CONFIG_INIT(&config, WdfIoQueueDispatchManual);
    status = WdfIoQueueCreate(device, &config, WDF_NO_OBJECT_ATTRIBUTES,
                              &Context->StateQueue);
    if (!NT_SUCCESS(status))
        return status;

    // The application's waiting writes must give back the count of bytes they
    // had taken, and the pending count.
    WDF_IO_QUEUE_CONFIG_INIT(&config, WdfIoQueueDispatchManual);
    config.EvtIoCanceledOnQueue = VssComEvtAppWriteCanceled;
    return WdfIoQueueCreate(device, &config, WDF_NO_OBJECT_ATTRIBUTES,
                            &Context->AppWriteQueue);
}
