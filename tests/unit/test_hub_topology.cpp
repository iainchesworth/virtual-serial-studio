//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "support/test_support.hpp"

#include <vss/core/hub.hpp>

#include <chrono>

using namespace vss;
using namespace vss::testkit;
using namespace vss_test_support;

namespace {
using namespace std::chrono_literals;

struct topology
{
    std::shared_ptr<vss::hub> hub = start_hub();
    mock_endpoint a;
    mock_endpoint b;
    mock_endpoint c;
};

void prepare(topology& fixture)
{
    fixture.a = add_mock(*fixture.hub, "a");
    fixture.b = add_mock(*fixture.hub, "b");
    fixture.c = add_mock(*fixture.hub, "c");
}

std::string written(const mock_endpoint& port)
{
    return port.written();
}
} // namespace

TEST_CASE("hub: endpoint registration validates its inputs")
{
    auto hub = start_hub();

    REQUIRE_EQ(hub->add_endpoint("", endpoint_kind::mock, nullptr),
               make_error_code(errc::invalid_argument));
    REQUIRE_EQ(hub->add_endpoint("x", endpoint_kind::mock, nullptr),
               make_error_code(errc::invalid_argument));

    auto port = add_mock(*hub, "x");
    REQUIRE_EQ(hub->add_endpoint("x", endpoint_kind::mock,
                                 loopback_transport::create(hub->executor(), "x")),
               make_error_code(errc::already_exists));

    REQUIRE_EQ(hub->endpoints().size(), std::size_t{1});
    REQUIRE_EQ(hub->endpoints().front()->id(), port.endpoint->id());
}

TEST_CASE("hub: lookup by id and name")
{
    topology fixture;
    prepare(fixture);

    REQUIRE_EQ(fixture.hub->find_endpoint("a")->id(), fixture.a.endpoint->id());
    REQUIRE_EQ(fixture.hub->find_endpoint(fixture.b.endpoint->id())->name(),
               std::string("b"));
    REQUIRE(fixture.hub->find_endpoint("missing") == nullptr);
    REQUIRE(fixture.hub->find_endpoint(endpoint_id{9999}) == nullptr);

    const auto all = fixture.hub->endpoints();
    REQUIRE_EQ(all.size(), std::size_t{3});
    REQUIRE_EQ(all[0]->name(), std::string("a"));
    REQUIRE_EQ(all[2]->name(), std::string("c"));
}

TEST_CASE("hub: rename validates uniqueness")
{
    topology fixture;
    prepare(fixture);

    REQUIRE(fixture.hub->rename_endpoint(fixture.a.endpoint->id(), "renamed"));
    REQUIRE_EQ(fixture.hub->find_endpoint("renamed")->id(),
               fixture.a.endpoint->id());
    // The endpoint reports the new name too, so events and info follow it.
    REQUIRE_EQ(fixture.a.endpoint->name(), std::string("renamed"));
    REQUIRE_EQ(fixture.a.endpoint->info().name, std::string("renamed"));
    REQUIRE(fixture.hub->find_endpoint("a") == nullptr);
    REQUIRE(fixture.hub->rename_endpoint(fixture.a.endpoint->id(), "again"));
    REQUIRE(fixture.hub->find_endpoint("renamed") == nullptr);
    REQUIRE_EQ(fixture.hub->find_endpoint("again")->id(),
               fixture.a.endpoint->id());
    REQUIRE(fixture.hub->rename_endpoint(fixture.a.endpoint->id(), "renamed"));

    REQUIRE_EQ(fixture.hub->rename_endpoint(fixture.a.endpoint->id(), "b"),
               make_error_code(errc::already_exists));
    REQUIRE_EQ(fixture.hub->rename_endpoint(fixture.a.endpoint->id(), ""),
               make_error_code(errc::invalid_argument));
    REQUIRE_EQ(fixture.hub->rename_endpoint(endpoint_id{9999}, "nope"),
               make_error_code(errc::not_found));
}

