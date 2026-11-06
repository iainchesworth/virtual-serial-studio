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
#include <vector>

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

TEST_CASE("listen: close frees a parked read even if the loop never runs again")
{
    // The hub stops its io_context once every endpoint has closed, so the
    // strand may never run another handler. An endpoint's read handler holds
    // the endpoint, which holds the transport: if close() left that handler
    // parked, the transport and the handler would keep each other alive.
    std::weak_ptr<listen_transport> weak;
    {
        asio::io_context io;
        listen_address address;
        address.host = "127.0.0.1";
        error_code ec;
        auto transport =
            std::make_shared<listen_transport>(io.get_executor(), address, ec);
        REQUIRE(!ec);
        weak = transport;

        auto buffer = std::make_shared<std::array<char, 16>>();
        transport->async_read_some(
            asio::buffer(*buffer),
            [transport, buffer](boost::system::error_code, std::size_t) {});
        io.poll(); // the strand parks the read

        transport->close();
        transport.reset();
        // io is not run again; destroying it drops what is still queued.
    }
    REQUIRE(weak.expired());
}

TEST_CASE("listen: cancel completes a parked read at once")
{
    asio::io_context io;
    listen_address address;
    address.host = "127.0.0.1";
    error_code ec;
    listen_transport transport(io.get_executor(), address, ec);
    REQUIRE(!ec);

    bool done = false;
    boost::system::error_code result;
    std::array<char, 16> buffer{};
    transport.async_read_some(asio::buffer(buffer),
                              [&](boost::system::error_code error, std::size_t)
                              {
                                  done = true;
                                  result = error;
                              });
    io.poll();
    REQUIRE(!done);

    transport.cancel();
    io.poll();
    REQUIRE(done);
    REQUIRE(result == asio::error::operation_aborted);
    REQUIRE(transport.is_open());
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

namespace
{

/// True once `done` holds, waited for up to three seconds.
template <class Done> bool wait_for(Done done)
{
    for (int attempt = 0; attempt < 300; ++attempt)
    {
        if (done())
            return true;
        std::this_thread::sleep_for(10ms);
    }
    return false;
}

} // namespace

TEST_CASE("listen: the allow list parses, prints and admits")
{
    const auto parsed = parse_listen_uri(
        "rfc2217-listen://0.0.0.0:7001?allow=192.168.1.0/24,10.0.0.5,fd00::/8");
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->protocol == listen_protocol::rfc2217);
    REQUIRE_EQ(parsed->allow.size(), std::size_t{3});
    REQUIRE_EQ(parsed->allow[0], std::string("192.168.1.0/24"));
    REQUIRE_EQ(make_listen_uri(*parsed),
               std::string("rfc2217-listen://0.0.0.0:7001"
                           "?allow=192.168.1.0/24,10.0.0.5,fd00::/8"));
    const auto round_trip = parse_listen_uri(make_listen_uri(*parsed));
    REQUIRE(round_trip.has_value());
    REQUIRE(*round_trip == *parsed);

    // IPv6 hosts and every scheme take the list.
    REQUIRE(parse_listen_uri("tcp-listen://[::1]:4001?allow=::1").has_value());
    REQUIRE(
        parse_listen_uri("udp-listen://:5000?allow=10.0.0.0/8").has_value());
    // No list, no query.
    REQUIRE(parse_listen_uri("tcp-listen://127.0.0.1:4001")->allow.empty());

    for (const char* bad :
         {"tcp-listen://127.0.0.1:4001?", "tcp-listen://127.0.0.1:4001?allow=",
          "tcp-listen://127.0.0.1:4001?allow=,",
          "tcp-listen://127.0.0.1:4001?x=1",
          "tcp-listen://127.0.0.1:4001?allow=10.0.0.0/33",
          "tcp-listen://127.0.0.1:4001?allow=10.0.0.0/",
          "tcp-listen://127.0.0.1:4001?allow=10.0.0.0/x",
          "tcp-listen://127.0.0.1:4001?allow=fd00::/129",
          "tcp-listen://127.0.0.1:4001?allow=host.example",
          "tcp-listen://127.0.0.1:4001?allow=10.0.0.1&allow=10.0.0.2",
          "tcp-listen://127.0.0.1:4001?allow=10.0.0.1,"})
    {
        REQUIRE(!parse_listen_uri(bad).has_value());
    }

    const std::vector<std::string> list = {"192.168.1.0/24", "10.0.0.5",
                                           "fd00::/8"};
    REQUIRE(allow_list_admits({}, "203.0.113.9"));
    REQUIRE(allow_list_admits(list, "192.168.1.200"));
    REQUIRE(!allow_list_admits(list, "192.168.2.1"));
    REQUIRE(allow_list_admits(list, "10.0.0.5"));
    REQUIRE(!allow_list_admits(list, "10.0.0.6"));
    REQUIRE(allow_list_admits(list, "fd12::1"));
    REQUIRE(!allow_list_admits(list, "fe80::1"));
    // An IPv4 address that arrives as IPv6 is the IPv4 address.
    REQUIRE(allow_list_admits(list, "::ffff:192.168.1.7"));
    REQUIRE(!allow_list_admits(list, "::ffff:192.168.3.7"));
    REQUIRE(!allow_list_admits(list, "not an address"));
    REQUIRE(allow_list_admits({"0.0.0.0/0"}, "198.51.100.1"));
    REQUIRE(allow_list_admits({"10.1.2.3/32"}, "10.1.2.3"));
    REQUIRE(!allow_list_admits({"10.1.2.3/32"}, "10.1.2.4"));
    // A prefix that is not a whole number of bytes.
    REQUIRE(allow_list_admits({"172.16.0.0/12"}, "172.31.255.255"));
    REQUIRE(!allow_list_admits({"172.16.0.0/12"}, "172.32.0.1"));

    listen_address address;
    REQUIRE(!listens_on_loopback(address)); // every interface
    address.host = "127.0.0.1";
    REQUIRE(listens_on_loopback(address));
    address.host = "127.8.9.10";
    REQUIRE(listens_on_loopback(address));
    address.host = "::1";
    REQUIRE(listens_on_loopback(address));
    address.host = "192.168.1.10";
    REQUIRE(!listens_on_loopback(address));
    address.host = "0.0.0.0";
    REQUIRE(!listens_on_loopback(address));
}

