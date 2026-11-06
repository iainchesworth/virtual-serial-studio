//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#pragma once

#include <vss/core/endpoint.hpp>
#include <vss/core/link.hpp>
#include <vss/core/serial_settings.hpp>
#include <vss/core/types.hpp>

#include <optional>
#include <vector>

namespace vss {

/// The baud rate and framing an endpoint is running at, when it is known.
///
/// For a port that a program has opened this is what the program set
/// (`endpoint_info::peer_format`).  For a real serial port and an RFC 2217
/// client it is the configured `serial` settings, which the device really
/// runs at.  A virtual port nobody has configured, a TCP or UDP pipe, a
/// listener and a mock fixture have no line format, and are left out of every
/// comparison rather than guessed at.
std::optional<line_format> running_format(const endpoint_info& info);

/// One member of a link, and the format it runs at.
struct member_format
{
    endpoint_id endpoint = 0;
    line_format format;
};

/// A link whose members do not all run at the same baud rate and framing.  On
/// a real wire the members would not understand each other; Studio carries the
/// bytes across all the same, so the mismatch has to be shown.
struct format_mismatch
{
    link_id link = 0;
    /// Every member whose format is known, at least two of them different.
    std::vector<member_format> members;
};

/// The links in `links` whose members, among those in `endpoints` with a known
/// format, are not all the same.  A link with fewer than two known formats is
/// never reported: not knowing is not a mismatch.  Sources, targets and the
/// members of a bus are all compared.
std::vector<format_mismatch>
find_format_mismatches(const std::vector<endpoint_info>& endpoints,
                       const std::vector<link_info>& links);

} // namespace vss
