//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <vss/ports/com0com.hpp>
#include <vss/ports/native_lines.hpp>
#include <vss/ports/virtual_port.hpp>

#include "test_framework.hpp"

#include <boost/asio/io_context.hpp>

using namespace vss;

TEST_CASE("native lines: peer driven inputs are unsupported on Windows")
{
    // Windows refuses to drive CTS/DSR/DCD/RI: they belong to the device.
    for (const auto line : {control_line::cts, control_line::dsr,
                            control_line::dcd, control_line::ri})
    {
        const auto result =
            native::write_line(static_cast<std::intptr_t>(1234), line, true);
        REQUIRE_EQ(result, make_error_code(errc::unsupported));
    }
}

TEST_CASE("com0com: pair management validates its arguments")
{
    com0com_installation fake;
    fake.found = true;
    fake.setupc_path = "does-not-exist";

    REQUIRE_EQ(create_com0com_pair(fake, "", "COM11", false),
               make_error_code(errc::invalid_argument));
    REQUIRE_EQ(create_com0com_pair(fake, "COM10", "COM10", false),
               make_error_code(errc::invalid_argument));
    REQUIRE(!create_com0com_pair(fake, "COM10", "COM11", false));
    REQUIRE_EQ(remove_com0com_pair(fake, -1, false),
               make_error_code(errc::invalid_argument));
}

TEST_CASE("virtual ports: pseudo terminals do not exist on Windows")
{
    REQUIRE(!supports(virtual_port_kind::pty));

    boost::asio::io_context io;
    REQUIRE_EQ(
        create_virtual_port(io.get_executor(), virtual_port_kind::pty, "x"),
        make_error_code(errc::unsupported));
}
