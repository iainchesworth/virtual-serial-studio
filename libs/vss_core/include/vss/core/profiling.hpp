//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#pragma once

#include <vss/core/error.hpp>
#include <vss/core/profile.hpp>

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

/// Run-time control of the Tracy instrumentation.
///
/// A build with `VSS_TRACY` carries the instrumentation, but Tracy itself is
/// built with a manual lifetime: nothing starts until `enable()` is called. A
/// dormant process has no profiler thread, no socket and no buffers, and each
/// zone costs one relaxed load of a flag.
///
/// What `enable()` does:
///
///  * starts Tracy's profiler thread and opens its listening socket,
///  * binds the socket to this computer only (127.0.0.1) unless the caller
///    asked for `allow_remote`,
///  * never broadcasts: the client is built with `TRACY_NO_BROADCAST`, so a
///    profiler has to be pointed at the address,
///  * sends nothing until a person connects a profiler.
///
/// `disable()` stops recording at once and returns every zone to the dormant
/// cost. Tracy cannot tear its listener down while other threads have used it,
/// so the socket stays bound until the process ends (or `shutdown()` at exit);
/// `current().state` says `paused` meanwhile, and the port stays reported.
/// `enable()` resumes recording on the same socket.
namespace vss::profiling
{

/// Where the profiler's listener starts when nothing else is asked for.
inline constexpr std::uint16_t default_port = 8086;

enum class state
{
    not_compiled, ///< This build has no instrumentation (`VSS_TRACY` off).
    dormant,      ///< Compiled in; never started in this process.
    recording,    ///< Started and recording; a profiler may connect.
    paused,       ///< Started, recording switched off.
};

struct options
{
    /// Port to listen on. 0 means "the first free port from `default_port`".
    std::uint16_t port{0};

    /// Listen on every network interface, not only this computer. This is a
    /// deliberate choice a person makes; nothing selects it by itself. It can
    /// only be chosen for the first start of a process.
    bool allow_remote{false};
};

struct status
{
    state current{state::dormant};
    std::uint16_t port{0};          ///< 0 until the listener exists.
    bool remote{false};             ///< Listening beyond this computer.
    bool profiler_connected{false}; ///< A profiler is attached right now.
};

[[nodiscard]] std::string_view to_string(state value) noexcept;

[[nodiscard]] status current() noexcept;

/// Starts the profiler on first use and switches recording on.
///
/// Errors: `unsupported` when the build has no instrumentation or Tracy was
/// already shut down, `device_busy` when the requested port is taken,
/// `port_exhausted` when no port in the search range is free, and
/// `invalid_argument` when `allow_remote` is requested after a local start.
[[nodiscard]] result<status> enable(const options& wanted = {});

/// Stops recording. Safe to call when nothing is running.
void disable() noexcept;

/// Stops recording and ends the profiler for good, flushing to a connected
/// profiler. Call it once, from the thread that is last to touch Tracy, when
/// the process is about to exit. It leaves Tracy running when a thread is
/// still inside a zone after a short wait.
void shutdown() noexcept;

/// Whether this build starts profiling by itself: true for Debug builds, which
/// are never shipped, false everywhere else.
[[nodiscard]] bool default_on() noexcept;

/// What a program was asked to do about profiling at start-up.
struct request
{
    bool enable{false};
    options wanted{};

    /// How long `start` waits for a profiler to connect, so a short program
    /// is captured from its first zone. Zero does not wait.
    std::chrono::seconds wait{0};
};

/// A request that starts from `default_enable` (by default `default_on()`) and
/// then follows the environment:
///
///  * `VSS_PROFILE=1` (or `on`, `true`, `yes`) turns it on, `0` turns it off,
///  * `VSS_PROFILE_PORT=<n>` picks the port,
///  * `VSS_PROFILE_REMOTE=1` allows other computers to connect,
///  * `VSS_PROFILE_WAIT=<seconds>` makes `start` wait for a profiler.
[[nodiscard]] request
request_from_environment(bool default_enable = default_on());

/// Reads one command-line argument. Returns true when it was one of
///
///   --profile            turn profiling on
///   --profile=<port>     turn it on and listen on that port
///   --profile-remote     turn it on and allow other computers to connect
///   --profile-wait=<s>   wait up to that long for a profiler at start-up
///   --no-profile         turn it off (wins over the environment)
///
/// and updated `into`.
[[nodiscard]] bool take_argument(std::string_view argument, request& into);

/// The option lines for a `--help` text.
[[nodiscard]] std::string_view usage() noexcept;

/// Acts on a request: calls `enable()` when `enable` is set, then waits for a
/// profiler to connect when `wait` asks for it.
[[nodiscard]] result<status> start(const request& what);

/// Waits until a profiler is attached; false when `timeout` passes first.
[[nodiscard]] bool wait_for_profiler(std::chrono::milliseconds timeout);

/// One sentence in plain language about where profiling stands, for a log
/// line or a status bar.
[[nodiscard]] std::string describe(const status& value);

} // namespace vss::profiling
