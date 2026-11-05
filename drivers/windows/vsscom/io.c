//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// vsscom: reads, writes and the serial control requests.
//
// The rules (what a timeout combination means, what an event history keeps)
// are in core/vsscom_core.c, which the unit tests cover on the host. This file
// carries them out against Windows requests.
//
// Without a hub handle the port is a loopback: bytes written come back from the
// next read, and a write that does not fit in the receive ring completes with
// the bytes that did. With a hub handle open (hub.c) the application's writes go
// to the hub and its reads come from the hub, with flow control both ways, and
// the input lines are the ones the hub sets.
//
// Locks and completion: Lock guards the context and is never held while a
// request is completed or while another queue is called.
//

#include "vsscom.h"

// -- Time ------------------------------------------------------------------

#define VSSCOM_100NS_PER_MS 10000ULL
// A timer fires on or after its due time; this much early still counts as due.
#define VSSCOM_TIMER_SLACK (1ULL * VSSCOM_100NS_PER_MS)

static ULONGLONG VssComNow(VOID)
{
    return VssComInterruptTime();
}

static ULONGLONG VssComDeadline(ULONG Milliseconds)
{
    return VssComNow() + (ULONGLONG)Milliseconds * VSSCOM_100NS_PER_MS;
}

static VOID VssComStartTimer(WDFTIMER Timer, ULONGLONG Deadline)
{
    const ULONGLONG now = VssComNow();
    const ULONGLONG left = Deadline > now ? Deadline - now : 1;
    WdfTimerStart(Timer, -(LONGLONG)left);
}

// -- Small request helpers -------------------------------------------------

VOID VssComCompleteWithData(_In_ WDFREQUEST Request,
                            _In_reads_bytes_(Length) const VOID* Data,
                            _In_ size_t Length)
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

BOOLEAN VssComFetchInput(_In_ WDFREQUEST Request,
                         _Out_writes_bytes_(Length) PVOID Data,
                         _In_ size_t Length)
{
    PVOID input = NULL;
    if (!NT_SUCCESS(WdfRequestRetrieveInputBuffer(Request, Length, &input, NULL)))
        return FALSE;
    RtlCopyMemory(Data, input, Length);
    return TRUE;
}

// -- Waiting reads ---------------------------------------------------------

static VOID VssComStopReadTimers(_In_ PVSSCOM_DEVICE_CONTEXT Context)
{
    WdfTimerStop(Context->ReadTotalTimer, FALSE);
    WdfTimerStop(Context->ReadIntervalTimer, FALSE);
}

// Starts or stops the two timers to match the read at the head of the queue.
static VOID VssComArmReadTimers(_In_ PVSSCOM_DEVICE_CONTEXT Context,
                                _In_ PVSSCOM_READ_CONTEXT Read)
{
    ULONGLONG total;
    ULONGLONG interval;

    WdfSpinLockAcquire(Context->Lock);
    total = Read->TotalDeadline;
    interval = Read->IntervalDeadline;
    WdfSpinLockRelease(Context->Lock);

    if (total != 0)
        VssComStartTimer(Context->ReadTotalTimer, total);
    else
        WdfTimerStop(Context->ReadTotalTimer, FALSE);

    if (interval != 0)
        VssComStartTimer(Context->ReadIntervalTimer, interval);
    else
        WdfTimerStop(Context->ReadIntervalTimer, FALSE);
}

static VOID VssComFinishRead(_In_ WDFREQUEST Request, _In_ NTSTATUS Status)
{
    PVSSCOM_READ_CONTEXT read = VssComReadContext(Request);
    WdfRequestCompleteWithInformation(Request, Status, read->Received);
}

