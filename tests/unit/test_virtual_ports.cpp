//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <vss/ports/com0com.hpp>
#include <vss/ports/serial_enumeration.hpp>
#include <vss/ports/vcom_driver.hpp>
#include <vss/ports/virtual_port.hpp>

#include "test_framework.hpp"

#include <boost/asio/io_context.hpp>

#include <algorithm>
#include <string>

using namespace vss;

TEST_CASE("virtual ports: the Windows driver hooks stay honest")
{
    // Supports must mirror the driver probe, so a missing driver is reported
    // rather than discovered halfway through creating a port.
    REQUIRE_EQ(supports(virtual_port_kind::windows_driver), driver_available());
    REQUIRE_EQ(std::string(to_string(virtual_port_kind::windows_driver)),
               std::string("windows-driver"));

    boost::asio::io_context io;
    if (!driver_available())
    {
        REQUIRE(driver_version().empty());
        REQUIRE_EQ(create_virtual_port(io.get_executor(),
                                       virtual_port_kind::windows_driver, "x"),
                   make_error_code(errc::unsupported));

        const auto kinds = available_kinds();
        REQUIRE(std::find(kinds.begin(), kinds.end(),
                          virtual_port_kind::windows_driver) == kinds.end());
    }
}

TEST_CASE("virtual ports: platform support matrix")
{
    REQUIRE(supports(virtual_port_kind::loopback));
    REQUIRE(!supports(virtual_port_kind::named_pipe));

    const auto kinds = available_kinds();
    REQUIRE(!kinds.empty());
    REQUIRE(std::find(kinds.begin(), kinds.end(),
                      virtual_port_kind::loopback) != kinds.end());

    REQUIRE_EQ(std::string(to_string(virtual_port_kind::pty)),
               std::string("pty"));
    REQUIRE_EQ(std::string(to_string(virtual_port_kind::named_pipe)),
               std::string("named-pipe"));
    REQUIRE_EQ(std::string(to_string(virtual_port_kind::loopback)),
               std::string("loopback"));
}

TEST_CASE("virtual ports: loopback creation")
{
    boost::asio::io_context io;
    const auto created = create_virtual_port(
        io.get_executor(), virtual_port_kind::loopback, "unit");
    REQUIRE(created);
    REQUIRE(created->port != nullptr);
    REQUIRE_EQ(created->kind, virtual_port_kind::loopback);
    REQUIRE_EQ(created->path, std::string("loopback:unit"));
    REQUIRE(created->port->is_open());
    REQUIRE(created->port->supports_control_lines());
}

TEST_CASE("virtual ports: named pipes report unsupported")
{
    boost::asio::io_context io;
    REQUIRE_EQ(create_virtual_port(io.get_executor(),
                                   virtual_port_kind::named_pipe, "x"),
               make_error_code(errc::unsupported));
}

TEST_CASE("virtual ports: best kind is always available")
{
    boost::asio::io_context io;
    const auto created = create_best_virtual_port(io.get_executor(), "best");
    REQUIRE(created);
    REQUIRE(created->port->is_open());
    REQUIRE(!created->path.empty());
}

TEST_CASE("enumeration: normalise sorts and removes duplicates")
{
    std::vector<serial_device_info> devices(5);
    devices[0].path = "COM5";
    devices[1].path = "COM3";
    devices[2].path = "COM3";
    devices[3].path = "";
    devices[4].path = "COM1";

    const auto normalised = normalise(devices);
    REQUIRE_EQ(normalised.size(), std::size_t{3});
    REQUIRE_EQ(normalised[0].path, std::string("COM1"));
    REQUIRE_EQ(normalised[1].path, std::string("COM3"));
    REQUIRE_EQ(normalised[2].path, std::string("COM5"));
}

TEST_CASE("enumeration: com0com ends are grouped into pairs")
{
    std::vector<serial_device_info> devices(5);

    devices[0].path = "COM3";
    devices[0].com0com = true;
    devices[0].com0com_pair = 1;
    devices[0].com0com_side = "A";

    devices[1].path = "COM5";
    devices[1].com0com = true;
    devices[1].com0com_pair = 1;
    devices[1].com0com_side = "B";

    devices[2].path = "COM7";
    devices[2].com0com = true;
    devices[2].com0com_pair = 2;
    devices[2].com0com_side = "A";

    devices[3].path = "COM9";
    devices[3].com0com = false;

    devices[4].path = "COM11";
    devices[4].com0com = true;
    devices[4].com0com_pair = -1;

    const auto pairs = group_com0com_pairs(devices);
    REQUIRE_EQ(pairs.size(), std::size_t{1});
    REQUIRE_EQ(pairs.front().index, 1);
    REQUIRE_EQ(pairs.front().a, std::string("COM3"));
    REQUIRE_EQ(pairs.front().b, std::string("COM5"));
}

TEST_CASE("enumeration: the machine can be enumerated")
{
    const auto devices = enumerate_serial_devices();
    for (const auto& device : devices)
        REQUIRE(!device.path.empty());

    // Enumerating twice must be stable.
    REQUIRE_EQ(enumerate_serial_devices().size(), devices.size());

    const auto names = enumerate_serial_port_names();
    REQUIRE_EQ(names.size(), devices.size());
}
