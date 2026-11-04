//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "vsscom_core.h"

#include <string.h>

// -- The byte ring ---------------------------------------------------------

void vsscom_ring_init(vsscom_ring* ring, uint8_t* storage, uint32_t size)
{
    ring->data = storage;
    ring->size = size;
    ring->head = 0;
    ring->count = 0;
}

uint32_t vsscom_ring_count(const vsscom_ring* ring)
{
    return ring->count;
}

uint32_t vsscom_ring_space(const vsscom_ring* ring)
{
    return ring->size - ring->count;
}

uint32_t vsscom_ring_write(vsscom_ring* ring, const uint8_t* bytes,
                           uint32_t length)
{
    const uint32_t room = vsscom_ring_space(ring);
    const uint32_t take = length < room ? length : room;
    if (take == 0)
        return 0;

    // The first free slot, then at most two runs: up to the end, then the
    // start.
    const uint32_t tail = (ring->head + ring->count) % ring->size;
    const uint32_t first = take < ring->size - tail ? take : ring->size - tail;
    memcpy(ring->data + tail, bytes, first);
    if (take > first)
        memcpy(ring->data, bytes + first, take - first);
    ring->count += take;
    return take;
}

uint32_t vsscom_ring_read(vsscom_ring* ring, uint8_t* bytes, uint32_t length)
{
    const uint32_t take = length < ring->count ? length : ring->count;
    if (take == 0)
        return 0;

    const uint32_t first =
        take < ring->size - ring->head ? take : ring->size - ring->head;
    memcpy(bytes, ring->data + ring->head, first);
    if (take > first)
        memcpy(bytes + first, ring->data, take - first);
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

bool vsscom_timeouts_valid(const vsscom_timeouts* timeouts)
{
    return !(timeouts->read_interval == VSSCOM_MAXDWORD &&
             timeouts->read_total_multiplier == VSSCOM_MAXDWORD &&
             timeouts->read_total_constant == VSSCOM_MAXDWORD);
}

// multiplier * length + constant, held at the largest value a timer can take
// below MAXDWORD so that a huge product still counts as "a very long time"
// and never wraps to a short one.
static uint32_t total_ms(uint32_t multiplier, uint32_t constant,
                         uint32_t length)
{
    const uint64_t total = (uint64_t)multiplier * length + constant;
    return total >= VSSCOM_MAXDWORD ? VSSCOM_MAXDWORD - 1u : (uint32_t)total;
}

vsscom_read_plan vsscom_plan_read(const vsscom_timeouts* timeouts,
                                  uint32_t requested)
{
    vsscom_read_plan plan;
    memset(&plan, 0, sizeof plan);

    // MAXDWORD interval with no total timeout: return what is there, now.
    if (timeouts->read_interval == VSSCOM_MAXDWORD &&
        timeouts->read_total_multiplier == 0 &&
        timeouts->read_total_constant == 0)
    {
        plan.return_now = true;
        return plan;
    }

    // MAXDWORD interval and multiplier with a constant: wait up to the constant
    // for the first byte, then return with what is there.
    if (timeouts->read_interval == VSSCOM_MAXDWORD &&
        timeouts->read_total_multiplier == VSSCOM_MAXDWORD &&
        timeouts->read_total_constant > 0 &&
        timeouts->read_total_constant < VSSCOM_MAXDWORD)
    {
        plan.return_on_first_byte = true;
        plan.has_total_timer = true;
        plan.total_ms = timeouts->read_total_constant;
        return plan;
    }

    if (timeouts->read_total_multiplier != 0 ||
        timeouts->read_total_constant != 0)
    {
        plan.has_total_timer = true;
        plan.total_ms = total_ms(timeouts->read_total_multiplier,
                                 timeouts->read_total_constant, requested);
    }
    plan.interval_ms = timeouts->read_interval;
    return plan;
}

bool vsscom_read_satisfied(const vsscom_read_plan* plan, uint32_t requested,
                           uint32_t received)
{
    if (received >= requested)
        return true;
    return plan->return_on_first_byte && received > 0;
}

uint32_t vsscom_write_timeout_ms(const vsscom_timeouts* timeouts,
                                 uint32_t length)
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
    events->waiting = false;
}

bool vsscom_events_set_mask(vsscom_events* events, uint32_t mask)
{
    const bool release = events->waiting;
    events->mask = mask;
    events->history = 0;
    events->waiting = false;
    return release;
}

bool vsscom_events_wait(vsscom_events* events, uint32_t* fired)
{
    if (events->waiting)
        return false;
    if (events->history != 0)
    {
        *fired = events->history;
        events->history = 0;
        return true;
    }
    events->waiting = true;
    return false;
}

bool vsscom_events_raise(vsscom_events* events, uint32_t happened,
                         uint32_t* fired)
{
    const uint32_t wanted = happened & events->mask;
    if (wanted == 0)
        return false;

    events->history |= wanted;
    if (!events->waiting)
        return false;

    events->waiting = false;
    *fired = events->history;
    events->history = 0;
    return true;
}

// -- Modem lines -----------------------------------------------------------

uint32_t vsscom_loopback_lines(bool dtr, bool rts)
{
    uint32_t lines = 0;
    if (rts)
        lines |= VSSCOM_MSR_CTS;
    if (dtr)
        lines |= VSSCOM_MSR_DSR | VSSCOM_MSR_DCD;
    return lines;
}

uint32_t vsscom_modem_deltas(uint32_t before, uint32_t after)
{
    const uint32_t changed = before ^ after;
    uint32_t deltas = 0;
    if (changed & VSSCOM_MSR_CTS)
        deltas |= VSSCOM_MSR_DCTS;
    if (changed & VSSCOM_MSR_DSR)
        deltas |= VSSCOM_MSR_DDSR;
    if (changed & VSSCOM_MSR_DCD)
        deltas |= VSSCOM_MSR_DDCD;
    return deltas;
}

uint32_t vsscom_modem_events(uint32_t before, uint32_t after)
{
    const uint32_t changed = before ^ after;
    uint32_t events = 0;
    if (changed & VSSCOM_MSR_CTS)
        events |= VSSCOM_EV_CTS;
    if (changed & VSSCOM_MSR_DSR)
        events |= VSSCOM_EV_DSR;
    if (changed & VSSCOM_MSR_DCD)
        events |= VSSCOM_EV_RLSD;
    return events;
}