// Serves the read at the head of the queue from the receive ring. A read that
// is complete leaves, and the next one gets its turn; one that still waits goes
// back to the head with its timers set.
static VOID VssComServiceOnce(_In_ PVSSCOM_DEVICE_CONTEXT Context,
                              _Inout_ BOOLEAN* Moved)
{
    for (;;)
    {
        WDFREQUEST request;
        PVSSCOM_READ_CONTEXT read;
        BOOLEAN done;
        BOOLEAN abort;
        BOOLEAN timed_out;
        ULONGLONG now;

        if (!NT_SUCCESS(WdfIoQueueRetrieveNextRequest(Context->ReadQueue,
                                                      &request)))
        {
            WdfSpinLockAcquire(Context->Lock);
            Context->AbortHead = FALSE;
            WdfSpinLockRelease(Context->Lock);
            VssComStopReadTimers(Context);
            return;
        }

        read = VssComReadContext(request);
        now = VssComNow();

        WdfSpinLockAcquire(Context->Lock);
        if (!read->Started)
        {
            read->Plan = vsscom_plan_read(&Context->Timeouts, read->Requested);
            read->Started = TRUE;
            if (read->Plan.has_total_timer)
                read->TotalDeadline = VssComDeadline(read->Plan.total_ms);
        }

        abort = Context->AbortHead;
        Context->AbortHead = FALSE;
        done = FALSE;
        timed_out = FALSE;

        if (!abort)
        {
            const ULONG got =
                vsscom_ring_read(&Context->Rx, read->Buffer + read->Received,
                                 read->Requested - read->Received);
            read->Received += got;
            if (got != 0)
                *Moved = TRUE;

            done = read->Plan.return_now ||
                   vsscom_read_satisfied(&read->Plan, read->Requested,
                                         read->Received);

            // The gap allowed between bytes restarts with each one that
            // arrives. MAXDWORD here means no gap limit.
            if (!done && got > 0 && read->Plan.interval_ms != 0 &&
                read->Plan.interval_ms != VSSCOM_MAXDWORD)
            {
                read->IntervalDeadline = VssComDeadline(read->Plan.interval_ms);
            }

            // A timer that fires calls this function, and a timer stopped too
            // late can call it early, so the deadlines decide.
            if (!done)
            {
                timed_out =
                    (read->TotalDeadline != 0 &&
                     now + VSSCOM_TIMER_SLACK >= read->TotalDeadline) ||
                    (read->IntervalDeadline != 0 &&
                     now + VSSCOM_TIMER_SLACK >= read->IntervalDeadline);
            }
        }
        WdfSpinLockRelease(Context->Lock);

        if (abort)
        {
            VssComStopReadTimers(Context);
            VssComFinishRead(request, STATUS_CANCELLED);
            continue;
        }
        if (done)
        {
            VssComStopReadTimers(Context);
            VssComFinishRead(request, STATUS_SUCCESS);
            continue;
        }
        if (timed_out)
        {
            // STATUS_TIMEOUT with the bytes read so far: Win32 reports a
            // successful short read.
            VssComStopReadTimers(Context);
            VssComFinishRead(request, STATUS_TIMEOUT);
            continue;
        }

        VssComArmReadTimers(Context, read);
        WdfRequestRequeue(request);
        return;
    }
}

// Runs VssComServiceOnce on one thread at a time, and again if something
// changed while it ran. Two threads at the head of the queue would hand the
// reads out of order.
static BOOLEAN VssComServiceReads(_In_ PVSSCOM_DEVICE_CONTEXT Context)
{
    BOOLEAN again;
    BOOLEAN moved = FALSE;

    WdfSpinLockAcquire(Context->Lock);
    if (Context->ServiceBusy)
    {
        Context->ServiceAgain = TRUE;
        WdfSpinLockRelease(Context->Lock);
        return FALSE;
    }
    Context->ServiceBusy = TRUE;
    WdfSpinLockRelease(Context->Lock);

    do
    {
        WdfSpinLockAcquire(Context->Lock);
        Context->ServiceAgain = FALSE;
        WdfSpinLockRelease(Context->Lock);

        VssComServiceOnce(Context, &moved);

        WdfSpinLockAcquire(Context->Lock);
        again = Context->ServiceAgain;
        if (!again)
            Context->ServiceBusy = FALSE;
        WdfSpinLockRelease(Context->Lock);
    } while (again);
    return moved;
}

// Moves bytes between the two ends and serves every request that can go on.
// One thread does it at a time, and again if something changed meanwhile.
// Reading from Rx makes room for the hub's writes, and the hub reading Tx makes
// room for the application's, so the four steps repeat until none moves bytes.
VOID VssComPump(_In_ PVSSCOM_DEVICE_CONTEXT Context)
{
    BOOLEAN again;

    WdfSpinLockAcquire(Context->Lock);
    if (Context->PumpBusy)
    {
        Context->PumpAgain = TRUE;
        WdfSpinLockRelease(Context->Lock);
        return;
    }
    Context->PumpBusy = TRUE;
    WdfSpinLockRelease(Context->Lock);

    do
    {
        BOOLEAN moved;

        WdfSpinLockAcquire(Context->Lock);
        Context->PumpAgain = FALSE;
        WdfSpinLockRelease(Context->Lock);

        do
        {
            moved = FALSE;
            if (VssComServiceHubWrites(Context))
                moved = TRUE;
            if (VssComServiceReads(Context))
                moved = TRUE;
            if (VssComServiceHubReads(Context))
                moved = TRUE;
            if (VssComServiceAppWrites(Context))
                moved = TRUE;
        } while (moved);

        WdfSpinLockAcquire(Context->Lock);
        again = Context->PumpAgain;
        if (!again)
            Context->PumpBusy = FALSE;
        WdfSpinLockRelease(Context->Lock);
    } while (again);
}

// A timer fired: the head read may be due.
VOID VssComEvtReadTotalTimer(_In_ WDFTIMER Timer)
{
    VssComPump(VssComGetContext(WdfTimerGetParentObject(Timer)));
}