TEST_CASE("hub: duplex link forwards both ways")
{
    topology fixture;
    prepare(fixture);

    const auto link = fixture.hub->connect(fixture.a.endpoint->id(),
                                           fixture.b.endpoint->id());
    REQUIRE(link);

    const auto links = fixture.hub->links();
    REQUIRE_EQ(links.size(), std::size_t{1});
    REQUIRE_EQ(links.front().mode, link_mode::duplex);
    REQUIRE_EQ(links.front().id, *link);
    REQUIRE(fixture.hub->find_link(*link));

    fixture.a.inject_read("to-b");
    REQUIRE(wait_until([&] { return written(fixture.b) == "to-b"; }, 2000ms));

    fixture.b.inject_read("to-a");
    REQUIRE(wait_until([&] { return written(fixture.a) == "to-a"; }, 2000ms));

    const auto stats = fixture.hub->find_link(*link);
    REQUIRE(stats);
    REQUIRE(stats->stats.bytes >= 8);
    REQUIRE(stats->stats.messages >= 2);
}

TEST_CASE("hub: connect rejects unknown and self endpoints")
{
    topology fixture;
    prepare(fixture);

    REQUIRE_EQ(fixture.hub->connect(fixture.a.endpoint->id(), endpoint_id{7777}),
               make_error_code(errc::not_found));
    REQUIRE_EQ(fixture.hub->connect(fixture.a.endpoint->id(),
                                    fixture.a.endpoint->id()),
               make_error_code(errc::invalid_argument));
}

TEST_CASE("hub: cycles are rejected")
{
    topology fixture;
    prepare(fixture);

    REQUIRE(fixture.hub->connect(fixture.a.endpoint->id(),
                                 fixture.b.endpoint->id()));
    REQUIRE_EQ(fixture.hub->connect(fixture.b.endpoint->id(),
                                    fixture.a.endpoint->id()),
               make_error_code(errc::cycle_detected));

    // a -> b -> c is fine; c -> a closes the loop.
    REQUIRE(fixture.hub->connect(fixture.b.endpoint->id(),
                                 fixture.c.endpoint->id()));
    REQUIRE_EQ(fixture.hub->connect(fixture.c.endpoint->id(),
                                    fixture.a.endpoint->id()),
               make_error_code(errc::cycle_detected));
}

TEST_CASE("hub: split fans out to every listener")
{
    topology fixture;
    prepare(fixture);

    const auto link = fixture.hub->split(fixture.a.endpoint->id(),
                                         {fixture.b.endpoint->id(),
                                          fixture.c.endpoint->id()});
    REQUIRE(link);

    const auto info = fixture.hub->find_link(*link);
    REQUIRE(info);
    REQUIRE_EQ(info->sources.size(), std::size_t{1});
    REQUIRE_EQ(info->targets.size(), std::size_t{2});

    fixture.a.inject_read("broadcast");
    REQUIRE(wait_until([&] { return written(fixture.b) == "broadcast"; }, 2000ms));
    REQUIRE(wait_until([&] { return written(fixture.c) == "broadcast"; }, 2000ms));

    // Replies from either listener are merged back into the source.
    fixture.b.inject_read("reply-b");
    REQUIRE(wait_until([&] { return written(fixture.a) == "reply-b"; }, 2000ms));
    fixture.c.inject_read("reply-c");
    REQUIRE(wait_until([&] { return written(fixture.a) == "reply-breply-c"; },
                       2000ms));
}

