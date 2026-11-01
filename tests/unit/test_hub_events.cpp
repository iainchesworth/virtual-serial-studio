//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "support/test_support.hpp"

#include <chrono>

using namespace vss;
using namespace vss::testkit;
using namespace vss_test_support;

namespace {
using namespace std::chrono_literals;
}

TEST_CASE("events: filter by endpoint")
{
    auto hub = start_hub();
    auto first = add_mock(*hub, "first");
    auto second = add_mock(*hub, "second");

    event_recorder recorder;
    event_filter filter;
    filter.endpoint = first.endpoint->id();
    hub->subscribe(filter, [&](const endpoint_event& event) {
        recorder.record(event);
    });

    first.transport->mock()->inject_read("one");
    second.transport->mock()->inject_read("two");

    REQUIRE(wait_until(
        [&] { return recorder.data_ascii(data_direction::rx) == "one"; }, 2000ms));
    REQUIRE(wait_until(
        [&] { return second.endpoint->stats().bytes_rx == 3; }, 2000ms));

    for (const auto& event : recorder.snapshot())
        REQUIRE_EQ(event.endpoint, first.endpoint->id());
}

TEST_CASE("events: filter by kind")
{
    auto hub = start_hub();
    auto mock = add_mock(*hub, "device");

    event_recorder data_only;
    event_filter filter;
    filter.kind = event_kind::data;
    hub->subscribe(filter, [&](const endpoint_event& event) {
        data_only.record(event);
    });

    mock.transport->mock()->inject_read("payload");
    mock.endpoint->write(as_bytes("reply"));

    REQUIRE(wait_until([&] { return data_only.count(event_kind::data) == 2; },
                       2000ms));
    REQUIRE_EQ(data_only.count(event_kind::lines), std::size_t{0});
}

TEST_CASE("events: unsubscribe stops delivery")
{
    auto hub = start_hub();
    auto mock = add_mock(*hub, "device");

    event_recorder recorder;
    const subscriber_id id =
        hub->subscribe({}, [&](const endpoint_event& event) { recorder.record(event); });

    mock.transport->mock()->inject_read("first");
    REQUIRE(wait_until(
        [&] { return recorder.data_ascii(data_direction::rx) == "first"; }, 2000ms));

    hub->unsubscribe(id);
    mock.transport->mock()->inject_read("second");
    REQUIRE(wait_until(
        [&] { return mock.endpoint->stats().bytes_rx == 11; }, 2000ms));

    REQUIRE_EQ(recorder.data_ascii(data_direction::rx), std::string("first"));
}

TEST_CASE("events: a throwing subscriber does not break the hub")
{
    auto hub = start_hub();
    auto mock = add_mock(*hub, "device");

    hub->subscribe({}, [](const endpoint_event&) {
        throw std::runtime_error("subscriber failure");
    });

    event_recorder recorder;
    hub->subscribe({}, [&](const endpoint_event& event) { recorder.record(event); });

    mock.transport->mock()->inject_read("survives");
    REQUIRE(wait_until(
        [&] { return recorder.data_ascii(data_direction::rx) == "survives"; },
        2000ms));
}

TEST_CASE("events: the traffic ring records both directions")
{
    auto hub = start_hub();
    auto mock = add_mock(*hub, "device");

    mock.transport->mock()->inject_read("in");
    mock.endpoint->write(as_bytes("out"));

    REQUIRE(wait_until([&] { return hub->traffic().size() >= 2; }, 2000ms));

    const auto records = hub->traffic();
    bool saw_rx = false;
    bool saw_tx = false;
    for (const auto& record : records)
    {
        REQUIRE_EQ(record.name, std::string("device"));
        REQUIRE(record.data != nullptr);
        if (record.direction == data_direction::rx
            && to_string(bytes_view(*record.data)) == "in")
            saw_rx = true;
        if (record.direction == data_direction::tx
            && to_string(bytes_view(*record.data)) == "out")
            saw_tx = true;
    }
    REQUIRE(saw_rx);
    REQUIRE(saw_tx);

    hub->clear_traffic();
    REQUIRE_EQ(hub->traffic().size(), std::size_t{0});
}

TEST_CASE("events: explicit log messages are dispatched")
{
    auto hub = start_hub();

    event_recorder recorder;
    event_filter filter;
    filter.kind = event_kind::log_message;
    hub->subscribe(filter, [&](const endpoint_event& event) {
        recorder.record(event);
    });

    endpoint_event message;
    message.kind = event_kind::log_message;
    message.message = "hello";
    message.endpoint = 7;
    hub->publish(std::move(message));

    REQUIRE(wait_until(
        [&] { return recorder.count(event_kind::log_message) == 1; }, 2000ms));
    REQUIRE_EQ(recorder.snapshot().front().message, std::string("hello"));
}