VOID VssComEvtReadIntervalTimer(_In_ WDFTIMER Timer)
{
    VssComPump(VssComGetContext(WdfTimerGetParentObject(Timer)));
}

// The framework calls this for a read that was cancelled while it waited.
VOID VssComEvtReadCanceled(_In_ WDFQUEUE Queue, _In_ WDFREQUEST Request)
{
    PVSSCOM_DEVICE_CONTEXT context = VssComGetContext(WdfIoQueueGetDevice(Queue));

    VssComFinishRead(Request, STATUS_CANCELLED);
    VssComPump(context);
}

// -- Events ----------------------------------------------------------------

// Completes the pending WaitCommEvent request, if there is one, with `Events`.
static VOID VssComFinishWait(_In_ PVSSCOM_DEVICE_CONTEXT Context,
                             _In_ ULONG Events)
{
    WDFREQUEST request;

    if (!NT_SUCCESS(WdfIoQueueRetrieveNextRequest(Context->WaitQueue, &request)))
        return;

    WdfSpinLockAcquire(Context->Lock);
    Context->WaitQueued = FALSE;
    WdfSpinLockRelease(Context->Lock);

    VssComCompleteWithData(request, &Events, sizeof(Events));
}

// Something happened that an application can wait for.
VOID VssComRaise(_In_ PVSSCOM_DEVICE_CONTEXT Context, _In_ ULONG Events)
{
    vsscom_u32 fired = 0;
    BOOLEAN complete;

    WdfSpinLockAcquire(Context->Lock);
    complete = vsscom_events_raise(&Context->Events, Events, &fired);
    WdfSpinLockRelease(Context->Lock);

    if (complete)
        VssComFinishWait(Context, fired);
}

VOID VssComEvtWaitCanceled(_In_ WDFQUEUE Queue, _In_ WDFREQUEST Request)
{
    PVSSCOM_DEVICE_CONTEXT context = VssComGetContext(WdfIoQueueGetDevice(Queue));

    WdfSpinLockAcquire(context->Lock);
    context->Events.waiting = 0;
    context->WaitQueued = FALSE;
    WdfSpinLockRelease(context->Lock);

    WdfRequestComplete(Request, STATUS_CANCELLED);
}

// Takes the input lines from where they come from now: the hub's lines while a
// hub handle is open, otherwise the loopback of DTR and RTS. Raises what
// changed.
VOID VssComApplyLines(_In_ PVSSCOM_DEVICE_CONTEXT Context)
{
    ULONG events;

    WdfSpinLockAcquire(Context->Lock);
    {
        const vsscom_u32 levels =
            Context->HubOpen
                ? (Context->HubLines & VSSCOM_LINE_MASK)
                : vsscom_loopback_lines(Context->Dtr != FALSE,
                                        Context->Rts != FALSE);
        events = vsscom_modem_events(Context->ModemLevels, levels);
        Context->ModemDeltas |= vsscom_modem_deltas(Context->ModemLevels, levels);
        Context->ModemLevels = levels;
    }
    WdfSpinLockRelease(Context->Lock);

    if (events != 0)
        VssComRaise(Context, events);
}

// The application set DTR and RTS.
static VOID VssComUpdateLines(_In_ PVSSCOM_DEVICE_CONTEXT Context,
                              _In_ BOOLEAN Dtr, _In_ BOOLEAN Rts)
{
    BOOLEAN changed;

    WdfSpinLockAcquire(Context->Lock);
    changed = (Context->Dtr != Dtr) || (Context->Rts != Rts);
    Context->Dtr = Dtr;
    Context->Rts = Rts;
    WdfSpinLockRelease(Context->Lock);

    VssComApplyLines(Context);
    if (changed)
        VssComBumpState(Context);
}

// -- Reset on close --------------------------------------------------------

// Like a UART driver, the port goes back to quiet when its last handle closes:
// lines dropped, nothing buffered, no mask.
VOID VssComResetPort(_In_ PVSSCOM_DEVICE_CONTEXT Context)
{
    BOOLEAN release;

    WdfSpinLockAcquire(Context->Lock);
    vsscom_ring_clear(&Context->Rx);
    Context->Dtr = FALSE;
    Context->Rts = FALSE;
    // With a hub attached its lines stay as they are, as a real device's would.
    Context->ModemLevels = Context->HubOpen ? (Context->HubLines & VSSCOM_LINE_MASK) : 0;
    Context->ModemDeltas = 0;
    release = vsscom_events_set_mask(&Context->Events, 0);
    WdfSpinLockRelease(Context->Lock);

    if (release)
        VssComFinishWait(Context, 0);
}

// -- Read and write --------------------------------------------------------

