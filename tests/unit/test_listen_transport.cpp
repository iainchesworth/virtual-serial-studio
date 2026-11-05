//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <vss/ports/listen_transport.hpp>

#include "test_framework.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>

#include <array>
#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <thread>

using namespace vss;
namespace asio = boost::asio;
using namespace std::chrono_literals;

namespace {

/// An io_context that runs on its own thread, like the hub's.
struct loop
{
    asio::io_context io;
    asio::executor_work_guard<asio::io_context::executor_type> guard{
        io.get_executor()};
    std::thread thread{[this] { io.run(); }};

    ~loop()
    {
        guard.reset();
        io.stop();
        thread.join();
    }
};

struct read_result
{
    boost::system::error_code error;
    std::string text;
};

/// One read from the transport, waited for up to `wait`.
read_result read_from(listen_transport& transport,
                      std::chrono::milliseconds wait = 3000ms)
{
    auto promise = std::make_shared<std::promise<read_result>>();
    auto buffer = std::make_shared<std::array<char, 256>>();
    transport.async_read_some(
        asio::buffer(*buffer),
        [promise, buffer](boost::system::error_code ec, std::size_t count)
        { promise->set_value({ec, std::string(buffer->data(), count)}); });
    auto future = promise->get_future();
    if (future.wait_for(wait) != std::future_status::ready)
    {
        transport.cancel();
        future.wait();
        return {asio::error::timed_out, {}};
    }
    return future.get();
}

/// A write through the transport, waited for.
std::size_t write_to(listen_transport& transport, const std::string& text)
{
    auto promise = std::make_shared<std::promise<std::size_t>>();
    transport.async_write_some(
        asio::buffer(text),
        [promise](boost::system::error_code, std::size_t count)
        { promise->set_value(count); });
    auto future = promise->get_future();
    REQUIRE(future.wait_for(3000ms) == std::future_status::ready);
    return future.get();
}

bool wait_for_peers(const listen_transport& transport, std::size_t count)
{
    for (int attempt = 0; attempt < 300; ++attempt)
    {
        if (transport.peer_count() == count)
            return true;
        std::this_thread::sleep_for(10ms);
    }
    return false;
}

std::unique_ptr<listen_transport> listen_on(loop& running,
                                            listen_protocol protocol)
{
    listen_address address;
    address.protocol = protocol;
    address.host = "127.0.0.1";
    address.port = 0;
    error_code ec;
    auto transport = std::make_unique<listen_transport>(
        running.io.get_executor(), address, ec);
    REQUIRE(!ec);
    REQUIRE(transport->local_port() != 0);
    return transport;
}

asio::ip::tcp::socket connect_to(asio::io_context& io, std::uint16_t port)
{
    asio::ip::tcp::socket socket(io);
    socket.connect({asio::ip::make_address("127.0.0.1"), port});
    return socket;
}

std::string receive(asio::ip::tcp::socket& socket, std::size_t count)
{
    std::string text(count, '\0');
    asio::read(socket, asio::buffer(text));
    return text;
}

} // namespace

TEST_CASE("listen: parsing the URIs")
{
    const auto tcp = parse_listen_uri("tcp-listen://127.0.0.1:4001");
    REQUIRE(tcp.has_value());
    REQUIRE(tcp->protocol == listen_protocol::tcp);
    REQUIRE_EQ(tcp->host, std::string("127.0.0.1"));
    REQUIRE_EQ(tcp->port, std::uint16_t{4001});

    const auto all = parse_listen_uri("udp-listen://:5000");
    REQUIRE(all.has_value());
    REQUIRE(all->protocol == listen_protocol::udp);
    REQUIRE(all->host.empty());

    const auto six = parse_listen_uri("tcp-listen://[::1]:4001");
    REQUIRE(six.has_value());
    REQUIRE_EQ(six->host, std::string("::1"));

    REQUIRE_EQ(make_listen_uri(*tcp),
               std::string("tcp-listen://127.0.0.1:4001"));
    REQUIRE_EQ(make_listen_uri(*six), std::string("tcp-listen://[::1]:4001"));
    REQUIRE_EQ(make_listen_uri(*all), std::string("udp-listen://:5000"));

    REQUIRE(is_listen_uri("tcp-listen://x"));
    REQUIRE(!is_listen_uri("tcp://127.0.0.1:4001"));
    for (const char* bad :
         {"tcp-listen://", "tcp-listen://127.0.0.1", "tcp-listen://127.0.0.1:",
          "tcp-listen://127.0.0.1:70000", "tcp-listen://host.example:4001",
          "tcp-listen://[::1:4001", "tcp-listen://::1:4001",
          "tcp-listen://127.0.0.1:40x1", "tcp-listen://127.0.0.1:4001/path",
          "serial://COM3"})
    {
        REQUIRE(!parse_listen_uri(bad).has_value());
    }
}

