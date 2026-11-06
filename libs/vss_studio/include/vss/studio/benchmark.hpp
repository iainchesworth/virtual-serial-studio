//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_STUDIO_BENCHMARK_HPP
#define VSS_STUDIO_BENCHMARK_HPP

#include <vss/core/endpoint.hpp>
#include <vss/core/error.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>

namespace vss {

/// What a benchmark sends and how it waits.
struct benchmark_options
{
    /// Total payload, in bytes.  Must be greater than zero.
    std::uint64_t bytes = std::uint64_t{1024} * 1024;
    /// Size of each write, in bytes.  Must be greater than zero.
    std::size_t chunk = 4096;
    /// How long one write may take before the run fails.
    std::chrono::milliseconds write_timeout{10000};
    /// How long to wait for the last bytes after the final write.
    std::chrono::milliseconds receive_timeout{60000};
    /// Seed of the payload pattern, so a run can be repeated exactly.
    unsigned seed = 11;
};

/// Where a run has got to.  `total` is the payload size.
struct benchmark_progress
{
    std::uint64_t sent = 0;
    std::uint64_t received = 0;
    std::uint64_t total = 0;
};

/// The outcome of a run that started.  A run that stops short is still a
/// result: `complete` is false and `lost()` says by how much.
struct benchmark_result
{
    std::uint64_t expected = 0;
    std::uint64_t sent = 0;
    std::uint64_t received = 0;
    std::chrono::milliseconds elapsed{0};
    /// Every byte arrived.
    bool complete = false;
    /// The caller asked to stop before the run finished.
    bool cancelled = false;

    /// Bytes that were sent and did not arrive.
    std::uint64_t lost() const noexcept
    {
        return sent > received ? sent - received : 0;
    }
};

using benchmark_progress_handler = std::function<void(const benchmark_progress&)>;

/// Writes a payload to `sender` and counts what `receiver` gets.
///
/// The call blocks until the run ends, so run it on a worker thread when a
/// user interface is waiting.  `sender` and `receiver` must not be linked to
/// each other: the data has to travel through the cable or the driver pair,
/// or the count means nothing.  Anything else that reaches `receiver` during
/// the run is counted too.
///
/// `progress` runs on the calling thread, at most about every 50 ms and once at
/// the end.  Setting `*cancel` to true stops the run at the next chunk or poll;
/// the result then has `cancelled` set.  The call fails with
/// `errc::invalid_argument` for empty options and with the transport's error if
/// a write fails or times out.
result<benchmark_result>
run_benchmark(endpoint& sender, endpoint& receiver,
              const benchmark_options& options,
              const benchmark_progress_handler& progress = {},
              const std::atomic<bool>* cancel = nullptr);

/// "12.3 MB/s", or "n/a" when no time has passed.
std::string format_throughput(std::uint64_t bytes,
                              std::chrono::milliseconds elapsed);

} // namespace vss

#endif // VSS_STUDIO_BENCHMARK_HPP
