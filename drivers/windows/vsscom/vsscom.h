//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// vsscom: the device context and the entry points shared by driver.c and io.c.
//

#ifndef VSSCOM_H
#define VSSCOM_H

#include <ntddk.h>
#include <wdf.h>
#include <ntstrsafe.h>

// initguid.h first, so that driver.c defines the GUIDs of ntddser.h and
// public.h and the linker finds them. io.c only declares them.
#ifdef VSSCOM_DEFINE_GUIDS
#include <initguid.h>
#endif
#include <ntddser.h>

#include "public.h"
#include "core/vsscom_core.h"

#define VSSCOM_POOL_TAG 'mssv'

// The bytes a port holds between a write and the read that returns them.
#define VSSCOM_RX_SIZE (64 * 1024)

// What one read request remembers while it waits in the read queue.
typedef struct _VSSCOM_READ_CONTEXT
{
    PUCHAR Buffer;
    ULONG Requested;
    ULONG Received;
    BOOLEAN Started;
    vsscom_read_plan Plan;
    ULONGLONG TotalDeadline;    // interrupt time, 100 ns units; 0 for none
    ULONGLONG IntervalDeadline; // 0 until the first byte has arrived
} VSSCOM_READ_CONTEXT, *PVSSCOM_READ_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(VSSCOM_READ_CONTEXT, VssComReadContext)

typedef struct _VSSCOM_DEVICE_CONTEXT
{
    WDFDEVICE Device;
    PWSTR PdoName;
    BOOLEAN CreatedSerialCommValue;

    // Reads wait here in arrival order; only the first is served. Manual, so
    // the framework cancels a request that is waiting here without our help.
    WDFQUEUE ReadQueue;
    // At most one WaitCommEvent request waits here.
    WDFQUEUE WaitQueue;
    WDFTIMER ReadTotalTimer;
    WDFTIMER ReadIntervalTimer;

    // Everything below is guarded by Lock.
    WDFSPINLOCK Lock;
    vsscom_u8 RxStorage[VSSCOM_RX_SIZE];
    vsscom_ring Rx;
    vsscom_timeouts Timeouts;
    vsscom_events Events;
    ULONG BaudRate;
    SERIAL_LINE_CONTROL LineControl;
    SERIAL_HANDFLOW HandFlow;
    SERIAL_CHARS Chars;
    BOOLEAN Dtr;
    BOOLEAN Rts;
    ULONG ModemLevels; // the input lines as last reported
    ULONG ModemDeltas; // changes since IOCTL_SERIAL_GET_MODEMSTATUS last ran
    BOOLEAN WaitQueued;   // a WaitCommEvent request is parked in WaitQueue
    BOOLEAN ServiceBusy;  // one thread is serving the read queue
    BOOLEAN ServiceAgain; // something changed while it was
    BOOLEAN AbortHead;    // PurgeComm asked for the waiting read to end
} VSSCOM_DEVICE_CONTEXT, *PVSSCOM_DEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(VSSCOM_DEVICE_CONTEXT, VssComGetContext)

// driver.c
NTSTATUS VssComRegisterPort(_In_ PVSSCOM_DEVICE_CONTEXT Context);

// io.c
NTSTATUS VssComCreateQueues(_In_ PVSSCOM_DEVICE_CONTEXT Context);
VOID VssComResetPort(_In_ PVSSCOM_DEVICE_CONTEXT Context);

#endif // VSSCOM_H
