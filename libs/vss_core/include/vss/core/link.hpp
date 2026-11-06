//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_CORE_LINK_HPP
#define VSS_CORE_LINK_HPP

#include <vss/core/control_lines.hpp>
#include <vss/core/stats.hpp>
#include <vss/core/types.hpp>

#include <cstddef>
#include <vector>

namespace vss {

/// The most members one bus takes.  A bus of `n` members holds `n * (n - 1)`
/// routes, so the limit keeps a saved file from asking for millions of them.
inline constexpr std::size_t max_bus_members = 64;

/// Tuning for one link.
struct link_options
{
    /// Cross DTR→DSR and RTS→CTS between the two ends, as a null-modem
    /// cable does.  Disable for a one-way tap that must not disturb the
    /// device.  A bus has no modem lines (a multi-drop wire pair carries
    /// none), so #hub::bus stores `false` here whatever the caller passed.
    bool emulate_modem_lines = true;

    /// Which control line drives which across the link, when
    /// `emulate_modem_lines` is on.  The default is the null-modem cable.  It
    /// lives in the running hub only: the saved topology does not store it.
    modem_wiring wiring;

    /// Arbitration between concurrent publishers into the same target.  A
    /// bus supports `interleave` only.
    merge_policy policy = merge_policy::interleave;
};

/// Snapshot of a link for the GUI and the CLI.
struct link_info
{
    link_id id = 0;
    link_mode mode = link_mode::duplex;
    link_options options;
    /// For a bus: its members, and `targets` is empty.
    std::vector<endpoint_id> sources;
    std::vector<endpoint_id> targets;
    endpoint_id owner = 0;
    link_stats stats;
};

} // namespace vss

#endif // VSS_CORE_LINK_HPP
