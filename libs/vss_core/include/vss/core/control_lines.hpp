//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_CORE_CONTROL_LINES_HPP
#define VSS_CORE_CONTROL_LINES_HPP

#include <vss/core/error.hpp>

#include <string>
#include <utility>

namespace vss {

/// The RS-232 control lines modelled by the studio.
///
/// DTR, RTS and BREAK are outputs: the attached application drives them.
/// CTS, DSR, DCD and RI are inputs: the peer drives them.  A virtual
/// null-modem link crosses the two sets, which is what makes control line
/// handling testable without hardware.
enum class control_line : unsigned
{
    dtr = 0,
    rts = 1,
    cts = 2,
    dsr = 3,
    dcd = 4,
    ri = 5,
    break_signal = 6,
    count = 7
};

constexpr unsigned line_mask(control_line line) noexcept
{
    return 1u << std::to_underlying(line);
}

/// Bit set describing the state of every line at one instant.
struct control_lines
{
    unsigned bits = 0;

    constexpr bool test(control_line line) const noexcept
    {
        return (bits & line_mask(line)) != 0;
    }

    constexpr void set(control_line line, bool state) noexcept
    {
        if (state)
            bits |= line_mask(line);
        else
            bits &= ~line_mask(line);
    }

    constexpr bool dtr() const noexcept { return test(control_line::dtr); }
    constexpr bool rts() const noexcept { return test(control_line::rts); }
    constexpr bool cts() const noexcept { return test(control_line::cts); }
    constexpr bool dsr() const noexcept { return test(control_line::dsr); }
    constexpr bool dcd() const noexcept { return test(control_line::dcd); }
    constexpr bool ri() const noexcept { return test(control_line::ri); }
    constexpr bool break_active() const noexcept
    {
        return test(control_line::break_signal);
    }

    /// Lines the application drives.
    constexpr unsigned outputs() const noexcept
    {
        return bits
            & (line_mask(control_line::dtr) | line_mask(control_line::rts)
               | line_mask(control_line::break_signal));
    }

    /// Lines the peer drives.
    constexpr unsigned inputs() const noexcept
    {
        return bits & input_bits();
    }

    static constexpr unsigned input_bits() noexcept
    {
        return line_mask(control_line::cts) | line_mask(control_line::dsr)
            | line_mask(control_line::dcd) | line_mask(control_line::ri);
    }

    static constexpr control_lines from_bits(unsigned value) noexcept
    {
        return control_lines{value};
    }

    friend constexpr bool operator==(control_lines lhs, control_lines rhs) noexcept
    {
        return lhs.bits == rhs.bits;
    }
    friend constexpr bool operator!=(control_lines lhs, control_lines rhs) noexcept
    {
        return lhs.bits != rhs.bits;
    }
};

/// `"DTR RTS | CTS"` style rendering, outputs first.
std::string to_string(control_lines lines);

/// Parses `"dtr,rts"`, `"DTR|CTS"` or `"0x41"`.
result<control_lines> parse_control_lines(std::string_view text);

/// A straight-through null-modem cable mapping: the peer sees this endpoint's
/// DTR as DSR and its RTS as CTS, and DCD follows DTR.
constexpr control_lines null_modem_peer(control_lines local) noexcept
{
    control_lines peer;
    peer.set(control_line::dsr, local.dtr());
    peer.set(control_line::cts, local.rts());
    peer.set(control_line::dcd, local.dtr());
    return peer;
}

/// Merges the peer driven `inputs` into `target`, replacing every input bit
/// while leaving the outputs alone.
constexpr control_lines with_peer_inputs(control_lines target,
                                         control_lines inputs) noexcept
{
    return control_lines::from_bits((target.bits & ~control_lines::input_bits())
                                    | inputs.inputs());
}

} // namespace vss

#endif // VSS_CORE_CONTROL_LINES_HPP
