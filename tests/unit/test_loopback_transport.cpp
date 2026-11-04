//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <vss/ports/loopback_transport.hpp>

#include "test_framework.hpp"

#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>

#include <array>
#include <memory>

using namespace vss;

namespace {

/// A loopback transport plus an io_context to drive it by hand.
struct fixture
{
    boost::asio::io_context io;
    std::shared_ptr<loopback_transport> port =
        loopback_transport::create(io.get_executor(), "unit");

    byte_buffer read(std::size_t capacity = 64)
    {
        byte_buffer buffer(capacity);
        std::size_t received = 0;
        boost::system::error_code error;
        bool called = false;

        port->async_read_some(boost::asio::buffer(buffer),
                              [&](boost::system::error_code ec, std::size_t count) {
                                  error = ec;
                                  received = count;
                                  called = true;
                              });
        io.restart();
        io.run();
        if (!called)
            return {};
        if (error)
            return {};
        buffer.resize(received);
        return buffer;
    }
};

} // namespace

TEST_CASE("loopback: write then read")
{
    fixture f;
    REQUIRE(f.port->is_open());
    REQUIRE_EQ(f.port->public_path(), std::string("loopback:unit"));

    std::size_t written = 0;
    f.port->async_write_some(boost::asio::buffer("hello", 5),
                             [&](boost::system::error_code, std::size_t count) {
                                 written = count;
                             });
    f.io.run();
    REQUIRE_EQ(written, std::size_t{5});
    REQUIRE_EQ(f.port->available(), std::size_t{5});
    REQUIRE_EQ(to_string(bytes_view(f.read())), std::string("hello"));
    REQUIRE_EQ(f.port->available(), std::size_t{0});
}

TEST_CASE("loopback: parked read is satisfied by the next write")
{
    fixture f;

    byte_buffer buffer(16);
    std::string received;
    f.port->async_read_some(boost::asio::buffer(buffer),
                            [&](boost::system::error_code ec, std::size_t count) {
                                if (!ec)
                                    received.assign(
                                        reinterpret_cast<char*>(buffer.data()),
                                        count);
                            });

    f.port->async_write_some(boost::asio::buffer("abc", 3),
                             [](boost::system::error_code, std::size_t) {});
    f.io.run();
    REQUIRE_EQ(received, std::string("abc"));
}

TEST_CASE("loopback: inject feeds a parked read and the remainder is buffered")
{
    fixture f;

    byte_buffer buffer(2);
    std::size_t received = 0;
    f.port->async_read_some(boost::asio::buffer(buffer),
                            [&](boost::system::error_code, std::size_t count) {
                                received = count;
                            });

    f.port->inject(as_bytes("wxyz"));
    f.io.run();
    REQUIRE_EQ(received, std::size_t{2});
    REQUIRE_EQ(f.port->available(), std::size_t{2});
    REQUIRE_EQ(to_string(bytes_view(f.read(8))), std::string("yz"));

    f.port->inject(as_bytes("ignored-empty"));
    REQUIRE(f.port->available() > 0);
}

TEST_CASE("loopback: partial reads drain the queue")
{
    fixture f;
    f.port->inject(as_bytes("abcdef"));
    REQUIRE_EQ(to_string(bytes_view(f.read(4))), std::string("abcd"));
    REQUIRE_EQ(to_string(bytes_view(f.read(4))), std::string("ef"));
}

TEST_CASE("loopback: close aborts a parked read")
{
    fixture f;
    boost::system::error_code observed;
    std::array<byte, 4> storage{};

    f.port->async_read_some(boost::asio::buffer(storage),
                            [&](boost::system::error_code ec, std::size_t) {
                                observed = ec;
                            });

    f.port->close();
    f.io.run();

    REQUIRE_EQ(observed, boost::asio::error::operation_aborted);
    REQUIRE(!f.port->is_open());
}

TEST_CASE("loopback: cancel aborts a parked read but keeps the port open")
{
    fixture f;
    boost::system::error_code observed;

    byte_buffer buffer(4);
    f.port->async_read_some(boost::asio::buffer(buffer),
                            [&](boost::system::error_code ec, std::size_t) {
                                observed = ec;
                            });

    f.port->cancel();
    f.io.run();

    REQUIRE_EQ(observed, boost::asio::error::operation_aborted);
    REQUIRE(f.port->is_open());
}

TEST_CASE("loopback: operations on a closed port fail")
{
    fixture f;
    f.port->close();

    std::array<byte, 2> storage{};
    boost::system::error_code read_error;
    f.port->async_read_some(boost::asio::buffer(storage),
                            [&](boost::system::error_code ec, std::size_t) {
                                read_error = ec;
                            });
    f.io.restart();
    f.io.run();
    REQUIRE_EQ(read_error, make_error_code(errc::not_open));

    boost::system::error_code write_error;
    f.port->async_write_some(boost::asio::buffer("x", 1),
                             [&](boost::system::error_code ec, std::size_t) {
                                 write_error = ec;
                             });
    f.io.restart();
    f.io.run();
    REQUIRE_EQ(write_error, make_error_code(errc::not_open));

    // Closing twice is harmless.
    f.port->close();
    REQUIRE(!f.port->is_open());
}

TEST_CASE("loopback: control lines are fully modelled")
{
    fixture f;
    REQUIRE(f.port->supports_control_lines());
    REQUIRE(f.port->lines().cts());
    REQUIRE(f.port->lines().dsr());
    REQUIRE(f.port->lines().dcd());

    f.port->set_control_line(control_line::dtr, true);
    f.port->set_control_line(control_line::cts, false);
    REQUIRE(f.port->lines().dtr());
    REQUIRE(!f.port->lines().cts());

    f.port->set_control_line(control_line::dtr, false);
    REQUIRE(!f.port->lines().dtr());
}

TEST_CASE("loopback: describe mentions the name")
{
    boost::asio::io_context io;
    auto port = loopback_transport::create(io.get_executor(), "demo");
    REQUIRE_EQ(port->describe(), std::string("loopback device 'demo'"));
}

TEST_CASE("loopback: a completed read leaves nothing parked")
{
    fixture f;
    byte_buffer buffer(8);
    int completions = 0;

    // Only the parked handler holds the token. Once the read has completed, a
    // copy left behind in the transport would keep it alive and would complete
    // a second time on the next write (libc++ does not empty a moved-from
    // std::function that holds a small callable).
    std::weak_ptr<int> watch;
    {
        auto token = std::make_shared<int>(0);
        watch = token;
        f.port->async_read_some(
            boost::asio::buffer(buffer),
            [&completions, token](boost::system::error_code, std::size_t) {
                ++completions;
            });
    }
    REQUIRE(!watch.expired());

    f.port->async_write_some(boost::asio::buffer("ab", 2),
                             [](boost::system::error_code, std::size_t) {});
    f.io.restart();
    f.io.run();
    REQUIRE_EQ(completions, 1);
    REQUIRE(watch.expired());

    f.port->async_write_some(boost::asio::buffer("cd", 2),
                             [](boost::system::error_code, std::size_t) {});
    f.io.restart();
    f.io.run();
    REQUIRE_EQ(completions, 1);
    REQUIRE_EQ(f.port->available(), std::size_t{2});
}
