//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <vss/core/types.hpp>
#include <vss/core/event.hpp>

namespace vss {

const char* to_string(endpoint_kind value) noexcept
{
    switch (value)
    {
    case endpoint_kind::virtual_port: return "virtual";
    case endpoint_kind::physical: return "physical";
    case endpoint_kind::tcp: return "tcp";
    case endpoint_kind::udp: return "udp";
    case endpoint_kind::rfc2217: return "rfc2217";
    case endpoint_kind::mock: return "mock";
    }
    return "unknown";
}

const char* to_string(endpoint_state value) noexcept
{
    switch (value)
    {
    case endpoint_state::created: return "created";
    case endpoint_state::opening: return "opening";
    case endpoint_state::open: return "open";
    case endpoint_state::closed: return "closed";
    case endpoint_state::failed: return "failed";
    }
    return "unknown";
}

const char* to_string(link_mode value) noexcept
{
    switch (value)
    {
    case link_mode::duplex: return "duplex";
    case link_mode::one_way: return "one-way";
    }
    return "unknown";
}

const char* to_string(merge_policy value) noexcept
{
    switch (value)
    {
    case merge_policy::interleave: return "interleave";
    case merge_policy::owner: return "owner";
    }
    return "unknown";
}

const char* to_string(data_direction value) noexcept
{
    switch (value)
    {
    case data_direction::rx: return "rx";
    case data_direction::tx: return "tx";
    }
    return "unknown";
}

const char* to_string(event_kind value) noexcept
{
    switch (value)
    {
    case event_kind::started: return "started";
    case event_kind::stopped: return "stopped";
    case event_kind::data: return "data";
    case event_kind::lines: return "lines";
    case event_kind::error: return "error";
    case event_kind::stats: return "stats";
    case event_kind::log_message: return "log";
    }
    return "unknown";
}

} // namespace vss
