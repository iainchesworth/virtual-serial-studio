//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "support/test_support.hpp"

#include <vss/core/hub.hpp>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace vss;
using namespace vss::testkit;
using namespace vss_test_support;

namespace
{
using namespace std::chrono_literals;

link_options latency(std::uint32_t delay_ms, std::uint32_t jitter_ms = 0)
{
    link_options options;
    options.delay_ms = delay_ms;
    options.jitter_ms = jitter_ms;
    return options;
}

struct latency_fixture
{
    std::shared_ptr<vss::hub> hub = start_hub();
    mock_endpoint a = add_mock(*hub, "a");
    mock_endpoint b = add_mock(*hub, "b");
    mock_endpoint c = add_mock(*hub, "c");
};

} // namespace

TEST_CASE("latency: a link with no delay forwards at once, as before")
{
    latency_fixture fixture;
    REQUIRE(fixture.hub->connect(fixture.a.endpoint->id(),
                                 fixture.b.endpoint->id()));

    fixture.a.inject_read("now");
    REQUIRE(wait_until([&] { return fixture.b.written() == "now"; }, 5000ms));
}

TEST_CASE("latency: bytes arrive no sooner than the delay")
{
    latency_fixture fixture;
    const auto link = fixture.hub->connect(
        fixture.a.endpoint->id(), fixture.b.endpoint->id(), latency(200));
    REQUIRE(link);

    const auto sent = std::chrono::steady_clock::now();
    fixture.a.inject_read("hello");

    // Well inside the delay nothing has arrived.
    std::this_thread::sleep_for(60ms);
    REQUIRE_EQ(fixture.b.written(), std::string());

    REQUIRE(wait_until([&] { return fixture.b.written() == "hello"; }, 5000ms));
    const auto arrived = std::chrono::steady_clock::now() - sent;
    REQUIRE(arrived >= 200ms);

    // The link reports what it was made with.
    const auto info = fixture.hub->find_link(*link);
    REQUIRE(info);
    REQUIRE_EQ(info->options.delay_ms, std::uint32_t{200});
    REQUIRE_EQ(info->options.jitter_ms, std::uint32_t{0});
}

TEST_CASE("latency: a duplex link delays both directions")
{
    latency_fixture fixture;
    REQUIRE(fixture.hub->connect(fixture.a.endpoint->id(),
                                 fixture.b.endpoint->id(), latency(120)));

    const auto start = std::chrono::steady_clock::now();
    fixture.a.inject_read("ping");
    REQUIRE(wait_until([&] { return fixture.b.written() == "ping"; }, 5000ms));
    fixture.b.inject_read("pong");
    REQUIRE(wait_until([&] { return fixture.a.written() == "pong"; }, 5000ms));

    // A round trip takes both delays.
    REQUIRE(std::chrono::steady_clock::now() - start >= 240ms);
}

TEST_CASE("latency: bytes keep their order, whatever the jitter")
{
    latency_fixture fixture;
    REQUIRE(fixture.hub->connect(fixture.a.endpoint->id(),
                                 fixture.b.endpoint->id(), latency(30, 30)));

    std::string sent;
    for (int index = 0; index < 150; ++index)
    {
        const std::string chunk = std::to_string(index) + ",";
        sent += chunk;
        fixture.a.inject_read(chunk);
    }

    REQUIRE(wait_until([&] { return fixture.b.written().size() == sent.size(); },
                       10000ms));
    REQUIRE_EQ(fixture.b.written(), sent);
}

TEST_CASE("latency: each chunk is held for the delay give or take the jitter")
{
    latency_fixture fixture;
    REQUIRE(fixture.hub->connect(fixture.a.endpoint->id(),
                                 fixture.b.endpoint->id(), latency(80, 40)));

    // One chunk at a time, so each has its own draw and nothing ahead of it.
    for (int round = 0; round < 6; ++round)
    {
        const std::string expected = fixture.b.written() + "x";
        const auto sent = std::chrono::steady_clock::now();
        fixture.a.inject_read("x");
        REQUIRE(wait_until([&] { return fixture.b.written() == expected; },
                           5000ms));
        // At least delay minus jitter.  The upper bound depends on how busy
        // the machine is, so it is not asserted.
        REQUIRE(std::chrono::steady_clock::now() - sent >= 40ms);
    }
}

TEST_CASE("latency: a delayed link does not slow another link")
{
    latency_fixture fixture;
    REQUIRE(fixture.hub->connect(fixture.a.endpoint->id(),
                                 fixture.b.endpoint->id(), latency(400)));
    REQUIRE(fixture.hub->connect(fixture.b.endpoint->id(),
                                 fixture.c.endpoint->id()));

    fixture.b.inject_read("fast");
    REQUIRE(wait_until([&] { return fixture.c.written() == "fast"; }, 5000ms));
    // The same bytes went to a on the delayed link and are still on the way.
    REQUIRE_EQ(fixture.a.written(), std::string());
    REQUIRE(wait_until([&] { return fixture.a.written() == "fast"; }, 5000ms));
}

