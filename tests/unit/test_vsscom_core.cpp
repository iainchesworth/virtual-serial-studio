//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The serial rules of the Windows virtual COM port driver. The driver cannot
// be debugged cheaply, so the rules live in plain C (vsscom_core) and are
// tested here on every platform.
//

// The core is plain C and declares no linkage of its own.
extern "C"
{
#include "vsscom_core.h"
}

#include "test_framework.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>

// The core defines its own types so the kernel build needs no C library; they
// must still be the sizes the rest of the code assumes.
static_assert(sizeof(vsscom_u8) == 1);
static_assert(sizeof(vsscom_u32) == 4);
static_assert(sizeof(vsscom_u64) == 8);

namespace
{

constexpr std::uint32_t max_dword = VSSCOM_MAXDWORD;

vsscom_timeouts timeouts(std::uint32_t interval, std::uint32_t read_mult,
                         std::uint32_t read_const, std::uint32_t write_mult = 0,
                         std::uint32_t write_const = 0)
{
    vsscom_timeouts t{};
    t.read_interval = interval;
    t.read_total_multiplier = read_mult;
    t.read_total_constant = read_const;
    t.write_total_multiplier = write_mult;
    t.write_total_constant = write_const;
    return t;
}

bool valid(vsscom_timeouts t)
{
    return vsscom_timeouts_valid(&t);
}

std::uint32_t write_ms(vsscom_timeouts t, std::uint32_t length)
{
    return vsscom_write_timeout_ms(&t, length);
}

std::string drain(vsscom_ring& ring, std::uint32_t length)
{
    std::string out(length, '\0');
    const auto got = vsscom_ring_read(
        &ring, reinterpret_cast<std::uint8_t*>(out.data()), length);
    out.resize(got);
    return out;
}

std::uint32_t put(vsscom_ring& ring, const std::string& text)
{
    return vsscom_ring_write(&ring,
                             reinterpret_cast<const std::uint8_t*>(text.data()),
                             static_cast<std::uint32_t>(text.size()));
}

} // namespace

TEST_CASE("vsscom ring: bytes come out in the order they went in")
{
    std::array<std::uint8_t, 8> storage{};
    vsscom_ring ring;
    vsscom_ring_init(&ring, storage.data(), 8);

    REQUIRE_EQ(vsscom_ring_count(&ring), 0u);
    REQUIRE_EQ(vsscom_ring_space(&ring), 8u);
    REQUIRE_EQ(drain(ring, 4), std::string());

    REQUIRE_EQ(put(ring, "abc"), 3u);
    REQUIRE_EQ(vsscom_ring_count(&ring), 3u);
    REQUIRE_EQ(drain(ring, 2), std::string("ab"));
    REQUIRE_EQ(drain(ring, 9), std::string("c"));
    REQUIRE_EQ(vsscom_ring_count(&ring), 0u);
}

TEST_CASE("vsscom ring: it wraps around the end of the storage")
{
    std::array<std::uint8_t, 8> storage{};
    vsscom_ring ring;
    vsscom_ring_init(&ring, storage.data(), 8);

    // Move the head to the middle, then write across the end.
    REQUIRE_EQ(put(ring, "123456"), 6u);
    REQUIRE_EQ(drain(ring, 5), std::string("12345"));
    REQUIRE_EQ(put(ring, "abcdefg"), 7u);
    REQUIRE_EQ(vsscom_ring_count(&ring), 8u);
    REQUIRE_EQ(vsscom_ring_space(&ring), 0u);
    REQUIRE_EQ(drain(ring, 8), std::string("6abcdefg"));
}

TEST_CASE("vsscom ring: a full ring takes what fits and no more")
{
    std::array<std::uint8_t, 4> storage{};
    vsscom_ring ring;
    vsscom_ring_init(&ring, storage.data(), 4);

    REQUIRE_EQ(put(ring, "abcdef"), 4u);
    REQUIRE_EQ(put(ring, "x"), 0u);
    REQUIRE_EQ(drain(ring, 8), std::string("abcd"));

    REQUIRE_EQ(put(ring, "xy"), 2u);
    vsscom_ring_clear(&ring);
    REQUIRE_EQ(vsscom_ring_count(&ring), 0u);
    REQUIRE_EQ(vsscom_ring_space(&ring), 4u);
    REQUIRE_EQ(put(ring, "ok"), 2u);
    REQUIRE_EQ(drain(ring, 8), std::string("ok"));
}

TEST_CASE("vsscom timeouts: only all three MAXDWORD read values are refused")
{
    REQUIRE(!valid(timeouts(max_dword, max_dword, max_dword)));
    REQUIRE(valid(timeouts(0, 0, 0)));
    REQUIRE(valid(timeouts(max_dword, 0, 0)));
    REQUIRE(valid(timeouts(max_dword, max_dword, 100)));
    REQUIRE(valid(timeouts(max_dword, max_dword, 0)));
    REQUIRE(valid(timeouts(0, max_dword, max_dword)));
}

