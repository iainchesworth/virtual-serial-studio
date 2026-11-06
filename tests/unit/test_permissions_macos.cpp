//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the permissions layer says about macOS: the Application Firewall, the
// Local Network permission, the App Sandbox and serial devices. The
// observations are injected, so every outcome is checked on every operating
// system.
//

#include "test_framework.hpp"

#include <vss/ports/permissions.hpp>

#include <string>
#include <utility>
#include <vector>

using namespace vss::permissions;

namespace
{

const fact* find(const std::vector<fact>& facts, topic what)
{
    for (const fact& each : facts)
    {
        if (each.what == what)
            return &each;
    }
    return nullptr;
}

context listening_on(std::string host, std::uint16_t port)
{
    context ask;
    ask.program_path = "/Applications/vss_gui.app/Contents/MacOS/vss_gui";
    listener each;
    each.host = std::move(host);
    each.port = port;
    ask.listeners.push_back(each);
    return ask;
}

observation mac_with(tri firewall_on, tri block_all, int darwin = 25)
{
    observation seen;
    seen.macos_host.user = "alice";
    seen.macos_host.firewall_on = firewall_on;
    seen.macos_host.block_all = block_all;
    seen.macos_host.darwin_major = darwin;
    seen.macos_host.sandboxed = tri::no;
    return seen;
}

} // namespace

TEST_CASE("macos permissions: the Application Firewall output is read as macOS "
          "prints it")
{
    tri on = tri::unknown;
    tri block = tri::unknown;
    parse_alf_global_state("Firewall is enabled. (State = 1)\n", on, block);
    REQUIRE(on == tri::yes);
    REQUIRE(block == tri::no);

    parse_alf_global_state("Firewall is enabled. (State = 2)", on, block);
    REQUIRE(on == tri::yes);
    REQUIRE(block == tri::yes);

    parse_alf_global_state("Firewall is disabled. (State = 0)", on, block);
    REQUIRE(on == tri::no);

    // Anything else is not guessed at.
    parse_alf_global_state("", on, block);
    REQUIRE(on == tri::unknown);
    REQUIRE(block == tri::unknown);
    parse_alf_global_state("Firewall is enabled. (State = x)", on, block);
    REQUIRE(on == tri::unknown);
    parse_alf_global_state("sudo: a password is required", on, block);
    REQUIRE(on == tri::unknown);

    REQUIRE(parse_alf_block_all("Block all ENABLED!") == tri::yes);
    REQUIRE(parse_alf_block_all("Block all DISABLED!") == tri::no);
    REQUIRE(parse_alf_block_all("") == tri::unknown);
}

TEST_CASE("macos permissions: the kernel release says whether the Local "
          "Network permission exists")
{
    REQUIRE_EQ(darwin_major_from_release("24.1.0"), 24);
    REQUIRE_EQ(darwin_major_from_release("25.0.0"), 25);
    REQUIRE_EQ(darwin_major_from_release("23"), 23);
    REQUIRE_EQ(darwin_major_from_release(""), 0);
    REQUIRE_EQ(darwin_major_from_release("x.y"), 0);
}

TEST_CASE("macos permissions: a listener on this computer only needs no "
          "firewall answer")
{
    const auto facts = evaluate(mac_with(tri::yes, tri::no),
                                listening_on("127.0.0.1", 4001), "macos");
    const fact* firewall = find(facts, topic::macos_firewall);
    REQUIRE(firewall->status == state::ok);
    REQUIRE_EQ(firewall->why, std::string(reason::loopback_only));
    REQUIRE(find(facts, topic::macos_local_network) == nullptr);
}

TEST_CASE("macos permissions: with the firewall on, macOS asks once and the "
          "fix for a no is a command")
{
    const auto facts = evaluate(mac_with(tri::yes, tri::no),
                                listening_on("0.0.0.0", 4001), "macos");
    const fact* firewall = find(facts, topic::macos_firewall);
    REQUIRE(firewall->status == state::info);
    REQUIRE_EQ(firewall->why, std::string(reason::alf_prompt));
    REQUIRE_EQ(firewall->port, std::uint16_t{4001});
    REQUIRE(firewall->remedy.find("socketfilterfw --add") != std::string::npos);
    REQUIRE(firewall->remedy.find("--unblockapp") != std::string::npos);
    REQUIRE(firewall->remedy.find(
                "/Applications/vss_gui.app/Contents/MacOS/vss_gui") !=
            std::string::npos);
    // Studio changes nothing on macOS.
    REQUIRE(!firewall->can_act);
    REQUIRE(!firewall->step.has_value());
}