TEST_CASE("hub: the listeners of a split do not hear each other")
{
    // Four-wire RS-422 and RS-485 multi-drop are modelled as a split: the master
    // reaches every device, the devices answer to the master alone. The user
    // guide says so, so this pins it.
    topology fixture;
    prepare(fixture);

    const auto link = fixture.hub->split(fixture.a.endpoint->id(),
                                         {fixture.b.endpoint->id(),
                                          fixture.c.endpoint->id()});
    REQUIRE(link);

    fixture.b.inject_read("from-b");
    REQUIRE(wait_until([&] { return written(fixture.a) == "from-b"; }, 2000ms));
    fixture.c.inject_read("from-c");
    REQUIRE(wait_until([&] { return written(fixture.a) == "from-bfrom-c"; },
                       2000ms));

    // Neither listener was written the other's answer, nor its own.
    REQUIRE_EQ(written(fixture.b), std::string());
    REQUIRE_EQ(written(fixture.c), std::string());

    // A one-way join back from the listeners to the source would close a loop.
    const auto back = fixture.hub->join({fixture.b.endpoint->id(),
                                         fixture.c.endpoint->id()},
                                        fixture.a.endpoint->id());
    REQUIRE(!back);
    REQUIRE_EQ(back.error(), make_error_code(errc::cycle_detected));
}

TEST_CASE("hub: split rejects an empty target list")
{
    topology fixture;
    prepare(fixture);
    REQUIRE_EQ(fixture.hub->split(fixture.a.endpoint->id(), {}),
               make_error_code(errc::invalid_argument));
}

TEST_CASE("hub: join merges publishers into one target")
{
    topology fixture;
    prepare(fixture);

    const auto link = fixture.hub->join(
        {fixture.a.endpoint->id(), fixture.b.endpoint->id()},
        fixture.c.endpoint->id());
    REQUIRE(link);

    const auto info = fixture.hub->find_link(*link);
    REQUIRE(info);
    REQUIRE_EQ(info->mode, link_mode::one_way);
    REQUIRE_EQ(info->sources.size(), std::size_t{2});

    fixture.a.inject_read("one");
    REQUIRE(wait_until([&] { return written(fixture.c) == "one"; }, 2000ms));
    fixture.b.inject_read("two");
    REQUIRE(wait_until([&] { return written(fixture.c) == "onetwo"; }, 2000ms));

    // One way: replies from the target do not reach the publishers.
    fixture.c.inject_read("reply");
    REQUIRE(wait_until(
        [&] { return fixture.c.endpoint->stats().bytes_rx == 5; }, 2000ms));
    REQUIRE_EQ(written(fixture.a), std::string());
}

TEST_CASE("hub: redirect is one way")
{
    topology fixture;
    prepare(fixture);

    REQUIRE(fixture.hub->redirect(fixture.a.endpoint->id(),
                                  fixture.b.endpoint->id()));

    fixture.a.inject_read("forward");
    REQUIRE(wait_until([&] { return written(fixture.b) == "forward"; }, 2000ms));

    fixture.b.inject_read("back");
    REQUIRE(wait_until(
        [&] { return fixture.b.endpoint->stats().bytes_rx == 4; }, 2000ms));
    REQUIRE_EQ(written(fixture.a), std::string());
}

TEST_CASE("hub: monitor does not drive modem lines")
{
    topology fixture;
    prepare(fixture);

    const auto link = fixture.hub->monitor(fixture.a.endpoint->id(),
                                           fixture.b.endpoint->id());
    REQUIRE(link);
    const auto info = fixture.hub->find_link(*link);
    REQUIRE(info);
    REQUIRE(!info->options.emulate_modem_lines);
    REQUIRE_EQ(info->mode, link_mode::one_way);
}

TEST_CASE("hub: disconnect removes the routes")
{
    topology fixture;
    prepare(fixture);

    const auto link = fixture.hub->connect(fixture.a.endpoint->id(),
                                           fixture.b.endpoint->id());
    REQUIRE(link);
    REQUIRE(fixture.hub->disconnect(*link));
    REQUIRE_EQ(fixture.hub->links().size(), std::size_t{0});
    REQUIRE_EQ(fixture.hub->disconnect(*link), make_error_code(errc::not_found));
}

