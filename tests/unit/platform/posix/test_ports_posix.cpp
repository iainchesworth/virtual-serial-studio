//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <vss/ports/com0com.hpp>
#include <vss/ports/native_lines.hpp>
#include <vss/ports/peer_open.hpp>
#include <vss/ports/virtual_port.hpp>

#include <vss/testkit/harness.hpp>

#include "test_framework.hpp"

#include <boost/asio/io_context.hpp>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <string>
#include <vector>

extern char** environ;

using namespace vss;

TEST_CASE("native lines: peer driven inputs report the OS error")
{
    // POSIX can drive CTS/DSR/DCD/RI on a PTY, so an invalid handle reports
    // the OS error instead of an unsupported-feature error.
    for (const auto line : {control_line::cts, control_line::dsr,
                            control_line::dcd, control_line::ri})
    {
        const auto result =
            native::write_line(static_cast<std::intptr_t>(1234), line, true);
        REQUIRE(!result);
    }
}

TEST_CASE("com0com: every operation is unsupported on POSIX")
{
    // com0com is a Windows driver.
    com0com_installation fake;
    fake.found = true;
    fake.setupc_path = "does-not-exist";

    REQUIRE_EQ(create_com0com_pair(fake, "", "COM11", false),
               make_error_code(errc::unsupported));
    REQUIRE_EQ(create_com0com_pair(fake, "COM10", "COM11", false),
               make_error_code(errc::unsupported));
    REQUIRE_EQ(remove_com0com_pair(fake, 0, false),
               make_error_code(errc::unsupported));
}

TEST_CASE("virtual ports: pseudo terminals are supported on POSIX")
{
    REQUIRE(supports(virtual_port_kind::pty));
}

TEST_CASE("virtual ports: a pty is a real device")
{
    boost::asio::io_context io;
    const auto created =
        create_virtual_port(io.get_executor(), virtual_port_kind::pty, "pty");
    REQUIRE(created);
    REQUIRE_EQ(created->kind, virtual_port_kind::pty);
    REQUIRE(created->path.find("/dev/") == 0);
    REQUIRE(created->port->is_open());
    REQUIRE(created->port->supports_control_lines());
    REQUIRE(!created->port->describe().empty());

    created->port->set_control_line(control_line::dtr, true);
    created->port->refresh_lines();
    created->port->close();
    REQUIRE(!created->port->is_open());
}

TEST_CASE("virtual ports: a pty carries data in both directions")
{
    boost::asio::io_context io;
    const auto created =
        create_virtual_port(io.get_executor(), virtual_port_kind::pty, "io");
    REQUIRE(created);

    const int slave = ::open(created->path.c_str(), O_RDWR | O_NOCTTY);
    REQUIRE(slave >= 0);

    // master -> slave
    std::size_t written = 0;
    created->port->async_write_some(
        boost::asio::buffer("hello", 5),
        [&](boost::system::error_code ec, std::size_t count)
        {
            if (!ec)
                written = count;
        });
    io.run();
    REQUIRE_EQ(written, std::size_t{5});

    char buffer[16]{};
    pollfd descriptor{slave, POLLIN, 0};
    REQUIRE(::poll(&descriptor, 1, 2000) > 0);
    REQUIRE_EQ(::read(slave, buffer, 5), ssize_t{5});
    REQUIRE_EQ(std::string(buffer, 5), std::string("hello"));

    // slave -> master
    REQUIRE_EQ(::write(slave, "world", 5), ssize_t{5});
    std::size_t received = 0;
    created->port->async_read_some(
        boost::asio::buffer(buffer),
        [&](boost::system::error_code ec, std::size_t count)
        {
            if (!ec)
                received = count;
        });
    io.restart();
    io.run();
    REQUIRE_EQ(received, std::size_t{5});
    REQUIRE_EQ(std::string(buffer, 5), std::string("world"));

    created->port->set_control_line(control_line::dtr, true);
    created->port->refresh_lines();
    REQUIRE(created->port->lines().dtr());

    ::close(slave);
    created->port->close();
    REQUIRE(!created->port->is_open());
}

TEST_CASE("peer open: another process holding a pty open is noticed")
{
    using namespace std::chrono_literals;

    boost::asio::io_context io;
    const auto created =
        create_virtual_port(io.get_executor(), virtual_port_kind::pty, "peer");
    REQUIRE(created);
    const std::vector<std::string> paths{created->path};
    REQUIRE(peer_open({}).empty());

    if (!peer_open_supported())
    {
        // A platform that cannot tell must say so for every path.
        const auto states = peer_open(paths);
        REQUIRE_EQ(states.size(), std::size_t{1});
        REQUIRE(states[0] == peer_open_state::unknown);
        return;
    }

    // This process holds the master, and the slave is not open anywhere.
    REQUIRE(peer_open(paths)[0] == peer_open_state::not_open);

    // A shell runs sleep with the pseudo terminal as its standard input.
    std::string shell = "/bin/sh";
    std::string flag = "-c";
    std::string script = "exec sleep 30 < \"$0\"";
    std::string device = created->path;
    char* const arguments[] = {shell.data(), flag.data(), script.data(),
                               device.data(), nullptr};
    pid_t child = 0;
    REQUIRE_EQ(::posix_spawn(&child, shell.c_str(), nullptr, nullptr,
                             arguments, environ),
               0);

    const bool seen = vss::testkit::wait_until(
        [&] { return peer_open(paths)[0] == peer_open_state::open; }, 5000ms);
    ::kill(child, SIGKILL);
    int status = 0;
    ::waitpid(child, &status, 0);
    REQUIRE(seen);

    REQUIRE(vss::testkit::wait_until(
        [&] { return peer_open(paths)[0] == peer_open_state::not_open; },
        5000ms));
}
