//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Listening endpoints: the studio waits on a loopback address and a remote
// computer, played here by plain sockets, connects to it.
//

#include "support/test_support.hpp"

#include <vss/ports/listen_transport.hpp>
#include <vss/studio/studio.hpp>

#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace vss;
using namespace vss::testkit;

namespace {
using namespace std::chrono_literals;

/// The port a listening endpoint bound, read from the path it advertises.
std::uint16_t bound_port(studio& app, const endpoint& port)
{
    const auto address = parse_listen_uri(app.advertised_path(port.id()));
    REQUIRE(address.has_value());
    REQUIRE(address->port != 0);
    return address->port;
}

} // namespace

TEST_CASE("system: a listening TCP endpoint carries bytes both ways")
{
    studio app;
    const auto port = app.attach_serial("server", "tcp-listen://127.0.0.1:0");
    REQUIRE(port);
    REQUIRE((*port)->is_open());
    REQUIRE_EQ((*port)->kind(), endpoint_kind::tcp);

    boost::asio::io_context io;
    boost::asio::ip::tcp::socket client(io);
    client.connect({boost::asio::ip::make_address("127.0.0.1"),
                    bound_port(app, **port)});

    boost::asio::write(client, boost::asio::buffer(std::string("hello")));
    REQUIRE(wait_for_rx(**port, 5, 5000ms));

    const auto written = (*port)->write_sync(as_bytes("world"), 3000ms);
    REQUIRE(written);
    std::array<char, 5> reply{};
    boost::asio::read(client, boost::asio::buffer(reply));
    REQUIRE_EQ(std::string(reply.data(), reply.size()), std::string("world"));
}

TEST_CASE("system: a listening UDP endpoint reads datagrams and answers the sender")
{
    studio app;
    const auto port = app.attach_serial("server", "udp-listen://127.0.0.1:0");
    REQUIRE(port);
    REQUIRE_EQ((*port)->kind(), endpoint_kind::udp);

    boost::asio::io_context io;
    boost::asio::ip::udp::socket client(io, boost::asio::ip::udp::v4());
    const boost::asio::ip::udp::endpoint server(
        boost::asio::ip::make_address("127.0.0.1"), bound_port(app, **port));
    client.send_to(boost::asio::buffer(std::string("ping")), server);
    REQUIRE(wait_for_rx(**port, 4, 5000ms));

    const auto written = (*port)->write_sync(as_bytes("pong"), 3000ms);
    REQUIRE(written);
    std::array<char, 8> reply{};
    boost::asio::ip::udp::endpoint from;
    const std::size_t count = client.receive_from(boost::asio::buffer(reply), from);
    REQUIRE_EQ(std::string(reply.data(), count), std::string("pong"));
}

TEST_CASE("system: a listening endpoint on a port that is taken fails cleanly")
{
    studio app;
    const auto first = app.attach_serial("first", "tcp-listen://127.0.0.1:0");
    REQUIRE(first);
    const auto port = bound_port(app, **first);

    const auto second = app.attach_serial(
        "second", "tcp-listen://127.0.0.1:" + std::to_string(port));
    REQUIRE(!second);
    REQUIRE(second.error() == boost::asio::error::address_in_use);
}

TEST_CASE("system: a listening endpoint with a malformed address is refused")
{
    studio app;
    REQUIRE(!app.attach_serial("bad", "tcp-listen://127.0.0.1"));
    REQUIRE(!app.attach_serial("bad", "udp-listen://example.invalid:4001"));
}

namespace
{

struct timing
{
    double median = 0;
    double p99 = 0;
    double worst = 0;
};

timing summarise(std::vector<double> milliseconds)
{
    std::sort(milliseconds.begin(), milliseconds.end());
    return {milliseconds[milliseconds.size() / 2],
            milliseconds[milliseconds.size() * 99 / 100], milliseconds.back()};
}

void report(const char* what, const timing& result)
{
    std::cout << what << ": median " << result.median << " ms, p99 "
              << result.p99 << " ms, worst " << result.worst << " ms\n";
}

} // namespace

