//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The pieces of the permissions layer that Linux and macOS share: asking about
// a serial device and running a read-only system command (the macOS Application
// Firewall query uses it). They run against the real system here, with commands
// that every Linux and macOS has.
//

#include "test_framework.hpp"

#include "../../../../libs/vss_ports/src/platform/posix/permissions_posix.hpp"

#include <chrono>
#include <string>

using namespace std::chrono_literals;
namespace posix = vss::detail::posix;

TEST_CASE("posix permissions: a command's output is captured")
{
    const auto text = posix::capture_command({"/bin/echo", "hello"}, 5s);
    REQUIRE(text.has_value());
    REQUIRE_EQ(*text, std::string("hello\n"));
}

TEST_CASE("posix permissions: a command that is not there gives nothing")
{
    REQUIRE(!posix::capture_command({"/nonexistent/tool"}, 2s).has_value());
    REQUIRE(!posix::capture_command({}, 2s).has_value());
}

TEST_CASE("posix permissions: a command that takes too long is stopped")
{
    const auto started = std::chrono::steady_clock::now();
    const auto text = posix::capture_command({"/bin/sleep", "30"}, 300ms);
    const auto took = std::chrono::steady_clock::now() - started;
    REQUIRE(!text.has_value());
    REQUIRE(took < 10s);
}

TEST_CASE("posix permissions: the command gets a small fixed environment")
{
    const auto text = posix::capture_command({"/usr/bin/env"}, 5s);
    REQUIRE(text.has_value());
    REQUIRE(text->find("LC_ALL=C") != std::string::npos);
    REQUIRE(text->find("PATH=/usr/bin:/bin:/usr/sbin:/sbin") !=
            std::string::npos);
    // Nothing of this program's environment leaks into the command.
    REQUIRE(text->find("HOME=") == std::string::npos);
}

TEST_CASE("posix permissions: a device is described without opening it")
{
    const auto groups = posix::process_groups();
    const auto null_device =
        posix::observe_device("/dev/null", posix::user_name(), groups);
    REQUIRE(null_device.exists);
    REQUIRE(null_device.permitted == vss::permissions::tri::yes);

    const auto missing = posix::observe_device("/dev/does-not-exist",
                                               posix::user_name(), groups);
    REQUIRE(!missing.exists);
}
