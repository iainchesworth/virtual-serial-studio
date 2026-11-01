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
#include <vss/core/profile.hpp>
#include <vss/ports/serial_transport.hpp>
#include <vss/studio/studio.hpp>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/write.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
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

std::string environment(const char* name)
{
#if defined(_MSC_VER)
    char* buffer = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&buffer, &length, name) == 0 && buffer != nullptr)
    {
        std::string value(buffer);
        std::free(buffer);
        return value;
    }
    return {};
#else
    const char* value = std::getenv(name);
    return value != nullptr ? std::string(value) : std::string();
#endif
}

/// A one-connection echo server, so the benchmark can cross a real socket
/// boundary instead of a mock.
class echo_server
{
public:
    echo_server() : acceptor_(io_)
    {
        boost::system::error_code error;
        const boost::asio::ip::tcp::endpoint endpoint(
            boost::asio::ip::tcp::v4(), 0);
        acceptor_.open(endpoint.protocol(), error);
        if (!error)
            acceptor_.bind(endpoint, error);
        if (!error)
            acceptor_.listen(8, error);
        if (error)
            return;

        port_ = acceptor_.local_endpoint().port();
        accept();
        thread_ = std::thread([this] { io_.run(); });
    }

    ~echo_server()
    {
        io_.stop();
        if (thread_.joinable())
            thread_.join();
    }

    std::uint16_t port() const { return port_; }

private:
    void accept()
    {
        acceptor_.async_accept(socket_, [this](boost::system::error_code error) {
            if (!error)
                read();
        });
    }

    void read()
    {
        socket_.async_read_some(
            boost::asio::buffer(buffer_),
            [this](boost::system::error_code error, std::size_t count) {
                if (error)
                    return;
                boost::asio::async_write(
                    socket_, boost::asio::buffer(buffer_, count),
                    [this](boost::system::error_code written, std::size_t) {
                        if (!written)
                            read();
                    });
            });
    }

    boost::asio::io_context io_;
    boost::asio::ip::tcp::acceptor acceptor_;
    boost::asio::ip::tcp::socket socket_{io_};
    std::array<char, 4096> buffer_{};
    std::uint16_t port_ = 0;
    std::thread thread_;
};

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
    VSS_PROFILE_ZONE("perf.route");

    const std::size_t total = 32u * 1024 * 1024;
    const std::size_t chunk = 4096;
    const byte_buffer payload(chunk, 0x5a);

    const auto start = std::chrono::steady_clock::now();
    std::size_t chunk_index = 0;
    for (std::size_t offset = 0; offset < total; offset += chunk)
    {
        if ((chunk_index++ % 16) == 0)
            VSS_PROFILE_FRAME();
        hub.route_data(source, bytes_view(payload));
    }

    const double elapsed = seconds_since(start);
    return {"hub_route", static_cast<double>(total) / elapsed / (1024.0 * 1024.0),
            "MB/s", "routing into a sink endpoint"};
}