// Which end sent a request: VSSCOM_FILE_APP, VSSCOM_FILE_HUB, or 0 for a
// request with no file object.
static ULONG VssComRequestKind(_In_ WDFREQUEST Request)
{
    WDFFILEOBJECT file = WdfRequestGetFileObject(Request);
    return file == NULL ? 0 : VssComFileContext(file)->Kind;
}

static VOID VssComEvtIoRead(_In_ WDFQUEUE Queue, _In_ WDFREQUEST Request,
                            _In_ size_t Length)
{
    PVSSCOM_DEVICE_CONTEXT context = VssComGetContext(WdfIoQueueGetDevice(Queue));
    PVSSCOM_READ_CONTEXT read = NULL;
    WDF_OBJECT_ATTRIBUTES attributes;
    PVOID buffer = NULL;
    NTSTATUS status;
    const ULONG kind = VssComRequestKind(Request);

    if (kind == VSSCOM_FILE_HUB)
    {
        VssComHubRead(context, Request, Length);
        return;
    }
    if (kind != VSSCOM_FILE_APP)
    {
        WdfRequestComplete(Request, STATUS_INVALID_DEVICE_REQUEST);
        return;
    }

    if (Length == 0)
    {
        WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, 0);
        return;
    }
    if (Length > VssComMaxUlong)
    {
        WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
        return;
    }

    status = WdfRequestRetrieveOutputBuffer(Request, Length, &buffer, NULL);
    if (!NT_SUCCESS(status))
    {
        WdfRequestComplete(Request, status);
        return;
    }

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, VSSCOM_READ_CONTEXT);
    status = WdfObjectAllocateContext(Request, &attributes, (PVOID*)&read);
    if (!NT_SUCCESS(status))
    {
        WdfRequestComplete(Request, status);
        return;
    }
    read->Buffer = (PUCHAR)buffer;
    read->Requested = (ULONG)Length;

    status = WdfRequestForwardToIoQueue(Request, context->ReadQueue);
    if (!NT_SUCCESS(status))
    {
        WdfRequestComplete(Request, status);
        return;
    }

    VssComPump(context);
}

static VOID VssComEvtIoWrite(_In_ WDFQUEUE Queue, _In_ WDFREQUEST Request,
                             _In_ size_t Length)
{
    PVSSCOM_DEVICE_CONTEXT context = VssComGetContext(WdfIoQueueGetDevice(Queue));
    PVOID buffer = NULL;
    ULONG written;
    NTSTATUS status;
    BOOLEAN hub;
    const ULONG kind = VssComRequestKind(Request);

    if (kind == VSSCOM_FILE_HUB)
    {
        VssComHubWrite(context, Request, Length);
        return;
    }
    if (kind != VSSCOM_FILE_APP)
    {
        WdfRequestComplete(Request, STATUS_INVALID_DEVICE_REQUEST);
        return;
    }

    if (Length == 0)
    {
        WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, 0);
        return;
    }
    if (Length > VssComMaxUlong)
    {
        WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
        return;
    }

    // With a hub attached the bytes go to it, and the write waits for room.
    WdfSpinLockAcquire(context->Lock);
    hub = context->HubOpen;
    WdfSpinLockRelease(context->Lock);
    if (hub)
    {
        VssComAppWriteToHub(context, Request, Length);
        return;
    }

    status = WdfRequestRetrieveInputBuffer(Request, Length, &buffer, NULL);
    if (!NT_SUCCESS(status))
    {
        WdfRequestComplete(Request, status);
        return;
    }

    WdfSpinLockAcquire(context->Lock);
    written = vsscom_ring_write(&context->Rx, (const vsscom_u8*)buffer,
                                (vsscom_u32)Length);
    WdfSpinLockRelease(context->Lock);

    WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, written);

    if (written != 0)
    {
        VssComPump(context);
        VssComRaise(context, VSSCOM_EV_RXCHAR);
    }
    VssComRaise(context, VSSCOM_EV_TXEMPTY);
}

// -- Control requests ------------------------------------------------------

