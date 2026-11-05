//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The serial rules of the virtual COM port, with no dependency on the kernel.
//
// The driver (driver.c) turns Windows requests into calls to these functions
// and carries out what they decide: complete a request, keep it waiting, arm a
// timer. The same code is built for the host and unit-tested there, because a
// driver cannot be debugged cheaply and these rules are where mistakes hide:
// read timeouts, wait-mask events, the byte ring and the modem lines.
//
// Plain C, no allocation, no locking. The caller serialises access.
//

#ifndef VSSCOM_CORE_H
#define VSSCOM_CORE_H

// No C library headers: the kernel build of this file must not pull in the
// user-mode runtime, so the few types it needs are defined here. Every system
// the studio supports has a 32-bit unsigned int and a 64-bit unsigned long long.
typedef unsigned char vsscom_u8;
typedef unsigned int vsscom_u32;
typedef unsigned long long vsscom_u64;
// 0 or 1.
typedef unsigned char vsscom_bool;

/// The value Windows uses for "no limit" in a timeout field.
#define VSSCOM_MAXDWORD 0xFFFFFFFFu

// -- The byte ring
// ---------------------------------------------------------

typedef struct vsscom_ring
{
    vsscom_u8* data;
    vsscom_u32 size;
    vsscom_u32 head; // index of the oldest byte
    vsscom_u32 count;
} vsscom_ring;

/// `storage` is owned by the caller and must hold `size` bytes.
void vsscom_ring_init(vsscom_ring* ring, vsscom_u8* storage, vsscom_u32 size);
vsscom_u32 vsscom_ring_count(const vsscom_ring* ring);
vsscom_u32 vsscom_ring_space(const vsscom_ring* ring);
/// Copies as many bytes as fit and returns how many.
vsscom_u32 vsscom_ring_write(vsscom_ring* ring, const vsscom_u8* bytes,
                           vsscom_u32 length);
/// Removes up to `length` of the oldest bytes and returns how many.
vsscom_u32 vsscom_ring_read(vsscom_ring* ring, vsscom_u8* bytes,
                          vsscom_u32 length);
void vsscom_ring_clear(vsscom_ring* ring);

// -- Timeouts
// --------------------------------------------------------------

/// The five numbers of COMMTIMEOUTS, in milliseconds.
typedef struct vsscom_timeouts
{
    vsscom_u32 read_interval;
    vsscom_u32 read_total_multiplier;
    vsscom_u32 read_total_constant;
    vsscom_u32 write_total_multiplier;
    vsscom_u32 write_total_constant;
} vsscom_timeouts;

/// False for the one combination Windows refuses: all three read values set
/// to MAXDWORD.
vsscom_bool vsscom_timeouts_valid(const vsscom_timeouts* timeouts);

/// What a read of `requested` bytes does under the current timeouts.
typedef struct vsscom_read_plan
{
    /// Complete at once with whatever is in the ring, possibly nothing.
    vsscom_bool return_now;
    /// Complete as soon as one byte is available, with all that is there.
    vsscom_bool return_on_first_byte;
    /// A total timer applies; `total_ms` is its length.
    vsscom_bool has_total_timer;
    vsscom_u32 total_ms;
    /// The gap allowed between bytes once the first has arrived, or 0 for
    /// none.
    vsscom_u32 interval_ms;
} vsscom_read_plan;

vsscom_read_plan vsscom_plan_read(const vsscom_timeouts* timeouts,
                                  vsscom_u32 requested);

/// True when a read has everything it asked for, or the plan says that what
/// it has is enough. Timers are the caller's: an expired timer completes
/// the read with what it holds.
vsscom_bool vsscom_read_satisfied(const vsscom_read_plan* plan, vsscom_u32 requested,
                           vsscom_u32 received);

/// The total write timeout for `length` bytes in milliseconds, or 0 for
/// none.
vsscom_u32 vsscom_write_timeout_ms(const vsscom_timeouts* timeouts,
                                 vsscom_u32 length);

// -- Events (SetCommMask and WaitCommEvent) --------------------------------

// The same values as SERIAL_EV_* in ntddser.h and EV_* in winbase.h.
#define VSSCOM_EV_RXCHAR 0x0001u
#define VSSCOM_EV_RXFLAG 0x0002u
#define VSSCOM_EV_TXEMPTY 0x0004u
#define VSSCOM_EV_CTS 0x0008u
#define VSSCOM_EV_DSR 0x0010u
#define VSSCOM_EV_RLSD 0x0020u
#define VSSCOM_EV_BREAK 0x0040u
#define VSSCOM_EV_ERR 0x0080u
#define VSSCOM_EV_RING 0x0100u

typedef struct vsscom_events
{
    vsscom_u32 mask; // the events the application asked about
    vsscom_u32
        history;  // masked events that happened while nobody was waiting
    vsscom_bool waiting; // a wait request is pending
} vsscom_events;

void vsscom_events_init(vsscom_events* events);

/// Sets the mask. History is dropped. Returns true when a pending wait must
/// be completed now with no events.
vsscom_bool vsscom_events_set_mask(vsscom_events* events, vsscom_u32 mask);

/// Starts a wait. Returns true and sets `*fired` when events are already
/// waiting in the history; the history is then empty. Returns false when
/// the request must stay pending (and marks it pending). A second wait
/// while one is pending also returns false and leaves `*fired` untouched;
/// the caller checks `waiting` first and refuses it.
vsscom_bool vsscom_events_wait(vsscom_events* events, vsscom_u32* fired);

/// Records events that happened. Returns true and sets `*fired` when a
/// pending wait completes because of them.
vsscom_bool vsscom_events_raise(vsscom_events* events, vsscom_u32 happened,
                         vsscom_u32* fired);

// -- Modem lines -----------------------------------------------------------

// The same values as SERIAL_MSR_* in ntddser.h.
#define VSSCOM_MSR_DCTS 0x01u
#define VSSCOM_MSR_DDSR 0x02u
#define VSSCOM_MSR_TERI 0x04u
#define VSSCOM_MSR_DDCD 0x08u
#define VSSCOM_MSR_CTS 0x10u
#define VSSCOM_MSR_DSR 0x20u
#define VSSCOM_MSR_RI 0x40u
#define VSSCOM_MSR_DCD 0x80u

/// The input lines a loopback port reports: RTS comes back as CTS, and DTR
/// as DSR and carrier detect, the way a null-modem cable to itself would.
vsscom_u32 vsscom_loopback_lines(vsscom_bool dtr, vsscom_bool rts);

/// The change bits (DCTS, DDSR, DDCD) between two readings of the level
/// bits.
vsscom_u32 vsscom_modem_deltas(vsscom_u32 before, vsscom_u32 after);

/// The events (CTS, DSR, RLSD) that a change between two readings raises.
vsscom_u32 vsscom_modem_events(vsscom_u32 before, vsscom_u32 after);

#endif // VSSCOM_CORE_H
