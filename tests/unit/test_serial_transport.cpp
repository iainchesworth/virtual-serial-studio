//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <vss/ports/serial_transport.hpp>

#include "test_framework.hpp"

#include <boost/asio/io_context.hpp>

#include <array>

using namespace vss;

TEST_CASE("serial transport: URI classification")
{
    REQUIRE_EQ(*classify_uri("serial://COM3"), endpoint_kind::physical);
    REQUIRE_EQ(*classify_uri("serial://COM3?baud=115200"), endpoint_kind::physical);
    REQUIRE_EQ(*classify_uri("tcp://127.0.0.1:4001"), endpoint_kind::tcp);
    REQUIRE_EQ(*classify_uri("udp://127.0.0.1:4002"), endpoint_kind::udp);
    REQUIRE_EQ(*classify_uri("rfc2217://lab:7001"), endpoint_kind::rfc2217);

    // Listening endpoints are network endpoints too.
    REQUIRE_EQ(*classify_uri("tcp-listen://127.0.0.1:4001"), endpoint_kind::tcp);
    REQUIRE_EQ(*classify_uri("udp-listen://:4002"), endpoint_kind::udp);
    REQUIRE(!classify_uri("tcp-listen://127.0.0.1"));
    REQUIRE(!classify_uri("udp-listen://host.example:4002"));

    // A bare device path is the serial library's shorthand for a local port.
    REQUIRE_EQ(*classify_uri("COM3"), endpoint_kind::physical);
    REQUIRE_EQ(*classify_uri("/dev/ttyUSB0"), endpoint_kind::physical);

    REQUIRE(!classify_uri("serial://"));
    REQUIRE(!classify_uri(""));
}

TEST_CASE("serial transport: settings become a serial config")
{
    serial_settings settings;
    settings.baud_rate = 9600;
    settings.data_bits = 7;
    settings.parity = parity_mode::even;
    settings.stop_bits = stop_bits_mode::two;
    settings.flow_control = flow_control_mode::hardware;
    settings.read_timeout = std::chrono::milliseconds(250);
    settings.write_timeout = std::chrono::milliseconds(500);
    settings.exclusive = true;

    const auto configuration = to_serial_config(settings);
    REQUIRE_EQ(configuration.baud_rate, 9600u);
    REQUIRE_EQ(configuration.data_bits, 7u);
    REQUIRE_EQ(static_cast<int>(configuration.parity),
               static_cast<int>(boost::virtual_serial::parity_mode::even));
    REQUIRE_EQ(static_cast<int>(configuration.stop_bits),
               static_cast<int>(boost::virtual_serial::stop_bits_mode::two));
    REQUIRE_EQ(static_cast<int>(configuration.flow_control),
               static_cast<int>(boost::virtual_serial::flow_control_mode::hardware));
    REQUIRE_EQ(configuration.read_timeout.count(), std::int64_t{250});
    REQUIRE_EQ(configuration.write_timeout.count(), std::int64_t{500});
    REQUIRE(configuration.exclusive);
}

TEST_CASE("serial transport: invalid URIs fail cleanly")
{
    boost::asio::io_context io;
    boost::system::error_code error;
    serial_transport transport(io.get_executor(), "banana", error);
    REQUIRE(error);
    REQUIRE(!transport.is_open());
    REQUIRE_EQ(transport.describe(), std::string("physical banana (9600 8N1)"));
}

TEST_CASE("serial transport: a mock fixture round trips")
{
    boost::asio::io_context io;

    boost::system::error_code error;
    auto transport = serial_transport::make_mock(
        io.get_executor(), serial_settings{}, "fixture", error);
    REQUIRE(!error);
    REQUIRE(transport != nullptr);
    REQUIRE(transport->is_open());
    REQUIRE_EQ(transport->kind(), endpoint_kind::mock);
    REQUIRE_EQ(transport->public_path(), std::string("fixture"));
    REQUIRE(transport->describe().find("mock fixture") != std::string::npos);
    REQUIRE(!transport->supports_control_lines());

    REQUIRE_EQ(transport->mock()->written(), std::string());

    transport->mock()->inject_read("incoming");
    std::array<char, 32> buffer{};
    std::size_t received = 0;
    transport->async_read_some(boost::asio::buffer(buffer),
                               [&](boost::system::error_code ec, std::size_t count) {
                                   REQUIRE(!ec);
                                   received = count;
                               });
    io.run();
    REQUIRE_EQ(std::string(buffer.data(), received), std::string("incoming"));

    io.restart();
    std::size_t written = 0;
    transport->async_write_some(boost::asio::buffer("outgoing", 8),
                                [&](boost::system::error_code ec, std::size_t count) {
                                    REQUIRE(!ec);
                                    written = count;
                                });
    io.run();
    REQUIRE_EQ(written, std::size_t{8});
    REQUIRE_EQ(transport->mock()->written(), std::string("outgoing"));

    // Control lines are stored but not driven by the fixture; DTR and RTS
    // are asserted on open.
    transport->set_control_line(control_line::dtr, true);
    transport->refresh_lines();
    REQUIRE_EQ(transport->lines().bits, 3u);

    transport->cancel();
    transport->close();
    REQUIRE(!transport->is_open());
    transport->close();
}