static VOID VssComProperties(_Out_ PSERIAL_COMMPROP Properties)
{
    RtlZeroMemory(Properties, sizeof(*Properties));
    Properties->PacketLength = (USHORT)sizeof(SERIAL_COMMPROP);
    Properties->PacketVersion = 2;
    Properties->ServiceMask = SERIAL_SP_SERIALCOMM;
    Properties->MaxBaud = SERIAL_BAUD_USER;
    Properties->ProvSubType = SERIAL_SP_RS232;
    Properties->ProvCapabilities =
        SERIAL_PCF_DTRDSR | SERIAL_PCF_RTSCTS | SERIAL_PCF_CD |
        SERIAL_PCF_PARITY_CHECK | SERIAL_PCF_XONXOFF | SERIAL_PCF_SETXCHAR |
        SERIAL_PCF_TOTALTIMEOUTS | SERIAL_PCF_INTTIMEOUTS;
    Properties->SettableParams =
        SERIAL_SP_PARITY | SERIAL_SP_BAUD | SERIAL_SP_DATABITS |
        SERIAL_SP_STOPBITS | SERIAL_SP_HANDSHAKING | SERIAL_SP_PARITY_CHECK |
        SERIAL_SP_CARRIER_DETECT;
    Properties->SettableBaud =
        SERIAL_BAUD_075 | SERIAL_BAUD_110 | SERIAL_BAUD_134_5 |
        SERIAL_BAUD_150 | SERIAL_BAUD_300 | SERIAL_BAUD_600 |
        SERIAL_BAUD_1200 | SERIAL_BAUD_1800 | SERIAL_BAUD_2400 |
        SERIAL_BAUD_4800 | SERIAL_BAUD_7200 | SERIAL_BAUD_9600 |
        SERIAL_BAUD_14400 | SERIAL_BAUD_19200 | SERIAL_BAUD_38400 |
        SERIAL_BAUD_56K | SERIAL_BAUD_57600 | SERIAL_BAUD_115200 |
        SERIAL_BAUD_128K | SERIAL_BAUD_USER;
    Properties->SettableData = SERIAL_DATABITS_5 | SERIAL_DATABITS_6 |
                               SERIAL_DATABITS_7 | SERIAL_DATABITS_8;
    Properties->SettableStopParity =
        SERIAL_STOPBITS_10 | SERIAL_STOPBITS_15 | SERIAL_STOPBITS_20 |
        SERIAL_PARITY_NONE | SERIAL_PARITY_ODD | SERIAL_PARITY_EVEN |
        SERIAL_PARITY_MARK | SERIAL_PARITY_SPACE;
    Properties->MaxTxQueue = VSSCOM_TX_SIZE;
    Properties->MaxRxQueue = VSSCOM_RX_SIZE;
    Properties->CurrentTxQueue = VSSCOM_TX_SIZE;
    Properties->CurrentRxQueue = VSSCOM_RX_SIZE;
}