TEST_CASE("vsscom read plan: all zeros waits for every byte, with no timer")
{
    const auto t = timeouts(0, 0, 0);
    const auto plan = vsscom_plan_read(&t, 10);
    REQUIRE(!plan.return_now);
    REQUIRE(!plan.return_on_first_byte);
    REQUIRE(!plan.has_total_timer);
    REQUIRE_EQ(plan.interval_ms, 0u);

    REQUIRE(!vsscom_read_satisfied(&plan, 10, 0));
    REQUIRE(!vsscom_read_satisfied(&plan, 10, 9));
    REQUIRE(vsscom_read_satisfied(&plan, 10, 10));
}

TEST_CASE("vsscom read plan: MAXDWORD interval alone returns at once")
{
    const auto t = timeouts(max_dword, 0, 0);
    const auto plan = vsscom_plan_read(&t, 10);
    REQUIRE(plan.return_now);
    REQUIRE(!plan.has_total_timer);
}

TEST_CASE("vsscom read plan: MAXDWORD with a constant waits for one byte")
{
    const auto t = timeouts(max_dword, max_dword, 250);
    const auto plan = vsscom_plan_read(&t, 10);
    REQUIRE(!plan.return_now);
    REQUIRE(plan.return_on_first_byte);
    REQUIRE(plan.has_total_timer);
    REQUIRE_EQ(plan.total_ms, 250u);

    REQUIRE(!vsscom_read_satisfied(&plan, 10, 0));
    REQUIRE(vsscom_read_satisfied(&plan, 10, 1));
    REQUIRE(vsscom_read_satisfied(&plan, 10, 10));

    // Without a constant the same pair is an ordinary (very long) read.
    const auto odd = timeouts(max_dword, max_dword, 0);
    REQUIRE(!vsscom_plan_read(&odd, 10).return_on_first_byte);
}

TEST_CASE("vsscom read plan: the total timeout grows with the request")
{
    const auto t = timeouts(0, 10, 100);
    REQUIRE_EQ(vsscom_plan_read(&t, 1).total_ms, 110u);
    REQUIRE_EQ(vsscom_plan_read(&t, 50).total_ms, 600u);
    REQUIRE(vsscom_plan_read(&t, 50).has_total_timer);

    // A constant alone is a fixed limit.
    const auto fixed = timeouts(0, 0, 500);
    REQUIRE_EQ(vsscom_plan_read(&fixed, 4096).total_ms, 500u);

    // A huge product is a long wait, not a wrapped short one.
    const auto huge = timeouts(0, max_dword - 1, 5);
    const auto plan = vsscom_plan_read(&huge, 1000000);
    REQUIRE(plan.has_total_timer);
    REQUIRE_EQ(plan.total_ms, max_dword - 1u);
}

TEST_CASE("vsscom read plan: the interval timeout applies after the first byte")
{
    const auto t = timeouts(50, 0, 0);
    const auto plan = vsscom_plan_read(&t, 8);
    REQUIRE(!plan.has_total_timer);
    REQUIRE_EQ(plan.interval_ms, 50u);

    const auto both = timeouts(50, 2, 20);
    const auto combined = vsscom_plan_read(&both, 8);
    REQUIRE(combined.has_total_timer);
    REQUIRE_EQ(combined.total_ms, 36u);
    REQUIRE_EQ(combined.interval_ms, 50u);
}

TEST_CASE("vsscom write timeout: none when both values are zero")
{
    REQUIRE_EQ(write_ms(timeouts(0, 0, 0, 0, 0), 100), 0u);
    REQUIRE_EQ(write_ms(timeouts(0, 0, 0, 5, 20), 100), 520u);
    REQUIRE_EQ(write_ms(timeouts(0, 0, 0, 0, 750), 100), 750u);
    REQUIRE_EQ(write_ms(timeouts(0, 0, 0, max_dword, 1), 100000),
               max_dword - 1u);
}

TEST_CASE("vsscom events: a wait completes when a masked event happens")
{
    vsscom_events events;
    vsscom_events_init(&events);
    std::uint32_t fired = 0;

    REQUIRE(!vsscom_events_set_mask(&events, VSSCOM_EV_RXCHAR));
    REQUIRE(!vsscom_events_wait(&events, &fired));
    REQUIRE(events.waiting);

    // An event outside the mask neither completes the wait nor is kept.
    REQUIRE(!vsscom_events_raise(&events, VSSCOM_EV_TXEMPTY, &fired));
    REQUIRE(events.waiting);
    REQUIRE_EQ(events.history, 0u);

    REQUIRE(
        vsscom_events_raise(&events, VSSCOM_EV_RXCHAR | VSSCOM_EV_CTS, &fired));
    REQUIRE_EQ(fired, VSSCOM_EV_RXCHAR);
    REQUIRE(!events.waiting);
    REQUIRE_EQ(events.history, 0u);
}