TEST_CASE("listen: bytes from a TCP client are read and the reply reaches it")
{
    loop running;
    auto transport = listen_on(running, listen_protocol::tcp);
    asio::io_context client_io;
    auto client = connect_to(client_io, transport->local_port());
    REQUIRE(wait_for_peers(*transport, 1));

    asio::write(client, asio::buffer(std::string("hello")));
    const auto got = read_from(*transport);
    REQUIRE(!got.error);
    REQUIRE_EQ(got.text, std::string("hello"));

    REQUIRE_EQ(write_to(*transport, "world"), std::size_t{5});
    REQUIRE_EQ(receive(client, 5), std::string("world"));
}

TEST_CASE("listen: a write goes to every connected client")
{
    loop running;
    auto transport = listen_on(running, listen_protocol::tcp);
    asio::io_context client_io;
    auto first = connect_to(client_io, transport->local_port());
    auto second = connect_to(client_io, transport->local_port());
    REQUIRE(wait_for_peers(*transport, 2));

    REQUIRE_EQ(write_to(*transport, "all"), std::size_t{3});
    REQUIRE_EQ(receive(first, 3), std::string("all"));
    REQUIRE_EQ(receive(second, 3), std::string("all"));

    asio::write(second, asio::buffer(std::string("two")));
    REQUIRE_EQ(read_from(*transport).text, std::string("two"));
}

TEST_CASE("listen: with nobody connected a write is dropped without an error")
{
    loop running;
    auto transport = listen_on(running, listen_protocol::tcp);
    REQUIRE_EQ(transport->peer_count(), std::size_t{0});
    REQUIRE_EQ(write_to(*transport, "nobody"), std::size_t{6});
}

TEST_CASE("listen: a client that leaves is forgotten")
{
    loop running;
    auto transport = listen_on(running, listen_protocol::tcp);
    asio::io_context client_io;
    {
        auto client = connect_to(client_io, transport->local_port());
        REQUIRE(wait_for_peers(*transport, 1));
    }
    REQUIRE(wait_for_peers(*transport, 0));

    auto again = connect_to(client_io, transport->local_port());
    REQUIRE(wait_for_peers(*transport, 1));
    asio::write(again, asio::buffer(std::string("back")));
    REQUIRE_EQ(read_from(*transport).text, std::string("back"));
}

TEST_CASE("listen: a port that is taken is reported")
{
    loop running;
    auto first = listen_on(running, listen_protocol::tcp);

    listen_address address;
    address.host = "127.0.0.1";
    address.port = first->local_port();
    error_code ec;
    listen_transport second(running.io.get_executor(), address, ec);
    REQUIRE(ec == asio::error::address_in_use);
}

TEST_CASE("listen: closing aborts a read that is waiting")
{
    loop running;
    auto transport = listen_on(running, listen_protocol::tcp);

    auto promise = std::make_shared<std::promise<boost::system::error_code>>();
    auto buffer = std::make_shared<std::array<char, 16>>();
    transport->async_read_some(
        asio::buffer(*buffer),
        [promise, buffer](boost::system::error_code ec, std::size_t)
        { promise->set_value(ec); });
    transport->close();

    auto future = promise->get_future();
    REQUIRE(future.wait_for(3000ms) == std::future_status::ready);
    REQUIRE(future.get() == asio::error::operation_aborted);
    REQUIRE(!transport->is_open());
}

TEST_CASE("listen: datagrams are read and answered")
{
    loop running;
    auto transport = listen_on(running, listen_protocol::udp);

    asio::io_context client_io;
    asio::ip::udp::socket client(client_io, asio::ip::udp::v4());
    const asio::ip::udp::endpoint server(asio::ip::make_address("127.0.0.1"),
                                         transport->local_port());
    client.send_to(asio::buffer(std::string("ping")), server);
    const auto got = read_from(*transport);
    REQUIRE(!got.error);
    REQUIRE_EQ(got.text, std::string("ping"));
    REQUIRE_EQ(transport->peer_count(), std::size_t{1});

    REQUIRE_EQ(write_to(*transport, "pong"), std::size_t{4});
    std::array<char, 16> reply{};
    asio::ip::udp::endpoint from;
    const std::size_t count = client.receive_from(asio::buffer(reply), from);
    REQUIRE_EQ(std::string(reply.data(), count), std::string("pong"));
}

TEST_CASE("listen: describe and public_path name the bound address")
{
    loop running;
    auto transport = listen_on(running, listen_protocol::tcp);
    const std::string expected =
        "tcp-listen://127.0.0.1:" + std::to_string(transport->local_port());
    REQUIRE_EQ(transport->public_path(), expected);
    REQUIRE(transport->describe().find("TCP listening on 127.0.0.1") !=
            std::string::npos);
}
