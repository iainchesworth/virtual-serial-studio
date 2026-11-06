//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "../peer_open_platform.hpp"

#include <vss/ports/vcom_driver.hpp>

#include <chrono>
#include <mutex>

namespace vss::detail
{

namespace
{

/// Whether the Studio driver is installed, looked at no more than every few
/// seconds: the answer needs a walk of the device list, and the GUI asks every
/// second.
bool driver_installed() noexcept
{
    using clock = std::chrono::steady_clock;
    static std::mutex mutex;
    static clock::time_point checked{};
    static bool installed = false;

    try
    {
        const std::lock_guard lock(mutex);
        const auto now = clock::now();
        if (checked == clock::time_point{} ||
            now - checked > std::chrono::seconds(5))
        {
            installed = driver_available();
            checked = now;
        }
        return installed;
    }
    catch (...)
    {
        return false;
    }
}

} // namespace

// Windows cannot say which process has an arbitrary COM port open, but the
// Studio driver says so about its own ports: it tells the studio when a program
// opens a port and which process it is. Every other port reads as unknown.
bool platform_peer_open_supported() noexcept
{
    return driver_installed();
}

std::vector<peer_open_state>
platform_peer_open(const std::vector<std::string>& paths)
{
    std::vector<peer_open_state> states;
    states.reserve(paths.size());
    for (const auto& path : paths)
    {
        const auto peer = driver_peer(path);
        if (!peer)
            states.push_back(peer_open_state::unknown);
        else
            states.push_back(peer->open ? peer_open_state::open
                                        : peer_open_state::not_open);
    }
    return states;
}

} // namespace vss::detail