TEST_CASE("hub: owner policy arbitrates publishers")
{
    topology fixture;
    prepare(fixture);

    link_options options;
    options.policy = merge_policy::owner;

    const auto link = fixture.hub->join(
        {fixture.a.endpoint->id(), fixture.b.endpoint->id()},
        fixture.c.endpoint->id(), options);
    REQUIRE(link);

    REQUIRE(fixture.hub->set_owner(*link, fixture.a.endpoint->id()));

    fixture.a.inject_read("owned");
    REQUIRE(wait_until([&] { return written(fixture.c) == "owned"; }, 2000ms));

    fixture.b.inject_read("rejected");
    REQUIRE(wait_until([&] {
        const auto info = fixture.hub->find_link(*link);
        return info && info->stats.dropped == 8;
    }, 2000ms));
    REQUIRE_EQ(written(fixture.c), std::string("owned"));

    // Releasing ownership lets the next publisher claim it.
    REQUIRE(fixture.hub->release_owner(*link));
    fixture.b.inject_read("now-mine");
    REQUIRE(wait_until([&] { return written(fixture.c) == "ownednow-mine"; },
                       2000ms));
    const auto info = fixture.hub->find_link(*link);
    REQUIRE(info);
    REQUIRE_EQ(info->owner, fixture.b.endpoint->id());

    REQUIRE_EQ(fixture.hub->set_owner(*link, fixture.c.endpoint->id()),
               make_error_code(errc::invalid_argument));
    REQUIRE_EQ(fixture.hub->set_owner(link_id{9999}, fixture.a.endpoint->id()),
               make_error_code(errc::not_found));
    REQUIRE_EQ(fixture.hub->release_owner(link_id{9999}),
               make_error_code(errc::not_found));
}

TEST_CASE("hub: removing an endpoint removes its links")
{
    topology fixture;
    prepare(fixture);

    REQUIRE(fixture.hub->connect(fixture.a.endpoint->id(),
                                 fixture.b.endpoint->id()));
    REQUIRE(fixture.hub->connect(fixture.b.endpoint->id(),
                                 fixture.c.endpoint->id()));

    REQUIRE(fixture.hub->remove_endpoint(fixture.b.endpoint->id()));
    REQUIRE_EQ(fixture.hub->links().size(), std::size_t{0});
    REQUIRE_EQ(fixture.hub->endpoints().size(), std::size_t{2});

    // The surviving endpoints still work.
    fixture.a.inject_read("alive");
    REQUIRE(wait_until(
        [&] { return fixture.a.endpoint->stats().bytes_rx == 5; }, 2000ms));
}

TEST_CASE("hub: routing empty data is a no-op")
{
    topology fixture;
    prepare(fixture);

    REQUIRE(fixture.hub->redirect(fixture.a.endpoint->id(),
                                  fixture.b.endpoint->id()));
    fixture.hub->route_data(fixture.a.endpoint->id(), bytes_view{});
    fixture.hub->route_data(endpoint_id{4242}, bytes_view{});
    REQUIRE_EQ(written(fixture.b), std::string());
}

TEST_CASE("hub: unknown link identifiers are handled")
{
    auto hub = start_hub();
    REQUIRE(!hub->find_link(link_id{4242}));
    REQUIRE_EQ(hub->disconnect(link_id{4242}), make_error_code(errc::not_found));
    REQUIRE_EQ(hub->set_owner(link_id{4242}, endpoint_id{1}),
               make_error_code(errc::not_found));
    REQUIRE_EQ(hub->release_owner(link_id{4242}),
               make_error_code(errc::not_found));
}

TEST_CASE("hub: start and stop are idempotent")
{
    auto hub = start_hub();
    REQUIRE(hub->running());
    hub->start();
    REQUIRE(hub->running());
    hub->stop();
    REQUIRE(!hub->running());
    hub->stop();
    REQUIRE(!hub->running());
    hub->join();

    // The hub can be started again after a full stop.
    hub->start();
    REQUIRE(hub->running());
    hub->stop();
    hub->join();
    REQUIRE(!hub->running());
}