TEST_CASE("system: two small writes in a row reach a TCP client without "
          "waiting for an acknowledgement")
{
    // Without TCP_NODELAY the second write waits for the first to be
    // acknowledged, and the client acknowledges late (40 ms on Linux, up to
    // 200 ms on Windows): the reply of a polled bus would miss its window.
    studio app;
    const auto port = app.attach_serial("server", "tcp-listen://127.0.0.1:0");
    REQUIRE(port);

    boost::asio::io_context io;
    boost::asio::ip::tcp::socket client(io);
    client.connect(
        {boost::asio::ip::make_address("127.0.0.1"), bound_port(app, **port)});
    REQUIRE(wait_until([&] { return (*port)->is_open(); }, 3000ms));
    // The server learns of the client when it accepts the connection.
    std::this_thread::sleep_for(100ms);

    std::vector<double> milliseconds;
    for (int round = 0; round < 100; ++round)
    {
        const auto before = std::chrono::steady_clock::now();
        REQUIRE((*port)->write_sync(as_bytes("\x10\x02\x50\x01"), 3000ms));
        REQUIRE((*port)->write_sync(as_bytes("\x62\x10\x03\xFF"), 3000ms));
        std::array<char, 8> reply{};
        boost::asio::read(client, boost::asio::buffer(reply));
        const auto after = std::chrono::steady_clock::now();
        milliseconds.push_back(
            std::chrono::duration<double, std::milli>(after - before).count());
    }
    const timing result = summarise(milliseconds);
    report("tcp-listen, two writes of four bytes", result);
    // 200 ms is the reply window of the bus. On Windows loopback a held-back
    // write is not much slower, so this guards the figure rather than the
    // option; on Linux the missing option costs 40 ms a round.
    REQUIRE(result.median < 20.0);
    REQUIRE(result.p99 < 150.0);
}

TEST_CASE("system: a poll and its reply cross an RFC 2217 server in a small "
          "part of 200 ms")
{
    // Computer A listens and has the master, a loopback port that sends back
    // what it is given. Computer B is a plain TCP client that sends the poll.
    studio app;
    const auto master =
        app.create_virtual_port("master", virtual_port_kind::loopback);
    const auto server =
        app.attach_serial("net-in", "rfc2217-listen://127.0.0.1:0");
    REQUIRE(master);
    REQUIRE(server);
    REQUIRE(app.connect("master", "net-in"));

    boost::asio::io_context io;
    boost::asio::ip::tcp::socket client(io);
    client.connect({boost::asio::ip::make_address("127.0.0.1"),
                    bound_port(app, **server)});
    client.set_option(boost::asio::ip::tcp::no_delay(true));
    std::this_thread::sleep_for(200ms);

    // The greeting and the answers to the first commands come before any data;
    // send one poll and read until its echo arrives.
    const std::string poll = "\x10\x02\x50\x01\x62\x10\x03\x7F";
    std::vector<double> milliseconds;
    std::array<char, 4096> buffer{};
    std::string seen;
    for (int round = 0; round < 300; ++round)
    {
        const auto before = std::chrono::steady_clock::now();
        boost::asio::write(client, boost::asio::buffer(poll));
        // What came before the echo (the greeting of the first round, a modem
        // state notice) is skipped; the next round starts after the echo.
        bool echoed = false;
        while (!echoed && std::chrono::steady_clock::now() < before + 3s)
        {
            const auto at = seen.find(poll);
            if (at != std::string::npos)
            {
                seen.erase(0, at + poll.size());
                echoed = true;
                break;
            }
            const std::size_t n = client.read_some(boost::asio::buffer(buffer));
            seen.append(buffer.data(), n);
        }
        const auto after = std::chrono::steady_clock::now();
        REQUIRE(echoed);
        if (round > 0)
        {
            milliseconds.push_back(
                std::chrono::duration<double, std::milli>(after - before)
                    .count());
        }
    }
    const timing result = summarise(milliseconds);
    report("rfc2217-listen, 8 byte poll and echoed reply", result);
    REQUIRE(result.median < 50.0);
    REQUIRE(result.p99 < 190.0);
}
