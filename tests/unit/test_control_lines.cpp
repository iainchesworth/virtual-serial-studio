//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <vss/core/control_lines.hpp>

#include "test_framework.hpp"

using namespace vss;

TEST_CASE("lines: bits, outputs and inputs")
{
    control_lines lines;
    REQUIRE_EQ(lines.bits, 0u);
    REQUIRE_EQ(lines.outputs(), 0u);
    REQUIRE_EQ(lines.inputs(), 0u);

    lines.set(control_line::dtr, true);
    lines.set(control_line::rts, true);
    lines.set(control_line::cts, true);
    lines.set(control_line::break_signal, true);

    REQUIRE(lines.dtr());
    REQUIRE(lines.rts());
    REQUIRE(lines.cts());
    REQUIRE(!lines.dsr());
    REQUIRE(lines.break_active());

    REQUIRE_EQ(lines.outputs(),
               line_mask(control_line::dtr) | line_mask(control_line::rts)
                   | line_mask(control_line::break_signal));
    REQUIRE_EQ(lines.inputs(), line_mask(control_line::cts));

    lines.set(control_line::dtr, false);
    REQUIRE(!lines.dtr());
}

TEST_CASE("lines: bit masks are distinct")
{
    REQUIRE_EQ(line_mask(control_line::dtr), 1u << 0);
    REQUIRE_EQ(line_mask(control_line::rts), 1u << 1);
    REQUIRE_EQ(line_mask(control_line::cts), 1u << 2);
    REQUIRE_EQ(line_mask(control_line::dsr), 1u << 3);
    REQUIRE_EQ(line_mask(control_line::dcd), 1u << 4);
    REQUIRE_EQ(line_mask(control_line::ri), 1u << 5);
    REQUIRE_EQ(line_mask(control_line::break_signal), 1u << 6);
}

TEST_CASE("lines: to_string renders outputs then inputs")
{
    REQUIRE_EQ(to_string(control_lines{}), std::string("none"));

    control_lines lines;
    lines.set(control_line::dtr, true);
    REQUIRE_EQ(to_string(lines), std::string("DTR"));

    lines.set(control_line::rts, true);
    REQUIRE_EQ(to_string(lines), std::string("DTR RTS"));

    lines.set(control_line::cts, true);
    REQUIRE_EQ(to_string(lines), std::string("DTR RTS | CTS"));

    lines.set(control_line::break_signal, true);
    REQUIRE_EQ(to_string(lines), std::string("DTR RTS BREAK | CTS"));

    control_lines inputs_only;
    inputs_only.set(control_line::dsr, true);
    inputs_only.set(control_line::ri, true);
    REQUIRE_EQ(to_string(inputs_only), std::string("| DSR RI"));
}

TEST_CASE("lines: parse roundtrip")
{
    for (const char* text : {"dtr", "DTR,RTS", "dtr|cts", "rts cts dsr",
                             "dtr+rts+ri", "break", "0x41"})
    {
        const auto parsed = parse_control_lines(text);
        REQUIRE(parsed);
        const auto rendered = to_string(*parsed);
        const auto again = parse_control_lines(rendered);
        REQUIRE(again);
        REQUIRE_EQ(again->bits, parsed->bits);
    }
}

TEST_CASE("lines: parse edge cases")
{
    REQUIRE_EQ(parse_control_lines("").value().bits, 0u);
    REQUIRE_EQ(parse_control_lines("none").value().bits, 0u);
    REQUIRE_EQ(parse_control_lines("0x7f").value().bits, 0x7fu);

    REQUIRE(!parse_control_lines("banana"));
    REQUIRE(!parse_control_lines("0x80"));
    REQUIRE(!parse_control_lines("0xzz"));
    REQUIRE(!parse_control_lines("dtr,banana"));
}

TEST_CASE("lines: parse tolerates trailing separators")
{
    const auto parsed = parse_control_lines("dtr,");
    REQUIRE(parsed);
    REQUIRE(parsed->dtr());
    REQUIRE_EQ(parsed->bits, line_mask(control_line::dtr));
}

TEST_CASE("lines: null modem crosses dtr/dsr and rts/cts")
{
    control_lines local;
    local.set(control_line::dtr, true);
    local.set(control_line::rts, true);
    local.set(control_line::cts, true); // local input must not leak

    const control_lines peer = null_modem_peer(local);
    REQUIRE(peer.dsr());
    REQUIRE(peer.cts());
    REQUIRE(peer.dcd());
    REQUIRE(!peer.dtr());
    REQUIRE(!peer.rts());

    const control_lines off = null_modem_peer(control_lines{});
    REQUIRE_EQ(off.bits, 0u);
}

TEST_CASE("lines: with_peer_inputs replaces the peer driven inputs")
{
    control_lines target;
    target.set(control_line::dtr, true);
    target.set(control_line::ri, true);

    control_lines inputs;
    inputs.set(control_line::cts, true);
    inputs.set(control_line::dsr, true);

    const control_lines merged = with_peer_inputs(target, inputs);
    REQUIRE(merged.dtr());
    REQUIRE(merged.cts());
    REQUIRE(merged.dsr());
    // The peer drives the whole input set, so RI is cleared.
    REQUIRE(!merged.ri());
    REQUIRE(!merged.dcd());

    const control_lines cleared = with_peer_inputs(merged, control_lines{});
    REQUIRE(!cleared.cts());
    REQUIRE(!cleared.dsr());
    REQUIRE(cleared.dtr());
}