TEST_CASE("serial transport: invalid settings are rejected")
{
    boost::asio::io_context io;
    serial_settings settings;
    settings.baud_rate = 1;

    boost::system::error_code error;
    auto transport = serial_transport::make_mock(io.get_executor(), settings,
                                                 "fixture", error);
    REQUIRE(error);
    REQUIRE(transport == nullptr);
    REQUIRE_EQ(error, make_error_code(errc::invalid_argument));
}

TEST_CASE("serial transport: operations after close fail cleanly")
{
    boost::asio::io_context io;
    boost::system::error_code error;
    auto transport = serial_transport::make_mock(
        io.get_executor(), serial_settings{}, "closing", error);
    REQUIRE(!error);

    transport->close();
    REQUIRE(!transport->is_open());
    transport->cancel();
    transport->close();

    std::array<char, 4> buffer{};
    boost::system::error_code read_error;
    transport->async_read_some(boost::asio::buffer(buffer),
                               [&](boost::system::error_code ec, std::size_t) {
                                   read_error = ec;
                               });
    io.run();
    // The backend reports its own "not connected"; the transport reports
    // "not open" when no backend is attached.  Either is a clean failure.
    REQUIRE(read_error);

    io.restart();
    boost::system::error_code write_error;
    transport->async_write_some(boost::asio::buffer("x", 1),
                                [&](boost::system::error_code ec, std::size_t) {
                                    write_error = ec;
                                });
    io.run();
    REQUIRE(write_error);
}

TEST_CASE("serial transport: line operations ignore non physical backends")
{
    boost::asio::io_context io;
    boost::system::error_code error;
    auto transport = serial_transport::make_mock(
        io.get_executor(), serial_settings{}, "lines", error);
    REQUIRE(!error);

    transport->set_control_line(control_line::dtr, true);
    transport->refresh_lines();
    REQUIRE_EQ(transport->lines().bits, 3u); // dtr | rts were asserted on open
    REQUIRE(!transport->supports_control_lines());
}

TEST_CASE("serial transport: every settings enum maps across")
{
    serial_settings settings;
    settings.baud_rate = 57600;
    settings.data_bits = 5;

    settings.parity = parity_mode::odd;
    REQUIRE_EQ(static_cast<int>(to_serial_config(settings).parity),
               static_cast<int>(boost::virtual_serial::parity_mode::odd));
    settings.parity = parity_mode::even;
    REQUIRE_EQ(static_cast<int>(to_serial_config(settings).parity),
               static_cast<int>(boost::virtual_serial::parity_mode::even));
    settings.parity = parity_mode::mark;
    REQUIRE_EQ(static_cast<int>(to_serial_config(settings).parity),
               static_cast<int>(boost::virtual_serial::parity_mode::mark));
    settings.parity = parity_mode::space;
    REQUIRE_EQ(static_cast<int>(to_serial_config(settings).parity),
               static_cast<int>(boost::virtual_serial::parity_mode::space));

    settings.stop_bits = stop_bits_mode::one_point_five;
    REQUIRE_EQ(static_cast<int>(to_serial_config(settings).stop_bits),
               static_cast<int>(boost::virtual_serial::stop_bits_mode::one_point_five));
    settings.stop_bits = stop_bits_mode::two;
    REQUIRE_EQ(static_cast<int>(to_serial_config(settings).stop_bits),
               static_cast<int>(boost::virtual_serial::stop_bits_mode::two));

    settings.flow_control = flow_control_mode::software;
    REQUIRE_EQ(static_cast<int>(to_serial_config(settings).flow_control),
               static_cast<int>(boost::virtual_serial::flow_control_mode::software));
    settings.flow_control = flow_control_mode::hardware;
    REQUIRE_EQ(static_cast<int>(to_serial_config(settings).flow_control),
               static_cast<int>(boost::virtual_serial::flow_control_mode::hardware));
}

TEST_CASE("serial transport: invalid settings are rejected before opening")
{
    boost::asio::io_context io;
    serial_settings settings;
    settings.data_bits = 3;

    boost::system::error_code error;
    serial_transport transport(io.get_executor(), "serial://COM3", settings, error);
    REQUIRE_EQ(error, make_error_code(errc::invalid_argument));
    REQUIRE(!transport.is_open());
    REQUIRE_EQ(transport.native_handle(), std::intptr_t{0});
}

TEST_CASE("serial transport: opening a missing device fails")
{
    boost::asio::io_context io;
    boost::system::error_code error;
    serial_transport transport(io.get_executor(),
                               "serial://definitely-not-a-device-42", error);
    REQUIRE(error);
    REQUIRE(!transport.is_open());
}
