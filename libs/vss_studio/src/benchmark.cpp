//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <vss/core/profile.hpp>

#include <vss/studio/benchmark.hpp>

#include <vss/core/bytes.hpp>

#include <algorithm>
#include <thread>
#include <vector>

namespace vss {
namespace {

using steady = std::chrono::steady_clock;

constexpr auto progress_interval = std::chrono::milliseconds(50);
constexpr auto poll_interval = std::chrono::milliseconds(2);

/// The same generator the test kit uses, so a pattern repeats exactly.
void fill_pattern(std::vector<unsigned char>& out, std::size_t size,
                  unsigned& state)
{
    out.resize(size);
    for (std::size_t index = 0; index < size; ++index)
    {
        state = state * 1664525u + 1013904223u;
        out[index] = static_cast<unsigned char>((state >> 16) & 0xff);
    }
}

bool stop_requested(const std::atomic<bool>* cancel)
{
    return cancel != nullptr && cancel->load(std::memory_order_relaxed);
}

} // namespace

result<benchmark_result>
run_benchmark(endpoint& sender, endpoint& receiver,
              const benchmark_options& options,
              const benchmark_progress_handler& progress,
              const std::atomic<bool>* cancel)
{
    VSS_PROFILE_ZONE("studio.benchmark.run");
    if (options.bytes == 0 || options.chunk == 0)
        return make_error_code(errc::invalid_argument);

    const std::uint64_t baseline = receiver.stats().bytes_rx;
    const auto received_now = [&]
    {
        const std::uint64_t total = receiver.stats().bytes_rx;
        return total > baseline ? total - baseline : std::uint64_t{0};
    };

    benchmark_result out;
    out.expected = options.bytes;

    auto last_report = steady::now() - progress_interval;
    const auto report = [&](bool force)
    {
        if (!progress)
            return;
        const auto now = steady::now();
        if (!force && now - last_report < progress_interval)
            return;
        last_report = now;
        progress({out.sent, std::min(received_now(), options.bytes),
                  options.bytes});
    };

    const auto started = steady::now();
    unsigned state = options.seed == 0 ? 1u : options.seed;
    std::vector<unsigned char> chunk;

    while (out.sent < options.bytes)
    {
        if (stop_requested(cancel))
        {
            out.cancelled = true;
            break;
        }
        const std::size_t size = static_cast<std::size_t>(
            std::min<std::uint64_t>(options.chunk, options.bytes - out.sent));
        fill_pattern(chunk, size, state);
        const auto written = sender.write_sync(
            bytes_view(chunk.data(), chunk.size()), options.write_timeout);
        if (!written)
            return written.error();
        out.sent += size;
        report(false);
    }

    if (!out.cancelled)
    {
        const auto deadline = steady::now() + options.receive_timeout;
        while (received_now() < out.sent && steady::now() < deadline)
        {
            if (stop_requested(cancel))
            {
                out.cancelled = true;
                break;
            }
            report(false);
            std::this_thread::sleep_for(poll_interval);
        }
    }

    out.received = received_now();
    out.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        steady::now() - started);
    out.complete = !out.cancelled && out.sent == options.bytes &&
                   out.received >= out.sent;
    report(true);
    return out;
}

std::string format_throughput(std::uint64_t bytes,
                              std::chrono::milliseconds elapsed)
{
    if (elapsed.count() <= 0)
        return "n/a";
    const double seconds = static_cast<double>(elapsed.count()) / 1000.0;
    return format_byte_size(
               static_cast<std::uint64_t>(static_cast<double>(bytes) / seconds)) +
           "/s";
}

} // namespace vss