TEST_CASE("latency: a split delays every target")
{
    latency_fixture fixture;
    REQUIRE(fixture.hub->split(
        fixture.a.endpoint->id(),
        {fixture.b.endpoint->id(), fixture.c.endpoint->id()}, latency(100)));

    const auto sent = std::chrono::steady_clock::now();
    fixture.a.inject_read("all");
    REQUIRE(wait_until(
        [&] {
            return fixture.b.written() == "all" && fixture.c.written() == "all";
        },
        5000ms));
    REQUIRE(std::chrono::steady_clock::now() - sent >= 100ms);
}

TEST_CASE("latency: a bus delays what each member hears")
{
    latency_fixture fixture;
    const auto bus = fixture.hub->bus(
        {fixture.a.endpoint->id(), fixture.b.endpoint->id(),
         fixture.c.endpoint->id()},
        latency(150));
    REQUIRE(bus);

    const auto info = fixture.hub->find_link(*bus);
    REQUIRE(info);
    REQUIRE_EQ(info->options.delay_ms, std::uint32_t{150});
    REQUIRE(!info->options.emulate_modem_lines);

    const auto sent = std::chrono::steady_clock::now();
    fixture.a.inject_read("poll");
    std::this_thread::sleep_for(50ms);
    REQUIRE_EQ(fixture.b.written(), std::string());
    REQUIRE_EQ(fixture.c.written(), std::string());

    REQUIRE(wait_until(
        [&] {
            return fixture.b.written() == "poll" &&
                   fixture.c.written() == "poll";
        },
        5000ms));
    REQUIRE(std::chrono::steady_clock::now() - sent >= 150ms);
    // The sender never hears itself, delayed or not.
    REQUIRE_EQ(fixture.a.written(), std::string());

    // A reply is delayed in turn, in order.
    fixture.b.inject_read("r1");
    fixture.b.inject_read("r2");
    REQUIRE(wait_until([&] { return fixture.a.written() == "r1r2"; }, 5000ms));
    REQUIRE(wait_until([&] { return fixture.c.written() == "pollr1r2"; },
                       5000ms));
}

TEST_CASE("latency: removing the link drops what is still on the way")
{
    latency_fixture fixture;
    const auto link = fixture.hub->connect(
        fixture.a.endpoint->id(), fixture.b.endpoint->id(), latency(200));
    REQUIRE(link);

    fixture.a.inject_read("lost");
    REQUIRE(fixture.hub->disconnect(*link));

    std::this_thread::sleep_for(350ms);
    REQUIRE_EQ(fixture.b.written(), std::string());
}

TEST_CASE("latency: removing the target leaves the rest of the topology alone")
{
    latency_fixture fixture;
    REQUIRE(fixture.hub->connect(fixture.a.endpoint->id(),
                                 fixture.b.endpoint->id(), latency(100)));
    REQUIRE(fixture.hub->connect(fixture.a.endpoint->id(),
                                 fixture.c.endpoint->id(), latency(100)));

    fixture.a.inject_read("some");
    REQUIRE(fixture.hub->remove_endpoint(fixture.b.endpoint->id()));

    REQUIRE(wait_until([&] { return fixture.c.written() == "some"; }, 5000ms));
}

TEST_CASE("latency: the delay and jitter are checked")
{
    latency_fixture fixture;
    const auto a = fixture.a.endpoint->id();
    const auto b = fixture.b.endpoint->id();
    const auto c = fixture.c.endpoint->id();
    const auto invalid = make_error_code(errc::invalid_argument);

    // Jitter over the delay, and a delay over the limit.
    const auto jittery = fixture.hub->connect(a, b, latency(10, 11));
    REQUIRE(!jittery);
    REQUIRE_EQ(jittery.error(), invalid);
    REQUIRE(!fixture.hub->connect(a, b, latency(0, 1)));
    REQUIRE(!fixture.hub->connect(a, b, latency(max_link_delay_ms + 1)));
    REQUIRE(!fixture.hub->split(a, {b, c}, latency(5, 6)));
    REQUIRE(!fixture.hub->join({a, b}, c, latency(5, 6)));
    REQUIRE(!fixture.hub->redirect(a, b, latency(5, 6)));
    const auto bus = fixture.hub->bus({a, b}, latency(5, 6));
    REQUIRE(!bus);
    REQUIRE_EQ(bus.error(), invalid);

    // Nothing was added by the refusals.
    REQUIRE(fixture.hub->links().empty());

    // The limits themselves are allowed.
    REQUIRE(fixture.hub->connect(
        a, b, latency(max_link_delay_ms, max_link_delay_ms)));
}
