//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The studio's side of the vsscom driver. A hosted runner has no driver, so
// these cases pin the protocol the guest scripts and the driver share, and what
// the client reports when the driver is absent. The client against a real
// driver is exercised in the Windows guests.
//

#include <vss/ports/vcom_driver.hpp>

#include "test_framework.hpp"

#include <boost/asio/io_context.hpp>

#include <cstddef>
#include <cwchar>
#include <string>

#include <windows.h>
#include <winioctl.h>

#include <vsscom/public.h>

using namespace vss;

TEST_CASE("driver client: the control codes are the ones the guest scripts use")
{
    // test-driver.ps1 spells these out; a change here must change it too.
    REQUIRE_EQ(static_cast<unsigned long>(IOCTL_VSSCOM_GET_VERSION), 0x80002000ul);
    REQUIRE_EQ(static_cast<unsigned long>(IOCTL_VSSCOM_HUB_GET_STATE), 0x80002004ul);
    REQUIRE_EQ(static_cast<unsigned long>(IOCTL_VSSCOM_HUB_WAIT_STATE), 0x80002008ul);
    REQUIRE_EQ(static_cast<unsigned long>(IOCTL_VSSCOM_HUB_SET_LINES), 0x8000200Cul);
}

TEST_CASE("driver client: the hub state has the layout the driver fills")
{
    // Eleven 32-bit words, in the order the guest script reads them.
    REQUIRE_EQ(sizeof(VSSCOM_HUB_STATE), std::size_t{44});
    REQUIRE_EQ(offsetof(VSSCOM_HUB_STATE, Generation), std::size_t{0});
    REQUIRE_EQ(offsetof(VSSCOM_HUB_STATE, AppOpen), std::size_t{4});
    REQUIRE_EQ(offsetof(VSSCOM_HUB_STATE, Dtr), std::size_t{24});
    REQUIRE_EQ(offsetof(VSSCOM_HUB_STATE, Lines), std::size_t{36});
    REQUIRE_EQ(offsetof(VSSCOM_HUB_STATE, TxQueued), std::size_t{40});
    REQUIRE_EQ(sizeof(VSSCOM_VERSION_INFO), std::size_t{8});

    // The line bits are the modem status bits Windows uses.
    REQUIRE_EQ(static_cast<unsigned>(VSSCOM_LINE_MASK), 0xF0u);
    REQUIRE_EQ(static_cast<unsigned>(VSSCOM_LINE_CTS), static_cast<unsigned>(MS_CTS_ON));
    REQUIRE_EQ(static_cast<unsigned>(VSSCOM_LINE_DSR), static_cast<unsigned>(MS_DSR_ON));
    REQUIRE_EQ(static_cast<unsigned>(VSSCOM_LINE_RI), static_cast<unsigned>(MS_RING_ON));
    REQUIRE_EQ(static_cast<unsigned>(VSSCOM_LINE_DCD), static_cast<unsigned>(MS_RLSD_ON));
}

TEST_CASE("driver client: the control device has the codes and layout the guest scripts use")
{
    // test-driver.ps1 spells these out in CtlIo; a change here must change it too.
    REQUIRE_EQ(static_cast<unsigned long>(IOCTL_VSSCOM_CTL_GET_VERSION), 0x80002040ul);
    REQUIRE_EQ(static_cast<unsigned long>(IOCTL_VSSCOM_CTL_CREATE_PORT), 0x80002044ul);
    REQUIRE_EQ(static_cast<unsigned long>(IOCTL_VSSCOM_CTL_DESTROY_PORT), 0x80002048ul);
    REQUIRE_EQ(static_cast<unsigned long>(IOCTL_VSSCOM_CTL_LIST_PORTS), 0x8000204Cul);

    REQUIRE_EQ(sizeof(VSSCOM_CTL_VERSION_INFO), std::size_t{8});
    REQUIRE_EQ(sizeof(VSSCOM_CTL_CREATE), std::size_t{8});
    REQUIRE_EQ(sizeof(VSSCOM_CTL_PORT), std::size_t{4});
    REQUIRE_EQ(sizeof(VSSCOM_CTL_LIST_ENTRY), std::size_t{8});
    REQUIRE_EQ(offsetof(VSSCOM_CTL_LIST, Ports), std::size_t{4});
    REQUIRE_EQ(sizeof(VSSCOM_CTL_LIST),
               std::size_t{4 + 8 * VSSCOM_CTL_MAX_PORTS});
    REQUIRE_EQ(static_cast<int>(VSSCOM_CTL_MAX_PORTS), 32);
    REQUIRE_EQ(static_cast<int>(VSSCOM_CTL_VERSION), 1);
}

TEST_CASE("driver client: the instance id of a port made on demand is predictable")
{
    // The client looks for this device instance id after CREATE_PORT.
    wchar_t suffix[16]{};
    std::swprintf(suffix, 16, VSSCOM_PORT_INSTANCE_FORMAT, 10ul);
    REQUIRE(std::wstring(suffix) == L"000A");
    REQUIRE(std::wstring(VSSCOM_PORT_HARDWARE_ID) == L"VSSCOM\\PORT");
}

TEST_CASE("driver client: without the driver it says so and takes nothing")
{
    if (driver_available())
        return; // a machine with the driver installed: covered in the guests

    boost::asio::io_context io;
    REQUIRE(driver_version().empty());
    REQUIRE_EQ(create_driver_port(io.get_executor(), "x"),
               make_error_code(errc::unsupported));
}
