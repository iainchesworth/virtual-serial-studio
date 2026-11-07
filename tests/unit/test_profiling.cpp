//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The run-time switch for the Tracy instrumentation: option parsing, the
// states, and the guarantee that a build which has not been asked to profile
// starts nothing. Nothing here starts the profiler, so an ordinary test run
// opens no port.
//

#include "test_framework.hpp"

#include <vss/core/profile.hpp>
#include <vss/core/profiling.hpp>

#include <string>

namespace
{

using vss::profiling::request;
using vss::profiling::take_argument;

} // namespace

TEST_CASE("profiling: the command line options are recognised")
{
    request wanted;
    REQUIRE(!wanted.enable);

    REQUIRE(take_argument("--profile", wanted));
    REQUIRE(wanted.enable);
    REQUIRE_EQ(wanted.wanted.port, 0);
    REQUIRE(!wanted.wanted.allow_remote);

    request ported;
    REQUIRE(take_argument("--profile=9000", ported));
    REQUIRE(ported.enable);
    REQUIRE_EQ(ported.wanted.port, 9000);

    request remote;
    REQUIRE(take_argument("--profile-remote", remote));
    REQUIRE(remote.enable);
    REQUIRE(remote.wanted.allow_remote);

    request waiting;
    REQUIRE(take_argument("--profile-wait=15", waiting));
    REQUIRE(waiting.enable);
    REQUIRE_EQ(waiting.wait.count(), 15);

    // --no-profile wins over what came before it.
    REQUIRE(take_argument("--no-profile", wanted));
    REQUIRE(!wanted.enable);
}

TEST_CASE("profiling: options it does not own are left alone")
{
    request untouched;
    REQUIRE(!take_argument("--profiles", untouched));
    REQUIRE(!take_argument("--profile=", untouched));
    REQUIRE(!take_argument("--profile=0", untouched));
    REQUIRE(!take_argument("--profile=70000", untouched));
    REQUIRE(!take_argument("--profile=http", untouched));
    REQUIRE(!take_argument("--profile-wait=soon", untouched));
    REQUIRE(!take_argument("run", untouched));
    REQUIRE(!untouched.enable);
}

TEST_CASE("profiling: a program that has not been asked is dormant")
{
    // The test runner starts profiling only when the run asks for it, so
    // without that the state is dormant (or not compiled), with no port.
    const auto before = vss::profiling::current();
    if (before.current == vss::profiling::state::recording)
        VSS_SKIP("this run is being profiled");

    REQUIRE(before.port == 0 ||
            before.current != vss::profiling::state::dormant);
    if (!vss::profiling::compiled_in)
    {
        REQUIRE(before.current == vss::profiling::state::not_compiled);
        const auto refused = vss::profiling::enable();
        REQUIRE(!refused);
        REQUIRE(refused.error() == vss::errc::unsupported);
    }
}

TEST_CASE("profiling: a zone, a plot and a message are harmless when dormant")
{
    // These must compile and do nothing when nothing is recording.
    VSS_PROFILE_ZONE("test.profiling.zone");
    VSS_PROFILE_ZONE_NAMED(named, "test.profiling.named");
    VSS_PROFILE_ZONE_TEXT(named, std::string("text"));
    VSS_PROFILE_ZONE_NAME(named, std::string("renamed"));
    VSS_PROFILE_ZONE_VALUE(named, 7);
    VSS_PROFILE_PLOT("test.profiling.plot", 3);
    VSS_PROFILE_PLOT_F("test.profiling.plot_f", 1.5);
    VSS_PROFILE_MESSAGE("test.profiling.message");
    VSS_PROFILE_MESSAGE_TEXT(std::string("test.profiling.text"));
    VSS_PROFILE_FRAME_NAMED("test.profiling.frame");

    vss::profiling::depth_counter depth;
    const auto up = depth.up();
    const auto down = depth.down();
    if (vss::profiling::compiled_in)
    {
        REQUIRE_EQ(up, 1);
        REQUIRE_EQ(down, 0);
    }
}

TEST_CASE("profiling: every state has a name and a sentence")
{
    using vss::profiling::state;
    REQUIRE_EQ(std::string(vss::profiling::to_string(state::dormant)),
               std::string("dormant"));
    REQUIRE_EQ(std::string(vss::profiling::to_string(state::recording)),
               std::string("recording"));

    vss::profiling::status listening;
    listening.current = state::recording;
    listening.port = 8086;
    const std::string local = vss::profiling::describe(listening);
    REQUIRE(local.find("127.0.0.1 port 8086") != std::string::npos);
    REQUIRE(local.find("this computer only") != std::string::npos);

    listening.remote = true;
    REQUIRE(vss::profiling::describe(listening).find(
                "all network interfaces") != std::string::npos);

    listening.current = state::paused;
    REQUIRE(vss::profiling::describe(listening).find("stays open") !=
            std::string::npos);
    REQUIRE(!std::string(vss::profiling::usage()).empty());
}
