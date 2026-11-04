//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "vsscom_core.h"

// -- The byte ring ---------------------------------------------------------

static void copy_bytes(vsscom_u8* to, const vsscom_u8* from, vsscom_u32 count)
{
    while (count--)
        *to++ = *from++;
}

void vsscom_ring_init(vsscom_ring* ring, vsscom_u8* storage, vsscom_u32 size)
{
    ring->data = storage;
    ring->size = size;
    ring->head = 0;
    ring->count = 0;
}

vsscom_u32 vsscom_ring_count(const vsscom_ring* ring)
{
    return ring->count;
}

vsscom_u32 vsscom_ring_space(const vsscom_ring* ring)
{
    return ring->size - ring->count;
}

vsscom_u32 vsscom_ring_write(vsscom_ring* ring, const vsscom_u8* bytes,
                           vsscom_u32 length)
{
    const vsscom_u32 room = vsscom_ring_space(ring);
    const vsscom_u32 take = length < room ? length : room;
    if (take == 0)
        return 0;

    // The first free slot, then at most two runs: up to the end, then the
    // start.
    const vsscom_u32 tail = (ring->head + ring->count) % ring->size;
    const vsscom_u32 first = take < ring->size - tail ? take : ring->size - tail;
    copy_bytes(ring->data + tail, bytes, first);
    if (take > first)
        copy_bytes(ring->data, bytes + first, take - first);
    ring->count += take;
    return take;
}

vsscom_u32 vsscom_ring_read(vsscom_ring* ring, vsscom_u8* bytes, vsscom_u32 length)
{
    const vsscom_u32 take = length < ring->count ? length : ring->count;
    if (take == 0)
        return 0;

    const vsscom_u32 first =
        take < ring->size - ring->head ? take : ring->size - ring->head;
    copy_bytes(bytes, ring->data + ring->head, first);
    if (take > first)
        copy_bytes(bytes + first, ring->data, take - first);
    ring->head = (ring->head + take) % ring->size;
    ring->count -= take;
    return take;
}

void vsscom_ring_clear(vsscom_ring* ring)
{
    ring->head = 0;
    ring->count = 0;
}

// -- Timeouts --------------------------------------------------------------

vsscom_bool vsscom_timeouts_valid(const vsscom_timeouts* timeouts)
{
    return !(timeouts->read_interval == VSSCOM_MAXDWORD &&
             timeouts->read_total_multiplier == VSSCOM_MAXDWORD &&
             timeouts->read_total_constant == VSSCOM_MAXDWORD);
}

// multiplier * length + constant, held at the largest value a timer can take
// below MAXDWORD so that a huge product still counts as "a very long time"
// and never wraps to a short one.
static vsscom_u32 total_ms(vsscom_u32 multiplier, vsscom_u32 constant,
                         vsscom_u32 length)
{
    const vsscom_u64 total = (vsscom_u64)multiplier * length + constant;
    return total >= VSSCOM_MAXDWORD ? VSSCOM_MAXDWORD - 1u : (vsscom_u32)total;
}

vsscom_read_plan vsscom_plan_read(const vsscom_timeouts* timeouts,
                                  vsscom_u32 requested)
{
    vsscom_read_plan plan;
    plan.return_now = 0;
    plan.return_on_first_byte = 0;
    plan.has_total_timer = 0;
    plan.total_ms = 0;
    plan.interval_ms = 0;

    // MAXDWORD interval with no total timeout: return what is there, now.
    if (timeouts->read_interval == VSSCOM_MAXDWORD &&
        timeouts->read_total_multiplier == 0 &&
        timeouts->read_total_constant == 0)
    {
        plan.return_now = 1;
        return plan;
    }

    // MAXDWORD interval and multiplier with a constant: wait up to the constant
    // for the first byte, then return with what is there.
    if (timeouts->read_interval == VSSCOM_MAXDWORD &&
        timeouts->read_total_multiplier == VSSCOM_MAXDWORD &&
        timeouts->read_total_constant > 0 &&
        timeouts->read_total_constant < VSSCOM_MAXDWORD)
    {
        plan.return_on_first_byte = 1;
        plan.has_total_timer = 1;
        plan.total_ms = timeouts->read_total_constant;
        return plan;
    }

    if (timeouts->read_total_multiplier != 0 ||
        timeouts->read_total_constant != 0)
    {
        plan.has_total_timer = 1;
        plan.total_ms = total_ms(timeouts->read_total_multiplier,
                                 timeouts->read_total_constant, requested);
    }
    plan.interval_ms = timeouts->read_interval;
    return plan;
}