TEST_CASE("vsscom events: events that happen before the wait are kept")
{
    vsscom_events events;
    vsscom_events_init(&events);
    std::uint32_t fired = 0;

    vsscom_events_set_mask(&events, VSSCOM_EV_RXCHAR | VSSCOM_EV_TXEMPTY);
    REQUIRE(!vsscom_events_raise(&events, VSSCOM_EV_RXCHAR, &fired));
    REQUIRE(!vsscom_events_raise(&events, VSSCOM_EV_TXEMPTY, &fired));

    // The wait finds both and empties the history.
    REQUIRE(vsscom_events_wait(&events, &fired));
    REQUIRE_EQ(fired, VSSCOM_EV_RXCHAR | VSSCOM_EV_TXEMPTY);
    REQUIRE(!events.waiting);

    // A second wait now has to wait.
    REQUIRE(!vsscom_events_wait(&events, &fired));
    REQUIRE(events.waiting);
}

TEST_CASE("vsscom events: setting the mask releases a wait and drops history")
{
    vsscom_events events;
    vsscom_events_init(&events);
    std::uint32_t fired = 0;

    vsscom_events_set_mask(&events, VSSCOM_EV_RXCHAR);
    vsscom_events_raise(&events, VSSCOM_EV_RXCHAR, &fired);
    REQUIRE_EQ(events.history, VSSCOM_EV_RXCHAR);

    // A new mask starts from nothing, even with the same value.
    REQUIRE(!vsscom_events_set_mask(&events, VSSCOM_EV_RXCHAR));
    REQUIRE_EQ(events.history, 0u);

    REQUIRE(!vsscom_events_wait(&events, &fired));
    REQUIRE(vsscom_events_set_mask(&events, 0));
    REQUIRE(!events.waiting);

    // With no mask nothing is recorded.
    REQUIRE(!vsscom_events_raise(&events, VSSCOM_EV_RXCHAR, &fired));
    REQUIRE_EQ(events.history, 0u);
}

TEST_CASE("vsscom events: a second wait while one is pending is not started")
{
    vsscom_events events;
    vsscom_events_init(&events);
    std::uint32_t fired = 0;
    vsscom_events_set_mask(&events, VSSCOM_EV_BREAK);

    REQUIRE(!vsscom_events_wait(&events, &fired));
    REQUIRE(events.waiting);
    // The caller reads `waiting` and refuses the second request; the call
    // itself changes nothing.
    REQUIRE(!vsscom_events_wait(&events, &fired));
    REQUIRE(events.waiting);
    REQUIRE_EQ(fired, 0u);
}

TEST_CASE("vsscom modem lines: a loopback port wires RTS to CTS and DTR to DSR")
{
    REQUIRE_EQ(vsscom_loopback_lines(false, false), 0u);
    REQUIRE_EQ(vsscom_loopback_lines(false, true), VSSCOM_MSR_CTS);
    REQUIRE_EQ(vsscom_loopback_lines(true, false),
               VSSCOM_MSR_DSR | VSSCOM_MSR_DCD);
    REQUIRE_EQ(vsscom_loopback_lines(true, true),
               VSSCOM_MSR_CTS | VSSCOM_MSR_DSR | VSSCOM_MSR_DCD);
}

TEST_CASE("vsscom modem lines: a change gives deltas and events")
{
    const std::uint32_t none = 0;
    const std::uint32_t cts = VSSCOM_MSR_CTS;
    const std::uint32_t all = vsscom_loopback_lines(true, true);

    REQUIRE_EQ(vsscom_modem_deltas(none, none), 0u);
    REQUIRE_EQ(vsscom_modem_events(none, none), 0u);

    REQUIRE_EQ(vsscom_modem_deltas(none, cts), VSSCOM_MSR_DCTS);
    REQUIRE_EQ(vsscom_modem_events(none, cts), VSSCOM_EV_CTS);
    REQUIRE_EQ(vsscom_modem_deltas(cts, none), VSSCOM_MSR_DCTS);

    REQUIRE_EQ(vsscom_modem_deltas(none, all),
               VSSCOM_MSR_DCTS | VSSCOM_MSR_DDSR | VSSCOM_MSR_DDCD);
    REQUIRE_EQ(vsscom_modem_events(none, all),
               VSSCOM_EV_CTS | VSSCOM_EV_DSR | VSSCOM_EV_RLSD);

    // The ring indicator is not wired in a loopback, so it never raises.
    REQUIRE_EQ(vsscom_modem_events(none, VSSCOM_MSR_RI), 0u);
}
