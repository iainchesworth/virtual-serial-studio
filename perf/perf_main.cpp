//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// vss_perf - throughput and latency floors for the studio.
//
// Every measurement is expressed in units the CI can compare against a
// checked-in floor: bytes per second for the data path, events per second for
// the dispatcher, operations per second for configuration round-trips.
//

#include <vss/config/document.hpp>
#include <vss/core/hub.hpp>
#include <vss/ports/serial_transport.hpp>

#include <boost/asio/post.hpp>
#include <boost/asio/executor_work_guard.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <future>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;
using vss::byte;
using vss::byte_buffer;
using vss::bytes_view;

/// A transport that accepts writes immediately and never produces data.
/// It isolates the cost of the hub's routing and queueing code.
class sink_transport final : public vss::transport
{
public:
    explicit sink_transport(const executor_type& executor)
        : executor_(executor)
    {
    }

    std::string describe() const override { return "sink"; }
    bool is_open() const override { return open_; }

    void async_read_some(boost::asio::mutable_buffer, io_handler handler) override
    {
        std::lock_guard lock(mutex_);
        pending_read_ = std::move(handler);
    }

    void async_write_some(boost::asio::const_buffer buffer,
                          io_handler handler) override
    {
        const std::size_t size = buffer.size();
        boost::asio::post(executor_, [handler, size] { handler({}, size); });
    }

    void close() override
    {
        io_handler pending;
        {
            std::lock_guard lock(mutex_);
            open_ = false;
            pending = std::move(pending_read_);
        }
        if (pending)
        {
            boost::asio::post(executor_, [pending] {
                pending(boost::asio::error::operation_aborted, 0);
            });
        }
    }

    void cancel() override {}

private:
    executor_type executor_;
    std::mutex mutex_;
    io_handler pending_read_;
    bool open_ = true;
};

struct measurement
{
    std::string name;
    double value = 0.0;
    std::string unit;
    std::string note;
};

double seconds_since(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
        .count();
}

bool wait_until(const std::function<bool()>& predicate,
                std::chrono::seconds timeout = 60s)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (predicate())
            return true;
        std::this_thread::sleep_for(1ms);
    }
    return predicate();
}

// -- benchmarks -------------------------------------------------------------

measurement bench_route_throughput(vss::hub& hub, vss::endpoint_id source)
{
    const std::size_t total = 32u * 1024 * 1024;
    const std::size_t chunk = 4096;
    const byte_buffer payload(chunk, 0x5a);

    const auto start = std::chrono::steady_clock::now();
    for (std::size_t offset = 0; offset < total; offset += chunk)
        hub.route_data(source, bytes_view(payload));

    const double elapsed = seconds_since(start);
    return {"hub_route", static_cast<double>(total) / elapsed / (1024.0 * 1024.0),
            "MB/s", "routing into a sink endpoint"};
}

measurement bench_event_dispatch(vss::hub& hub)
{
    constexpr std::size_t count = 20'000;
    std::atomic<std::size_t> received{0};

    hub.subscribe({}, [&](const vss::endpoint_event& event) {
        if (event.kind == vss::event_kind::log_message)
            received.fetch_add(1);
    });

    const auto start = std::chrono::steady_clock::now();
    for (std::size_t index = 0; index < count; ++index)
    {
        vss::endpoint_event event;
        event.kind = vss::event_kind::log_message;
        event.message = "benchmark";
        hub.publish(std::move(event));
    }

    const bool drained = wait_until([&] { return received.load() >= count; });
    const double elapsed = seconds_since(start);
    const double rate = drained
        ? static_cast<double>(count) / elapsed
        : 0.0;
    return {"event_dispatch", rate, "events/s", "publish to subscriber"};
}

measurement bench_config_roundtrip()
{
    vss::config::document document;
    document.name = "benchmark";
    for (int index = 0; index < 100; ++index)
    {
        vss::config::port_entry port;
        port.name = "port-" + std::to_string(index);
        port.kind = "virtual";
        port.virtual_kind = "loopback";
        port.serial.baud_rate = 115200;
        document.ports.push_back(port);
    }

    constexpr std::size_t iterations = 200;
    const auto start = std::chrono::steady_clock::now();
    std::size_t bytes = 0;
    for (std::size_t index = 0; index < iterations; ++index)
    {
        const auto text = vss::config::to_json(document, false);
        if (!text)
            return {"config_roundtrip", 0.0, "ops/s", "serialisation failed"};
        bytes += text->size();
        const auto parsed = vss::config::from_json(*text);
        if (!parsed)
            return {"config_roundtrip", 0.0, "ops/s", "parse failed"};
    }
    const double elapsed = seconds_since(start);
    return {"config_roundtrip", static_cast<double>(iterations) / elapsed,
            "ops/s", std::to_string(bytes / iterations) + " bytes/document"};
}

