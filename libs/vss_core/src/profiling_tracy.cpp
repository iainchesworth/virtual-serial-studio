//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Tracy side of vss::profiling. Compiled when VSS_TRACY is on.
//

#include <vss/core/profiling.hpp>

#include "platform/environment_platform.hpp"

#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/socket_base.hpp>

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>

namespace vss::profiling
{
namespace
{

std::mutex control_mutex;
std::atomic<bool> running{false};
std::atomic<bool> shut_down{false};
std::atomic<std::uint16_t> listen_port{0};
std::atomic<bool> listen_remote{false};

thread_local const char* requested_thread_name = nullptr;

// How many ports above the default are tried when no port was asked for. This
// is the same window Tracy searches by itself.
constexpr std::uint16_t search_window = 20;

/// Whether a listener could bind `port` now. Tracy binds the same address, so
/// this tells the caller the port is taken before the profiler thread starts
/// and silently waits for it.
[[nodiscard]] bool port_is_free(std::uint16_t port, bool remote)
{
    namespace ip = boost::asio::ip;
    boost::asio::io_context io;
    ip::tcp::acceptor acceptor(io);
    boost::system::error_code ec;
    const ip::tcp::endpoint endpoint =
        remote ? ip::tcp::endpoint(ip::tcp::v4(), port)
               : ip::tcp::endpoint(ip::address_v4::loopback(), port);
    acceptor.open(endpoint.protocol(), ec);
    if (ec)
    {
        return false;
    }
    if (platform::probe_reuses_address())
    {
        // Tracy sets SO_REUSEADDR here, so a port in TIME_WAIT is usable.
        acceptor.set_option(boost::asio::socket_base::reuse_address(true), ec);
    }
    acceptor.bind(endpoint, ec);
    return !ec;
}

} // namespace

namespace detail
{

void name_current_thread() noexcept
{
    named_epoch = epoch.load(std::memory_order_relaxed);
    if (requested_thread_name != nullptr)
    {
        tracy::SetThreadName(requested_thread_name);
    }
}

} // namespace detail

void name_thread(const char* name) noexcept
{
    requested_thread_name = name;
    detail::named_epoch = 0;
    if (detail::armed.load(std::memory_order_relaxed))
    {
        const detail::section guard;
        if (guard.active())
        {
            detail::name_current_thread();
        }
    }
}

status current() noexcept
{
    status out;
    if (!running.load(std::memory_order_acquire))
    {
        out.current = state::dormant;
        return out;
    }
    const bool armed = detail::armed.load(std::memory_order_relaxed);
    out.current = armed ? state::recording : state::paused;
    out.port = listen_port.load(std::memory_order_relaxed);
    out.remote = listen_remote.load(std::memory_order_relaxed);
    if (armed)
    {
        const detail::section guard;
        if (guard.active())
        {
            out.profiler_connected = TracyIsConnected;
        }
    }
    return out;
}

result<status> enable(const options& wanted)
{
    const std::scoped_lock lock(control_mutex);
    if (shut_down.load(std::memory_order_acquire))
    {
        return make_error_code(errc::unsupported);
    }

    if (!running.load(std::memory_order_acquire))
    {
        std::uint16_t port = wanted.port;
        if (port != 0)
        {
            if (!port_is_free(port, wanted.allow_remote))
            {
                return make_error_code(errc::device_busy);
            }
        }
        else
        {
            for (std::uint16_t offset = 0; offset < search_window; ++offset)
            {
                const auto candidate =
                    static_cast<std::uint16_t>(default_port + offset);
                if (port_is_free(candidate, wanted.allow_remote))
                {
                    port = candidate;
                    break;
                }
            }
            if (port == 0)
            {
                return make_error_code(errc::port_exhausted);
            }
        }

        // Tracy reads these when its thread opens the socket. Without
        // ONLY_LOCALHOST it binds every interface; with it, and IPv4 only, it
        // binds 127.0.0.1 so a profiler that connects by address finds it.
        platform::set_environment("TRACY_PORT", std::to_string(port));
        platform::set_environment("TRACY_ONLY_LOCALHOST",
                                  wanted.allow_remote ? "0" : "1");
        platform::set_environment("TRACY_ONLY_IPV4",
                                  wanted.allow_remote ? "0" : "1");

        tracy::StartupProfiler();
        listen_port.store(port, std::memory_order_relaxed);
        listen_remote.store(wanted.allow_remote, std::memory_order_relaxed);
        running.store(true, std::memory_order_release);
    }
    else if (wanted.allow_remote && !listen_remote.load())
    {
        // The socket is already bound to this computer only.
        return make_error_code(errc::invalid_argument);
    }

    detail::epoch.fetch_add(1, std::memory_order_relaxed);
    detail::armed.store(true, std::memory_order_seq_cst);
    VSS_PROFILE_MESSAGE("profiling.enabled");
    return current();
}

void disable() noexcept
{
    if (!running.load(std::memory_order_acquire))
    {
        return;
    }
    VSS_PROFILE_MESSAGE("profiling.disabled");
    detail::armed.store(false, std::memory_order_seq_cst);
}

void shutdown() noexcept
{
    const std::scoped_lock lock(control_mutex);
    if (!running.load(std::memory_order_acquire) ||
        shut_down.load(std::memory_order_acquire))
    {
        return;
    }

    detail::armed.store(false, std::memory_order_seq_cst);

    // A thread that was inside a zone when recording stopped finishes it
    // first. If one never does, leave Tracy running: a leaked profiler is
    // better than a freed one that a thread still writes to.
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    for (;;)
    {
        std::int64_t inside = 0;
        for (const auto& shard : detail::shards)
        {
            inside += shard.inside.load(std::memory_order_acquire);
        }
        if (inside == 0)
        {
            break;
        }
        if (std::chrono::steady_clock::now() >= deadline)
        {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    tracy::ShutdownProfiler();
    running.store(false, std::memory_order_release);
    shut_down.store(true, std::memory_order_release);
}

} // namespace vss::profiling