static VOID VssComEvtIoDeviceControl(_In_ WDFQUEUE Queue,
                                     _In_ WDFREQUEST Request,
                                     _In_ size_t OutputBufferLength,
                                     _In_ size_t InputBufferLength,
                                     _In_ ULONG IoControlCode)
{
    PVSSCOM_DEVICE_CONTEXT context = VssComGetContext(WdfIoQueueGetDevice(Queue));
    const ULONG kind = VssComRequestKind(Request);
    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    if (IoControlCode == IOCTL_VSSCOM_GET_VERSION)
    {
        VSSCOM_VERSION_INFO info = {VSSCOM_VERSION, VSSCOM_RX_SIZE};
        VssComCompleteWithData(Request, &info, sizeof(info));
        return;
    }
    if (kind == VSSCOM_FILE_HUB)
    {
        VssComHubIoControl(context, Request, IoControlCode);
        return;
    }
    if (kind != VSSCOM_FILE_APP)
    {
        WdfRequestComplete(Request, STATUS_INVALID_DEVICE_REQUEST);
        return;
    }

    switch (IoControlCode)
    {
    case IOCTL_SERIAL_GET_BAUD_RATE:
    {
        SERIAL_BAUD_RATE baud = {0};
        WdfSpinLockAcquire(context->Lock);
        baud.BaudRate = context->BaudRate;
        WdfSpinLockRelease(context->Lock);
        VssComCompleteWithData(Request, &baud, sizeof(baud));
        return;
    }

    case IOCTL_SERIAL_SET_BAUD_RATE:
    {
        SERIAL_BAUD_RATE baud = {0};
        if (!VssComFetchInput(Request, &baud, sizeof(baud)) || baud.BaudRate == 0)
        {
            WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
            return;
        }
        WdfSpinLockAcquire(context->Lock);
        context->BaudRate = baud.BaudRate;
        WdfSpinLockRelease(context->Lock);
        VssComBumpState(context);
        WdfRequestComplete(Request, STATUS_SUCCESS);
        return;
    }

    case IOCTL_SERIAL_GET_LINE_CONTROL:
    {
        SERIAL_LINE_CONTROL control;
        WdfSpinLockAcquire(context->Lock);
        control = context->LineControl;
        WdfSpinLockRelease(context->Lock);
        VssComCompleteWithData(Request, &control, sizeof(control));
        return;
    }

    case IOCTL_SERIAL_SET_LINE_CONTROL:
    {
        SERIAL_LINE_CONTROL control = {0};
        if (!VssComFetchInput(Request, &control, sizeof(control)))
        {
            WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
            return;
        }
        WdfSpinLockAcquire(context->Lock);
        context->LineControl = control;
        WdfSpinLockRelease(context->Lock);
        VssComBumpState(context);
        WdfRequestComplete(Request, STATUS_SUCCESS);
        return;
    }

    case IOCTL_SERIAL_GET_TIMEOUTS:
    {
        SERIAL_TIMEOUTS timeouts;
        WdfSpinLockAcquire(context->Lock);
        timeouts.ReadIntervalTimeout = context->Timeouts.read_interval;
        timeouts.ReadTotalTimeoutMultiplier =
            context->Timeouts.read_total_multiplier;
        timeouts.ReadTotalTimeoutConstant = context->Timeouts.read_total_constant;
        timeouts.WriteTotalTimeoutMultiplier =
            context->Timeouts.write_total_multiplier;
        timeouts.WriteTotalTimeoutConstant =
            context->Timeouts.write_total_constant;
        WdfSpinLockRelease(context->Lock);
        VssComCompleteWithData(Request, &timeouts, sizeof(timeouts));
        return;
    }

    case IOCTL_SERIAL_SET_TIMEOUTS:
    {
        SERIAL_TIMEOUTS timeouts = {0};
        vsscom_timeouts next;
        if (!VssComFetchInput(Request, &timeouts, sizeof(timeouts)))
        {
            WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
            return;
        }
        next.read_interval = timeouts.ReadIntervalTimeout;
        next.read_total_multiplier = timeouts.ReadTotalTimeoutMultiplier;
        next.read_total_constant = timeouts.ReadTotalTimeoutConstant;
        next.write_total_multiplier = timeouts.WriteTotalTimeoutMultiplier;
        next.write_total_constant = timeouts.WriteTotalTimeoutConstant;
        if (!vsscom_timeouts_valid(&next))
        {
            WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
            return;
        }
        WdfSpinLockAcquire(context->Lock);
        context->Timeouts = next;
        WdfSpinLockRelease(context->Lock);
        WdfRequestComplete(Request, STATUS_SUCCESS);
        return;
    }

    case IOCTL_SERIAL_GET_HANDFLOW:
    {
        SERIAL_HANDFLOW handflow;
        WdfSpinLockAcquire(context->Lock);
        handflow = context->HandFlow;
        WdfSpinLockRelease(context->Lock);
        VssComCompleteWithData(Request, &handflow, sizeof(handflow));
        return;
    }

    case IOCTL_SERIAL_SET_HANDFLOW:
    {
        SERIAL_HANDFLOW handflow = {0};
        if (!VssComFetchInput(Request, &handflow, sizeof(handflow)))
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
    {
        SERIAL_CHARS chars;
        WdfSpinLockAcquire(context->Lock);
        chars = context->Chars;
        WdfSpinLockRelease(context->Lock);
        VssComCompleteWithData(Request, &chars, sizeof(chars));
        return;
    }

    case IOCTL_SERIAL_SET_CHARS:
    {
        SERIAL_CHARS chars = {0};
        if (!VssComFetchInput(Request, &chars, sizeof(chars)))
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

    case IOCTL_SERIAL_GET_PROPERTIES:
    {
        SERIAL_COMMPROP properties;
        VssComProperties(&properties);
        VssComCompleteWithData(Request, &properties, sizeof(properties));
        return;
    }

    case IOCTL_SERIAL_SET_QUEUE_SIZE:
    {
        // The ring has a fixed size; accept any request, as a UART would.
        SERIAL_QUEUE_SIZE size = {0};
        if (!VssComFetchInput(Request, &size, sizeof(size)))
        {
            WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
            return;
        }
        WdfRequestComplete(Request, STATUS_SUCCESS);
        return;
    }

    case IOCTL_SERIAL_GET_COMMSTATUS:
    {
        SERIAL_STATUS status = {0};
        WdfSpinLockAcquire(context->Lock);
        status.AmountInInQueue = vsscom_ring_count(&context->Rx);
        status.AmountInOutQueue = vsscom_ring_count(&context->Tx);
        WdfSpinLockRelease(context->Lock);
        VssComCompleteWithData(Request, &status, sizeof(status));
        return;
    }

    case IOCTL_SERIAL_SET_DTR:
    case IOCTL_SERIAL_CLR_DTR:
    case IOCTL_SERIAL_SET_RTS:
    case IOCTL_SERIAL_CLR_RTS:
    {
        BOOLEAN dtr;
        BOOLEAN rts;
        WdfSpinLockAcquire(context->Lock);
        dtr = context->Dtr;
        rts = context->Rts;
        WdfSpinLockRelease(context->Lock);

        if (IoControlCode == IOCTL_SERIAL_SET_DTR)
            dtr = TRUE;
        else if (IoControlCode == IOCTL_SERIAL_CLR_DTR)
            dtr = FALSE;
        else if (IoControlCode == IOCTL_SERIAL_SET_RTS)
            rts = TRUE;
        else
            rts = FALSE;

        VssComUpdateLines(context, dtr, rts);
        WdfRequestComplete(Request, STATUS_SUCCESS);
        return;
    }

    case IOCTL_SERIAL_GET_DTRRTS:
    {
        ULONG lines = 0;
        WdfSpinLockAcquire(context->Lock);
        if (context->Dtr)
            lines |= SERIAL_DTR_STATE;
        if (context->Rts)
            lines |= SERIAL_RTS_STATE;
        WdfSpinLockRelease(context->Lock);
        VssComCompleteWithData(Request, &lines, sizeof(lines));
        return;
    }

    case IOCTL_SERIAL_GET_MODEMSTATUS:
    {
        ULONG modem;
        WdfSpinLockAcquire(context->Lock);
        modem = context->ModemLevels | context->ModemDeltas;
        context->ModemDeltas = 0;
        WdfSpinLockRelease(context->Lock);
        VssComCompleteWithData(Request, &modem, sizeof(modem));
        return;
    }

    case IOCTL_SERIAL_SET_BREAK_ON:
    case IOCTL_SERIAL_SET_BREAK_OFF:
    {
        BOOLEAN changed;
        const BOOLEAN on = IoControlCode == IOCTL_SERIAL_SET_BREAK_ON;
        WdfSpinLockAcquire(context->Lock);
        changed = context->Break != on;
        context->Break = on;
        WdfSpinLockRelease(context->Lock);
        if (changed)
            VssComBumpState(context);
        WdfRequestComplete(Request, STATUS_SUCCESS);
        return;
    }

    case IOCTL_SERIAL_SET_XOFF:
    case IOCTL_SERIAL_SET_XON:
    case IOCTL_SERIAL_RESET_DEVICE:
        WdfRequestComplete(Request, STATUS_SUCCESS);
        return;

    case IOCTL_SERIAL_CONFIG_SIZE:
    {
        ULONG size = 0;
        VssComCompleteWithData(Request, &size, sizeof(size));
        return;
    }

    case IOCTL_SERIAL_SET_WAIT_MASK:
    {
        ULONG mask = 0;
        BOOLEAN release;
        if (!VssComFetchInput(Request, &mask, sizeof(mask)))
        {
            WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
            return;
        }
        WdfSpinLockAcquire(context->Lock);
        release = vsscom_events_set_mask(&context->Events, mask);
        WdfSpinLockRelease(context->Lock);

        // A wait that was pending ends with no events, as a new mask replaces
        // the old one.
        if (release)
            VssComFinishWait(context, 0);
        WdfRequestComplete(Request, STATUS_SUCCESS);
        return;
    }

    case IOCTL_SERIAL_GET_WAIT_MASK:
    {
        ULONG mask;
        WdfSpinLockAcquire(context->Lock);
        mask = context->Events.mask;
        WdfSpinLockRelease(context->Lock);
        VssComCompleteWithData(Request, &mask, sizeof(mask));
        return;
    }

    case IOCTL_SERIAL_WAIT_ON_MASK:
    {
        vsscom_u32 fired = 0;
        BOOLEAN ready;
        NTSTATUS status;

        WdfSpinLockAcquire(context->Lock);
        if (context->WaitQueued)
        {
            WdfSpinLockRelease(context->Lock);
            WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
            return;
        }
        context->WaitQueued = TRUE;
        WdfSpinLockRelease(context->Lock);

        // The request is parked before the event state says "waiting", so an
        // event that arrives in between finds a request to complete.
        status = WdfRequestForwardToIoQueue(Request, context->WaitQueue);
        if (!NT_SUCCESS(status))
        {
            WdfSpinLockAcquire(context->Lock);
            context->WaitQueued = FALSE;
            WdfSpinLockRelease(context->Lock);
            WdfRequestComplete(Request, status);
            return;
        }

        WdfSpinLockAcquire(context->Lock);
        ready = vsscom_events_wait(&context->Events, &fired);
        WdfSpinLockRelease(context->Lock);

        // Events were already waiting in the history: take the request back
        // and finish it now.
        if (ready)
            VssComFinishWait(context, fired);
        return;
    }

    case IOCTL_SERIAL_PURGE:
    {
        ULONG flags = 0;
        BOOLEAN abort_read;
        if (!VssComFetchInput(Request, &flags, sizeof(flags)))
        {
            WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
            return;
        }

        abort_read = (flags & SERIAL_PURGE_RXABORT) != 0;
        if ((flags & (SERIAL_PURGE_RXCLEAR | SERIAL_PURGE_RXABORT)) != 0)
        {
            WdfSpinLockAcquire(context->Lock);
            vsscom_ring_clear(&context->Rx);
            WdfSpinLockRelease(context->Lock);
        }
        if ((flags & (SERIAL_PURGE_TXCLEAR | SERIAL_PURGE_TXABORT)) != 0)
        {
            WdfSpinLockAcquire(context->Lock);
            vsscom_ring_clear(&context->Tx);
            WdfSpinLockRelease(context->Lock);
        }
        // Aborting writes ends those that are waiting for the hub.
        if ((flags & SERIAL_PURGE_TXABORT) != 0)
            VssComAbortAppWrites(context, STATUS_CANCELLED);

        // Aborting reads ends the one that is waiting with what it holds.
        if (abort_read)
        {
            WdfSpinLockAcquire(context->Lock);
            context->AbortHead = TRUE;
            WdfSpinLockRelease(context->Lock);
        }
        // Clearing made room, and an abort needs the read queue served.
        VssComPump(context);
        WdfRequestComplete(Request, STATUS_SUCCESS);
        return;
    }

    case IOCTL_SERIAL_IMMEDIATE_CHAR:
    {
        UCHAR byte = 0;
        ULONG stored;
        BOOLEAN hub;
        if (!VssComFetchInput(Request, &byte, sizeof(byte)))
        {
            WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
            return;
        }
        // The byte goes to the hub when there is one, and loops back if not.
        WdfSpinLockAcquire(context->Lock);
        hub = context->HubOpen;
        stored = vsscom_ring_write(hub ? &context->Tx : &context->Rx, &byte, 1);
        WdfSpinLockRelease(context->Lock);
        WdfRequestComplete(Request, STATUS_SUCCESS);
        if (stored != 0)
        {
            VssComPump(context);
            if (!hub)
                VssComRaise(context, VSSCOM_EV_RXCHAR);
        }
        return;
    }

    default:
        WdfRequestComplete(Request, STATUS_INVALID_DEVICE_REQUEST);
        return;
    }
}

// -- Queues ----------------------------------------------------------------

NTSTATUS VssComCreateQueues(_In_ PVSSCOM_DEVICE_CONTEXT Context)
{
    WDFDEVICE device = Context->Device;
    WDF_IO_QUEUE_CONFIG config;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_TIMER_CONFIG timer;
    WDFQUEUE queue;
    NTSTATUS status;

    // Control requests: the default queue, many at once.
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&config, WdfIoQueueDispatchParallel);
    config.EvtIoDeviceControl = VssComEvtIoDeviceControl;
    status = WdfIoQueueCreate(device, &config, WDF_NO_OBJECT_ATTRIBUTES, &queue);
    if (!NT_SUCCESS(status))
        return status;

    // Reads arrive here and are parked in ReadQueue; writes are handled at once.
    WDF_IO_QUEUE_CONFIG_INIT(&config, WdfIoQueueDispatchParallel);
    config.EvtIoRead = VssComEvtIoRead;
    config.EvtIoWrite = VssComEvtIoWrite;
    status = WdfIoQueueCreate(device, &config, WDF_NO_OBJECT_ATTRIBUTES, &queue);
    if (!NT_SUCCESS(status))
        return status;
    status = WdfDeviceConfigureRequestDispatching(device, queue,
                                                  WdfRequestTypeRead);
    if (!NT_SUCCESS(status))
        return status;
    status = WdfDeviceConfigureRequestDispatching(device, queue,
                                                  WdfRequestTypeWrite);
    if (!NT_SUCCESS(status))
        return status;

    WDF_IO_QUEUE_CONFIG_INIT(&config, WdfIoQueueDispatchManual);
    config.EvtIoCanceledOnQueue = VssComEvtReadCanceled;
    status = WdfIoQueueCreate(device, &config, WDF_NO_OBJECT_ATTRIBUTES,
                              &Context->ReadQueue);
    if (!NT_SUCCESS(status))
        return status;

    WDF_IO_QUEUE_CONFIG_INIT(&config, WdfIoQueueDispatchManual);
    config.EvtIoCanceledOnQueue = VssComEvtWaitCanceled;
    status = WdfIoQueueCreate(device, &config, WDF_NO_OBJECT_ATTRIBUTES,
                              &Context->WaitQueue);
    if (!NT_SUCCESS(status))
        return status;

    status = VssComCreateHubQueues(Context);
    if (!NT_SUCCESS(status))
        return status;

    // The timers take the lock themselves, so the framework does not
    // serialise their callbacks with the device.
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = device;
    WDF_TIMER_CONFIG_INIT(&timer, VssComEvtReadTotalTimer);
    timer.AutomaticSerialization = FALSE;
    status = WdfTimerCreate(&timer, &attributes, &Context->ReadTotalTimer);
    if (!NT_SUCCESS(status))
        return status;

    WDF_TIMER_CONFIG_INIT(&timer, VssComEvtReadIntervalTimer);
    timer.AutomaticSerialization = FALSE;
    return WdfTimerCreate(&timer, &attributes, &Context->ReadIntervalTimer);
}