vsscom_bool vsscom_read_satisfied(const vsscom_read_plan* plan, vsscom_u32 requested,
                           vsscom_u32 received)
{
    if (received >= requested)
        return 1;
    return plan->return_on_first_byte && received > 0;
}

vsscom_u32 vsscom_write_timeout_ms(const vsscom_timeouts* timeouts,
                                 vsscom_u32 length)
{
    if (timeouts->write_total_multiplier == 0 &&
        timeouts->write_total_constant == 0)
        return 0;
    return total_ms(timeouts->write_total_multiplier,
                    timeouts->write_total_constant, length);
}

// -- Events ----------------------------------------------------------------

void vsscom_events_init(vsscom_events* events)
{
    events->mask = 0;
    events->history = 0;
    events->waiting = 0;
}

vsscom_bool vsscom_events_set_mask(vsscom_events* events, vsscom_u32 mask)
{
    const vsscom_bool release = events->waiting;
    events->mask = mask;
    events->history = 0;
    events->waiting = 0;
    return release;
}

vsscom_bool vsscom_events_wait(vsscom_events* events, vsscom_u32* fired)
{
    if (events->waiting)
        return 0;
    if (events->history != 0)
    {
        *fired = events->history;
        events->history = 0;
        return 1;
    }
    events->waiting = 1;
    return 0;
}

vsscom_bool vsscom_events_raise(vsscom_events* events, vsscom_u32 happened,
                         vsscom_u32* fired)
{
    const vsscom_u32 wanted = happened & events->mask;
    if (wanted == 0)
        return 0;

    events->history |= wanted;
    if (!events->waiting)
        return 0;

    events->waiting = 0;
    *fired = events->history;
    events->history = 0;
    return 1;
}

// -- Modem lines -----------------------------------------------------------

vsscom_u32 vsscom_loopback_lines(vsscom_bool dtr, vsscom_bool rts)
{
    vsscom_u32 lines = 0;
    if (rts)
        lines |= VSSCOM_MSR_CTS;
    if (dtr)
        lines |= VSSCOM_MSR_DSR | VSSCOM_MSR_DCD;
    return lines;
}

vsscom_u32 vsscom_modem_deltas(vsscom_u32 before, vsscom_u32 after)
{
    const vsscom_u32 changed = before ^ after;
    vsscom_u32 deltas = 0;
    if (changed & VSSCOM_MSR_CTS)
        deltas |= VSSCOM_MSR_DCTS;
    if (changed & VSSCOM_MSR_DSR)
        deltas |= VSSCOM_MSR_DDSR;
    if (changed & VSSCOM_MSR_DCD)
        deltas |= VSSCOM_MSR_DDCD;
    return deltas;
}

vsscom_u32 vsscom_modem_events(vsscom_u32 before, vsscom_u32 after)
{
    const vsscom_u32 changed = before ^ after;
    vsscom_u32 events = 0;
    if (changed & VSSCOM_MSR_CTS)
        events |= VSSCOM_EV_CTS;
    if (changed & VSSCOM_MSR_DSR)
        events |= VSSCOM_EV_DSR;
    if (changed & VSSCOM_MSR_DCD)
        events |= VSSCOM_EV_RLSD;
    return events;
}
