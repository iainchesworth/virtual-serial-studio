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
#include <cstdint>
#include <vector>

namespace vss {

/// The most members one bus takes.  A bus of `n` members holds `n * (n - 1)`
/// routes, so the limit keeps a saved file from asking for millions of them.
inline constexpr std::size_t max_bus_members = 64;

/// The longest `delay_ms` a link takes, one minute.  A saved file cannot ask
/// for a delay that holds bytes for hours.
inline constexpr std::uint32_t max_link_delay_ms = 60'000;

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

    /// How long each chunk is held between its source and its target, in
    /// milliseconds, to rehearse a slow wire or a network hop.  Zero (the
    /// default) forwards at once, as before.  It applies to every route of
    /// the link, so on a bus each receiving member hears a byte this long
    /// after it was sent.
    std::uint32_t delay_ms = 0;

    /// Each chunk's delay varies by up to this much either side of
    /// `delay_ms`, drawn uniformly.  It must not exceed `delay_ms`, so a delay
    /// is never negative.  Bytes of one route stay in order whatever the
    /// draw: a chunk never overtakes the one before it.
    std::uint32_t jitter_ms = 0;

    bool has_latency() const noexcept
    {
        return delay_ms != 0 || jitter_ms != 0;
    }
};

/// True when the link's delay and jitter are within the limits.
inline constexpr bool valid_latency(const link_options& options) noexcept
{
    return options.delay_ms <= max_link_delay_ms &&
           options.jitter_ms <= options.delay_ms;
}

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
