//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The parts of vss::profiling that do not depend on Tracy: reading the
// environment and the command line, describing the state in words. The
// profiler itself is in profiling_tracy.cpp (VSS_TRACY on) or
// profiling_off.cpp (off); CMake picks one.
//

#include <vss/core/profiling.hpp>

#include "platform/environment_platform.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <optional>
#include <string>
#include <system_error>
#include <thread>

namespace vss::profiling
{
namespace
{

[[nodiscard]] std::optional<bool> parse_switch(std::string_view text)
{
    std::string lowered(text);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char c)
                   { return static_cast<char>(std::tolower(c)); });
    if (lowered == "1" || lowered == "on" || lowered == "true" ||
        lowered == "yes")
    {
        return true;
    }
    if (lowered == "0" || lowered == "off" || lowered == "false" ||
        lowered == "no")
    {
        return false;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<unsigned> parse_number(std::string_view text)
{
    unsigned value = 0;
    const auto* end = text.data() + text.size();
    // The end pointer carries the size, so the data() call is bounded.
    // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage)
    const auto [stop, ec] = std::from_chars(text.data(), end, value);
    if (ec != std::errc{} || stop != end)
    {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] std::optional<std::uint16_t> parse_port(std::string_view text)
{
    const auto value = parse_number(text);
    if (!value || *value == 0 || *value > 65535)
    {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(*value);
}

} // namespace

std::string_view to_string(state value) noexcept
{
    switch (value)
    {
    case state::not_compiled:
        return "not compiled";
    case state::dormant:
        return "dormant";
    case state::recording:
        return "recording";
    case state::paused:
        return "paused";
    }
    return "unknown";
}

bool default_on() noexcept
{
    // CMake defines this to 1 for a Debug build with Tracy, and 0 otherwise.
    return VSS_PROFILING_DEFAULT_ON != 0;
}

request request_from_environment(bool default_enable)
{
    request out;
    out.enable = default_enable;
    if (const auto value = platform::get_environment("VSS_PROFILE"))
    {
        if (const auto on = parse_switch(*value))
        {
            out.enable = *on;
        }
    }
    if (const auto value = platform::get_environment("VSS_PROFILE_PORT"))
    {
        if (const auto port = parse_port(*value))
        {
            out.wanted.port = *port;
        }
    }
    if (const auto value = platform::get_environment("VSS_PROFILE_REMOTE"))
    {
        out.wanted.allow_remote = parse_switch(*value).value_or(false);
    }
    if (const auto value = platform::get_environment("VSS_PROFILE_WAIT"))
    {
        if (const auto seconds = parse_number(*value))
        {
            out.wait = std::chrono::seconds(std::min(*seconds, 3600u));
        }
    }
    return out;
}

bool take_argument(std::string_view argument, request& into)
{
    if (argument == "--profile")
    {
        into.enable = true;
        return true;
    }
    if (argument == "--profile-remote")
    {
        into.enable = true;
        into.wanted.allow_remote = true;
        return true;
    }
    if (argument == "--no-profile")
    {
        into.enable = false;
        return true;
    }
    constexpr std::string_view with_wait = "--profile-wait=";
    if (argument.substr(0, with_wait.size()) == with_wait)
    {
        const auto seconds = parse_number(argument.substr(with_wait.size()));
        if (seconds && *seconds <= 3600)
        {
            into.enable = true;
            into.wait = std::chrono::seconds(*seconds);
            return true;
        }
        return false;
    }
    constexpr std::string_view with_port = "--profile=";
    if (argument.substr(0, with_port.size()) == with_port)
    {
        if (const auto port = parse_port(argument.substr(with_port.size())))
        {
            into.enable = true;
            into.wanted.port = *port;
            return true;
        }
    }
    return false;
}

std::string_view usage() noexcept
{
    return "  --profile[=PORT]   Start the Tracy profiler listener on this "
           "computer\n"
           "                     (default port 8086; environment: "
           "VSS_PROFILE=1,\n"
           "                     VSS_PROFILE_PORT=PORT)\n"
           "  --profile-remote   Also accept profilers on other computers "
           "(VSS_PROFILE_REMOTE=1)\n"
           "  --profile-wait=S   Wait up to S seconds for a profiler at "
           "start-up (VSS_PROFILE_WAIT=S)\n"
           "  --no-profile       Do not start the profiler, even where it is "
           "on by default\n";
}

result<status> start(const request& what)
{
    if (!what.enable)
    {
        return current();
    }
    auto attempt = enable(what.wanted);
    if (attempt && what.wait.count() > 0)
    {
        static_cast<void>(wait_for_profiler(what.wait));
        return current();
    }
    return attempt;
}

bool wait_for_profiler(std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!current().profiler_connected)
    {
        if (std::chrono::steady_clock::now() >= deadline)
        {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return true;
}

std::string describe(const status& value)
{
    const std::string where =
        value.remote
            ? "all network interfaces, port " + std::to_string(value.port)
            : "127.0.0.1 port " + std::to_string(value.port) +
                  " (this computer only)";
    switch (value.current)
    {
    case state::not_compiled:
        return "Profiling is not available: this build does not include the "
               "profiler.";
    case state::dormant:
        return "Profiling is off.";
    case state::recording:
        return (value.profiler_connected
                    ? "Profiling is on and a profiler is connected, listening "
                      "on "
                    : "Profiling is on and waiting for a profiler, listening "
                      "on ") +
               where + ".";
    case state::paused:
        return "Profiling is paused; the listener on " + where +
               " stays open until the program exits.";
    }
    return {};
}

} // namespace vss::profiling
