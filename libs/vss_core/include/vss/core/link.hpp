//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_CORE_LINK_HPP
#define VSS_CORE_LINK_HPP

#include <vss/core/stats.hpp>
#include <vss/core/types.hpp>

#include <vector>

namespace vss {

/// Tuning for one link.
struct link_options
{
    /// Cross DTR→DSR and RTS→CTS between the two ends, as a null-modem
    /// cable does.  Disable for a one-way tap that must not disturb the
    /// device.
    bool emulate_modem_lines = true;

    /// Arbitration between concurrent publishers into the same target.
    merge_policy policy = merge_policy::interleave;
};

/// Snapshot of a link for the GUI and the CLI.
struct link_info
{
    link_id id = 0;
    link_mode mode = link_mode::duplex;
    link_options options;
    std::vector<endpoint_id> sources;
    std::vector<endpoint_id> targets;
    endpoint_id owner = 0;
    link_stats stats;
};

} // namespace vss

#endif // VSS_CORE_LINK_HPP
