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

#include <array>
#include <chrono>
#include <string>

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