measurement bench_mock_write()
{
    boost::asio::io_context io;
    boost::system::error_code error;
    auto transport = vss::serial_transport::make_mock(
        io.get_executor(), vss::serial_settings{}, "bench", error);
    if (!transport)
        return {"mock_write", 0.0, "MB/s", "mock fixture unavailable"};

    // The mock keeps every written byte for assertions, so the capture is
    // cleared between chunks to keep memory and reallocation flat; the
    // measurement is the transport write path, not the recording.
    const std::size_t chunk = 4096;
    const std::size_t total = 8u * 1024 * 1024;
    const byte_buffer payload(chunk, 0x33);

    // Keep the io_context alive for the whole measurement: the mock completes
    // on that executor, and run() would otherwise return between chunks.
    auto guard = boost::asio::make_work_guard(io);
    auto thread = std::thread([&] { io.run(); });
    const auto start = std::chrono::steady_clock::now();

    std::size_t written_total = 0;
    for (std::size_t offset = 0; offset < total; offset += chunk)
    {
        std::promise<std::size_t> done;
        auto future = done.get_future();
        transport->async_write_some(boost::asio::buffer(payload),
                                    [&](boost::system::error_code ec, std::size_t n) {
                                        if (ec)
                                            return;
                                        done.set_value(n);
                                    });
        if (future.wait_for(30s) != std::future_status::ready)
            break;
        written_total += future.get();
        transport->mock()->clear_written();
    }

    const double elapsed = seconds_since(start);
    guard.reset();
    transport->close();
    io.stop();
    if (thread.joinable())
        thread.join();

    return {"mock_write",
            static_cast<double>(written_total) / elapsed / (1024.0 * 1024.0),
            "MB/s", "scriptable fixture"};
}

} // namespace

int main(int argc, char** argv)
{
    bool quick = false;
    std::string output;
    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];
        if (argument == "--quick")
            quick = true;
        else if (argument.rfind("--output=", 0) == 0)
            output = argument.substr(9);
        else if (argument.rfind("--output", 0) == 0 && index + 1 < argc)
            output = argv[++index];
    }

    vss::hub_options options;
    options.io_threads = 2;
    auto hub = std::make_shared<vss::hub>(options);
    hub->start();

    const auto add_sink = [&](const std::string& name) {
        return hub->add_endpoint(
            name, vss::endpoint_kind::mock,
            std::make_shared<sink_transport>(hub->executor()),
            vss::endpoint_options{});
    };

    std::vector<measurement> results;

    const auto source = add_sink("source");
    const auto target = add_sink("target");
    if (!source || !target)
    {
        std::fprintf(stderr, "cannot create benchmark endpoints\n");
        return 1;
    }

    if (const auto linked = hub->redirect((*source)->id(), (*target)->id()); !linked)
    {
        std::fprintf(stderr, "cannot link benchmark endpoints\n");
        return 1;
    }

    results.push_back(bench_route_throughput(*hub, (*source)->id()));
    results.push_back(bench_event_dispatch(*hub));
    results.push_back(bench_config_roundtrip());
    if (!quick)
        results.push_back(bench_mock_write());

    hub->stop();
    hub->join();

    std::printf("\n%-20s %14s  %-10s %s\n", "benchmark", "value", "unit", "note");
    std::printf("%s\n", std::string(74, '-').c_str());
    for (const auto& result : results)
    {
        std::printf("%-20s %14.1f  %-10s %s\n", result.name.c_str(), result.value,
                    result.unit.c_str(), result.note.c_str());
    }

    if (!output.empty())
    {
        std::ofstream stream(output, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            std::fprintf(stderr, "cannot write %s\n", output.c_str());
            return 1;
        }
        stream << "[\n";
        for (std::size_t index = 0; index < results.size(); ++index)
        {
            const auto& result = results[index];
            stream << "  {\"name\": \"" << result.name << "\", \"value\": "
                   << result.value << ", \"unit\": \"" << result.unit
                   << "\", \"note\": \"" << result.note << "\"}"
                   << (index + 1 < results.size() ? "," : "") << "\n";
        }
        stream << "]\n";
        stream.close();
    }

    return 0;
}