measurement bench_event_dispatch(vss::hub& hub)
{
    VSS_PROFILE_ZONE("perf.events");

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
    VSS_PROFILE_ZONE("perf.config");

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
    VSS_PROFILE_ZONE("perf.mock_write");

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

// -- end to end -------------------------------------------------------------
//
// These two cross a real boundary (a socket, or a pair of pseudo terminals),
// so the Tracy capture shows the whole pipeline: transport completion, hub
// routing, endpoint queueing and the application write path.

measurement bench_e2e_tcp()
{
    VSS_PROFILE_ZONE("perf.e2e.tcp");

    echo_server server;
    if (server.port() == 0)
        return {"e2e_tcp", 0.0, "MB/s", "echo server did not start"};

    vss::studio app;
    const auto port = app.attach_serial(
        "echo", "tcp://127.0.0.1:" + std::to_string(server.port()));
    if (!port)
        return {"e2e_tcp", 0.0, "MB/s", "cannot open the tcp endpoint"};

    const std::size_t chunk = 4096;
    const std::size_t total = 1024u * 1024;
    const byte_buffer payload(chunk, 0x5a);

    const auto start = std::chrono::steady_clock::now();
    std::size_t sent = 0;
    for (std::size_t offset = 0; offset < total; offset += chunk)
    {
        VSS_PROFILE_FRAME();
        VSS_PROFILE_ZONE("perf.e2e.tcp.chunk");
        if (!(*port)->write_sync(bytes_view(payload), 5000ms))
            break;
        sent += chunk;
    }

    // Wait for the echo to come back through the hub before stopping the
    // clock; the writes themselves only prove they reached the socket.
    const bool drained = sent != 0
        && wait_until([&] { return (*port)->stats().bytes_rx >= sent; }, 15s);

    const double elapsed = seconds_since(start);
    return {"e2e_tcp",
            drained
                ? static_cast<double>(sent) / elapsed / (1024.0 * 1024.0)
                : 0.0,
            "MB/s", "socket echo through the hub"};
}

measurement bench_e2e_pty()
{
    VSS_PROFILE_ZONE("perf.e2e.pty");

    const std::string left_path = environment("VSS_LOOPBACK_A");
    const std::string right_path = environment("VSS_LOOPBACK_B");
    if (left_path.empty() || right_path.empty())
        return {"e2e_pty", 0.0, "MB/s", "no loopback pair in the environment"};

    vss::studio app;
    const auto left = app.attach_serial(
        "left", "serial://" + left_path + "?baud=115200");
    const auto right = app.attach_serial(
        "right", "serial://" + right_path + "?baud=115200");
    if (!left || !right)
        return {"e2e_pty", 0.0, "MB/s", "cannot open the loopback pair"};

    const std::size_t chunk = 4096;
    const std::size_t total = 512u * 1024;
    const byte_buffer payload(chunk, 0x33);

    const auto start = std::chrono::steady_clock::now();
    std::size_t sent = 0;
    for (std::size_t offset = 0; offset < total; offset += chunk)
    {
        VSS_PROFILE_FRAME();
        VSS_PROFILE_ZONE("perf.e2e.pty.chunk");
        if (!(*left)->write_sync(bytes_view(payload), 5000ms))
            break;
        sent += chunk;
    }

    const bool drained = sent != 0
        && wait_until([&] { return (*right)->stats().bytes_rx >= sent; }, 20s);

    const double elapsed = seconds_since(start);
    return {"e2e_pty",
            drained
                ? static_cast<double>(sent) / elapsed / (1024.0 * 1024.0)
                : 0.0,
            "MB/s", "pseudo terminal pair"};
}

} // namespace

int main(int argc, char** argv)
{
    bool quick = false;
    bool tracy = false;
    std::string output;
    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];
        if (argument == "--quick")
            quick = true;
        else if (argument == "--tracy")
            tracy = true;
        else if (argument.rfind("--output=", 0) == 0)
            output = argument.substr(9);
        else if (argument.rfind("--output", 0) == 0 && index + 1 < argc)
            output = argv[++index];
    }

    vss::hub_options options;
    options.io_threads = 2;

#if defined(VSS_TRACY)
    if (tracy)
    {
        // On-demand profiling records nothing until the profiler connects, and
        // that handshake is asynchronous. Wait for it so the trace starts at
        // the first benchmark instead of part way through.
        for (int attempt = 0; attempt < 300 && !TracyIsConnected; ++attempt)
            std::this_thread::sleep_for(10ms);
    }
#endif

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

    if (tracy)
    {
        // Cross a real socket and, when CI provides one, a real tty pair.
        results.push_back(bench_e2e_tcp());
        results.push_back(bench_e2e_pty());
    }

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

    // Give the Tracy client a moment to flush the closing zones before the
    // process exits and the capture tool saves the trace.
    if (tracy)
        std::this_thread::sleep_for(300ms);

    return 0;
}