TEST_CASE("listen: a TCP client outside the allow list is closed at once")
{
    loop running;
    listen_address address;
    address.host = "127.0.0.1";
    address.allow = {"10.0.0.0/8"};
    error_code ec;
    listen_transport refusing(running.io.get_executor(), address, ec);
    REQUIRE(!ec);

    asio::io_context client_io;
    auto outsider = connect_to(client_io, refusing.local_port());
    boost::system::error_code closed;
    std::array<char, 8> buffer{};
    outsider.read_some(asio::buffer(buffer), closed);
    REQUIRE(closed == asio::error::eof ||
            closed == asio::error::connection_reset);
    REQUIRE(wait_for([&] { return refusing.refused_count() == 1; }));
    REQUIRE_EQ(refusing.peer_count(), std::size_t{0});

    // The listener goes on: a client on the list is served.
    address.allow = {"127.0.0.0/8"};
    listen_transport admitting(running.io.get_executor(), address, ec);
    REQUIRE(!ec);
    auto member = connect_to(client_io, admitting.local_port());
    REQUIRE(wait_for_peers(admitting, 1));
    asio::write(member, asio::buffer(std::string("in")));
    REQUIRE_EQ(read_from(admitting).text, std::string("in"));
    REQUIRE_EQ(admitting.refused_count(), std::size_t{0});

    // A list that is not valid cannot be opened.
    address.allow = {"nonsense"};
    listen_transport broken(running.io.get_executor(), address, ec);
    REQUIRE(ec == errc::invalid_uri);
}

TEST_CASE("listen: an RFC 2217 server applies the allow list as well")
{
    loop running;
    listen_address address;
    address.protocol = listen_protocol::rfc2217;
    address.host = "127.0.0.1";
    address.allow = {"::1"};
    error_code ec;
    listen_transport server(running.io.get_executor(), address, ec);
    REQUIRE(!ec);
    asio::io_context client_io;
    auto outsider = connect_to(client_io, server.local_port());
    boost::system::error_code closed;
    std::array<char, 8> buffer{};
    outsider.read_some(asio::buffer(buffer), closed);
    // Closed before the greeting: nothing was sent.
    REQUIRE(closed);
    REQUIRE(wait_for([&] { return server.refused_count() == 1; }));
}

TEST_CASE("listen: datagrams from outside the allow list are ignored")
{
    loop running;
    listen_address address;
    address.protocol = listen_protocol::udp;
    address.host = "127.0.0.1";
    address.allow = {"10.0.0.0/8"};
    error_code ec;
    listen_transport transport(running.io.get_executor(), address, ec);
    REQUIRE(!ec);

    asio::io_context client_io;
    asio::ip::udp::socket client(client_io, asio::ip::udp::v4());
    client.send_to(
        asio::buffer(std::string("no")),
        {asio::ip::make_address("127.0.0.1"), transport.local_port()});
    REQUIRE(wait_for([&] { return transport.refused_count() == 1; }));
    REQUIRE_EQ(transport.peer_count(), std::size_t{0});
    REQUIRE_EQ(read_from(transport, 200ms).error, asio::error::timed_out);
}

TEST_CASE("listen: the client count is available through the transport interface")
{
    loop running;
    auto transport = listen_on(running, listen_protocol::tcp);
    const vss::transport& as_transport = *transport;

    // A listener always has a count, even when it is zero; that is how "no
    // remote connected" differs from "not a listener".
    REQUIRE(as_transport.client_count().has_value());
    REQUIRE_EQ(*as_transport.client_count(), std::size_t{0});

    asio::io_context client_io;
    auto first = connect_to(client_io, transport->local_port());
    REQUIRE(wait_for_peers(*transport, 1));
    REQUIRE_EQ(*as_transport.client_count(), std::size_t{1});

    auto second = connect_to(client_io, transport->local_port());
    REQUIRE(wait_for_peers(*transport, 2));
    REQUIRE_EQ(*as_transport.client_count(), std::size_t{2});

    first.close();
    REQUIRE(wait_for_peers(*transport, 1));
    REQUIRE_EQ(*as_transport.client_count(), std::size_t{1});
}