TEST_CASE("macos permissions: firewall off, block-all and unreadable")
{
    auto facts = evaluate(mac_with(tri::no, tri::no),
                          listening_on("0.0.0.0", 4001), "macos");
    REQUIRE_EQ(find(facts, topic::macos_firewall)->why,
               std::string(reason::firewall_off));

    facts = evaluate(mac_with(tri::yes, tri::yes),
                     listening_on("0.0.0.0", 4001), "macos");
    REQUIRE(find(facts, topic::macos_firewall)->status == state::attention);
    REQUIRE_EQ(find(facts, topic::macos_firewall)->why,
               std::string(reason::block_all_inbound));

    facts = evaluate(mac_with(tri::unknown, tri::unknown),
                     listening_on("0.0.0.0", 4001), "macos");
    REQUIRE(find(facts, topic::macos_firewall)->status == state::unknown);
    REQUIRE_EQ(find(facts, topic::macos_firewall)->why,
               std::string(reason::not_asked));
}

TEST_CASE("macos permissions: Local Network exists from macOS 15 and cannot be "
          "asked")
{
    auto facts = evaluate(mac_with(tri::no, tri::no, 24),
                          listening_on("0.0.0.0", 4001), "macos");
    const fact* network = find(facts, topic::macos_local_network);
    REQUIRE(network != nullptr);
    REQUIRE(network->status == state::info);
    REQUIRE_EQ(network->why, std::string(reason::local_network_prompt));

    // macOS 14 and older have no such permission.
    facts = evaluate(mac_with(tri::no, tri::no, 23),
                     listening_on("0.0.0.0", 4001), "macos");
    REQUIRE(find(facts, topic::macos_local_network) == nullptr);
}

TEST_CASE("macos permissions: the App Sandbox is information with the "
          "entitlements to set")
{
    auto seen = mac_with(tri::no, tri::no);
    auto facts = evaluate(seen, {}, "macos");
    REQUIRE_EQ(find(facts, topic::confinement)->why,
               std::string(reason::unconfined));

    seen.macos_host.sandboxed = tri::yes;
    facts = evaluate(seen, {}, "macos");
    const fact* sandbox = find(facts, topic::confinement);
    REQUIRE(sandbox->status == state::info);
    REQUIRE_EQ(sandbox->why, std::string(reason::macos_sandbox));
    REQUIRE(sandbox->remedy.find("com.apple.security.device.serial") !=
            std::string::npos);
    REQUIRE(sandbox->remedy.find("com.apple.security.network.server") !=
            std::string::npos);
}

TEST_CASE("macos permissions: a serial device is open, or the mode or the "
          "sandbox is the reason")
{
    auto seen = mac_with(tri::no, tri::no);
    device_access open_device;
    open_device.path = "/dev/cu.usbserial-1";
    open_device.exists = true;
    open_device.group = "wheel";
    open_device.in_group = tri::no;
    open_device.listed_in_group = tri::no;
    open_device.group_may_write = tri::yes;
    open_device.permitted = tri::yes;
    seen.macos_host.devices = {open_device};

    context ask;
    ask.device_paths = {"/dev/cu.usbserial-1"};
    auto facts = evaluate(seen, ask, "macos");
    REQUIRE(find(facts, topic::serial_access)->status == state::ok);

    // The mode forbids it. There is no "dialout" on macOS and no group step.
    seen.macos_host.devices[0].permitted = tri::no;
    facts = evaluate(seen, ask, "macos");
    const fact* serial = find(facts, topic::serial_access);
    REQUIRE_EQ(serial->why, std::string(reason::mode_denies));
    REQUIRE_EQ(serial->remedy, std::string("ls -l /dev/cu.usbserial-1"));
    REQUIRE(!serial->group_step.has_value());

    // The mode allows it and an open was refused: the sandbox is the reason.
    seen.macos_host.devices[0].permitted = tri::yes;
    seen.macos_host.sandboxed = tri::yes;
    ask.refused_paths = {"/dev/cu.usbserial-1"};
    facts = evaluate(seen, ask, "macos");
    serial = find(facts, topic::serial_access);
    REQUIRE_EQ(serial->why, std::string(reason::denied_by_policy));
    REQUIRE(serial->remedy.find("com.apple.security.device.serial") !=
            std::string::npos);
}
