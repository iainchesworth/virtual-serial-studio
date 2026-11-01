//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_CORE_STATS_HPP
#define VSS_CORE_STATS_HPP

#include <cstdint>
#include <chrono>

namespace vss {

using clock_type = std::chrono::steady_clock;
using time_point = clock_type::time_point;

/// Traffic counters for one endpoint.
struct endpoint_stats
{
    std::uint64_t bytes_rx = 0;
    std::uint64_t bytes_tx = 0;
    std::uint64_t read_ops = 0;
    std::uint64_t write_ops = 0;
    std::uint64_t errors = 0;
    std::uint64_t dropped = 0;
    time_point started{};
    time_point last_activity{};

    std::uint64_t total_bytes() const noexcept { return bytes_rx + bytes_tx; }
};

/// Traffic counters for one link.
struct link_stats
{
    std::uint64_t bytes = 0;
    std::uint64_t messages = 0;
    std::uint64_t dropped = 0;
};

} // namespace vss

#endif // VSS_CORE_STATS_HPP
