//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <vss/studio/benchmark.hpp>
#include <vss/studio/studio.hpp>

#include "test_framework.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

using namespace vss;
using namespace std::chrono_literals;

namespace {

/// One loopback port echoes what is written to it, so it can be both ends.
std::shared_ptr<endpoint> loopback(studio& app, const char* name)
{
    const auto created =
        app.create_virtual_port(name, virtual_port_kind::loopback);
    REQUIRE(created);
    return *created;
}

} // namespace

TEST_CASE("benchmark: every byte arrives and the result says so")
{
    studio app;
    const auto port = loopback(app, "echo");

    benchmark_options options;
    options.bytes = 64 * 1024 + 123;
    options.chunk = 1000;
    options.receive_timeout = 5000ms;

    std::vector<benchmark_progress> seen;
    const auto ran = run_benchmark(
        *port, *port, options,
        [&](const benchmark_progress& value) { seen.push_back(value); });
    REQUIRE(ran);
    REQUIRE(ran->complete);
    REQUIRE(!ran->cancelled);
    REQUIRE_EQ(ran->expected, options.bytes);
    REQUIRE_EQ(ran->sent, options.bytes);
    REQUIRE_EQ(ran->received, options.bytes);
    REQUIRE_EQ(ran->lost(), std::uint64_t{0});

    REQUIRE(!seen.empty());
    REQUIRE_EQ(seen.back().sent, options.bytes);
    REQUIRE_EQ(seen.back().received, options.bytes);
    REQUIRE_EQ(seen.back().total, options.bytes);
}

TEST_CASE("benchmark: a short receive is a result that says by how much")
{
    studio app;
    const auto sender = loopback(app, "echo");
    const auto silent = app.create_mock_port("silent");
    REQUIRE(silent);

    benchmark_options options;
    options.bytes = 4096;
    options.chunk = 1024;
    options.receive_timeout = 100ms;

    const auto ran = run_benchmark(*sender, **silent, options);
    REQUIRE(ran);
    REQUIRE(!ran->complete);
    REQUIRE_EQ(ran->sent, std::uint64_t{4096});
    REQUIRE_EQ(ran->received, std::uint64_t{0});
    REQUIRE_EQ(ran->lost(), std::uint64_t{4096});
}

TEST_CASE("benchmark: bytes that arrived before the run are not counted")
{
    studio app;
    const auto port = loopback(app, "echo");

    const std::uint8_t earlier[] = {1, 2, 3, 4, 5};
    port->write(bytes_view(earlier, sizeof earlier));
    while (port->stats().bytes_rx < sizeof earlier)
        std::this_thread::sleep_for(2ms);

    benchmark_options options;
    options.bytes = 2000;
    options.chunk = 500;
    const auto ran = run_benchmark(*port, *port, options);
    REQUIRE(ran);
    REQUIRE(ran->complete);
    REQUIRE_EQ(ran->received, std::uint64_t{2000});
}

TEST_CASE("benchmark: a run stops when asked to")
{
    studio app;
    const auto port = loopback(app, "echo");

    benchmark_options options;
    options.bytes = 1024 * 1024;
    std::atomic<bool> stop{true};
    const auto ran = run_benchmark(*port, *port, options, {}, &stop);
    REQUIRE(ran);
    REQUIRE(ran->cancelled);
    REQUIRE(!ran->complete);
    REQUIRE_EQ(ran->sent, std::uint64_t{0});
}

TEST_CASE("benchmark: empty options are refused")
{
    studio app;
    const auto port = loopback(app, "echo");

    benchmark_options none;
    none.bytes = 0;
    REQUIRE_EQ(run_benchmark(*port, *port, none).error(),
               make_error_code(errc::invalid_argument));

    benchmark_options no_chunk;
    no_chunk.chunk = 0;
    REQUIRE_EQ(run_benchmark(*port, *port, no_chunk).error(),
               make_error_code(errc::invalid_argument));
}

TEST_CASE("benchmark: throughput is written with a unit, or n/a")
{
    REQUIRE_EQ(format_throughput(1024 * 1024, 1000ms), std::string("1.0 MB/s"));
    REQUIRE_EQ(format_throughput(10, 0ms), std::string("n/a"));
}
