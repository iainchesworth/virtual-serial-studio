//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <vss/ports/com0com.hpp>
#include <vss/ports/loopback_transport.hpp>
#include <vss/ports/native_lines.hpp>
#include <vss/ports/process_control.hpp>
#include <vss/ports/vcom_driver.hpp>
#include <vss/ports/virtual_port.hpp>

#include "test_framework.hpp"

#include <boost/asio/io_context.hpp>

#include <windows.h>

#include <cstdint>
#include <string>

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

TEST_CASE("process control: this process is described and cannot be ended")
{
    const auto self =
        describe_process(static_cast<std::uint32_t>(::GetCurrentProcessId()));
    REQUIRE(self.has_value());
    REQUIRE(self->own_user);
    REQUIRE(self->can_end);
    REQUIRE(!self->name.empty());
    REQUIRE(self->name.find("test") != std::string::npos);

    // A program is never asked to end itself, nor process 0.
    REQUIRE_EQ(terminate_process(static_cast<std::uint32_t>(::GetCurrentProcessId())),
               make_error_code(errc::invalid_argument));
    REQUIRE_EQ(terminate_process(0), make_error_code(errc::invalid_argument));
    REQUIRE(!describe_process(0).has_value());
}

TEST_CASE("process control: another process of the same user is described and ended")
{
    // A child of this test that only waits, so ending it harms nothing.
    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    char command[] = "cmd.exe /c ping -n 30 127.0.0.1";
    REQUIRE(::CreateProcessA(nullptr, command, nullptr, nullptr, FALSE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                             &child) != 0);
    const auto id = static_cast<std::uint32_t>(child.dwProcessId);

    const auto summary = describe_process(id);
    REQUIRE(summary.has_value());
    REQUIRE(summary->own_user);
    REQUIRE(summary->can_end);
    REQUIRE_EQ(summary->id, id);

    REQUIRE(terminate_process(id).has_value());
    REQUIRE(::WaitForSingleObject(child.hProcess, 10000) == WAIT_OBJECT_0);
    ::CloseHandle(child.hThread);
    ::CloseHandle(child.hProcess);

    // Once it has gone, it is not found.
    REQUIRE(!describe_process(id).has_value());
    const auto again = terminate_process(id);
    REQUIRE(!again.has_value());
    REQUIRE(again.error() == make_error_code(errc::not_found) ||
            again.error() == make_error_code(errc::permission_denied));
}

TEST_CASE("driver client: revoking a transport that is not a driver port is refused")
{
    boost::asio::io_context io;
    auto loopback = loopback_transport::create(io.get_executor(), "x");
    const auto revoked = revoke_driver_port(*loopback);
    REQUIRE(!revoked.has_value());
    REQUIRE_EQ(revoked.error(), make_error_code(errc::invalid_argument));
}
