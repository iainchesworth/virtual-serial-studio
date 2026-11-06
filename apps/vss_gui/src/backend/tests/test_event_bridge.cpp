//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "backend/engine_studio_backend.hpp"
#include "backend/event_bridge.hpp"

#include "qt_wait.hpp"
#include "test_framework.hpp"

#include <vss/core/hub.hpp>
#include <vss/ports/peer_open.hpp>
#include <vss/studio/studio.hpp>

#include <QByteArray>
#include <QCoreApplication>
#include <QThread>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{

vss::endpoint_event make_event(int sequence)
{
    vss::endpoint_event event;
    event.kind = vss::event_kind::log_message;
    event.endpoint = 1;
    event.name = "test";
    event.message = "event " + std::to_string(sequence);
    event.when = vss::clock_type::now();
    return event;
}

using vss_test::spin;
using vss_test::wait_until;

vss::hub_options make_hub_options()
{
    vss::hub_options options;
    options.io_threads = 1;
    options.traffic_capacity = 64;
    return options;
}

} // namespace

TEST_CASE("event bridge: batches hub events on the GUI thread")
{
    vss::hub hub{make_hub_options()};
    hub.start();

    vss::gui::EventBridge::options options;
    options.interval_ms = 60000;
    options.max_events = 128;

    {
        vss::gui::EventBridge bridge(hub, options);
        int batches = 0;
        std::size_t last_size = 0;
        QThread* delivery_thread = nullptr;
        QObject::connect(&bridge, &vss::gui::EventBridge::batch_ready, &bridge,
                         [&](const vss::gui::event_batch& events)
                         {
                             ++batches;
                             last_size =
                                 static_cast<std::size_t>(events.size());
                             delivery_thread = QThread::currentThread();
                         });

        bridge.start();
        for (int index = 0; index < 40; ++index)
            hub.publish(make_event(index));

        REQUIRE(wait_until([&bridge] { return bridge.delivered() >= 40; }));

        REQUIRE_EQ(bridge.delivered(), std::uint64_t{40});
        REQUIRE_EQ(batches, 0);

        bridge.flush();
        REQUIRE_EQ(batches, 1);
        REQUIRE_EQ(last_size, std::size_t{40});
        REQUIRE(delivery_thread == QCoreApplication::instance()->thread());
    }

    hub.stop();
    hub.join();
}

TEST_CASE("event bridge: caps the queue and counts dropped events")
{
    vss::hub hub{make_hub_options()};
    hub.start();

    vss::gui::EventBridge::options options;
    options.interval_ms = 60000;
    options.max_events = 10;

    {
        vss::gui::EventBridge bridge(hub, options);
        int batches = 0;
        std::size_t last_size = 0;
        QObject::connect(&bridge, &vss::gui::EventBridge::batch_ready, &bridge,
                         [&](const vss::gui::event_batch& events)
                         {
                             ++batches;
                             last_size =
                                 static_cast<std::size_t>(events.size());
                         });

        bridge.start();
        for (int index = 0; index < 50; ++index)
            hub.publish(make_event(index));
        REQUIRE(wait_until([&bridge] { return bridge.delivered() >= 50; }));

        REQUIRE_EQ(bridge.delivered(), std::uint64_t{50});
        REQUIRE_EQ(bridge.dropped(), std::uint64_t{40});
        bridge.flush();
        REQUIRE_EQ(batches, 1);
        REQUIRE_EQ(last_size, std::size_t{10});
        REQUIRE_EQ(bridge.dropped(), std::uint64_t{40});
    }

    hub.stop();
    hub.join();
}

TEST_CASE("event bridge: stop unsubscribes but keeps the shared queue alive")
{
    vss::hub hub{make_hub_options()};
    hub.start();

    vss::gui::EventBridge::options options;
    options.interval_ms = 60000;

    {
        vss::gui::EventBridge bridge(hub, options);
        // A second subscriber sees every event the hub dispatches. Sinks run
        // in publish order on one strand, so once it has seen the last event
        // the hub has finished dispatching all the earlier ones.
        auto seen = std::make_shared<std::atomic<int>>(0);
        const auto probe =
            hub.subscribe({}, [seen](const vss::endpoint_event&)
                          { seen->fetch_add(1, std::memory_order_relaxed); });

        bridge.start();
        for (int index = 0; index < 5; ++index)
            hub.publish(make_event(index));
        REQUIRE(wait_until([&bridge] { return bridge.delivered() >= 5; }));
        REQUIRE_EQ(bridge.delivered(), std::uint64_t{5});

        bridge.stop();
        for (int index = 0; index < 5; ++index)
            hub.publish(make_event(index));
        // Plus one more, so the dispatch of the fifth is also finished.
        hub.publish(make_event(5));
        REQUIRE(wait_until([&seen] { return seen->load() >= 11; }));
        REQUIRE_EQ(bridge.delivered(), std::uint64_t{5});
        hub.unsubscribe(probe);
    }

    hub.stop();
    hub.join();
}

TEST_CASE("event bridge: restart drops events queued before stop")
{
    vss::hub hub{make_hub_options()};
    hub.start();

    vss::gui::EventBridge::options options;
    options.interval_ms = 60000;

    {
        vss::gui::EventBridge bridge(hub, options);
        int batches = 0;
        QObject::connect(&bridge, &vss::gui::EventBridge::batch_ready, &bridge,
                         [&batches](const vss::gui::event_batch&)
                         { ++batches; });

        bridge.start();
        for (int index = 0; index < 3; ++index)
            hub.publish(make_event(index));
        REQUIRE(wait_until([&bridge] { return bridge.delivered() >= 3; }));
        REQUIRE_EQ(bridge.delivered(), std::uint64_t{3});

        bridge.stop();
        bridge.start();
        bridge.flush();
        REQUIRE_EQ(batches, 0);

        hub.publish(make_event(99));
        REQUIRE(wait_until([&bridge] { return bridge.delivered() >= 4; }));
        bridge.flush();
        REQUIRE_EQ(batches, 1);
    }

    hub.stop();
    hub.join();
}

TEST_CASE("event bridge: survives a publisher during shutdown")
{
    vss::hub hub{make_hub_options()};
    hub.start();

    std::atomic<bool> running{true};
    std::atomic<int> published{0};
    std::thread publisher(
        [&hub, &running, &published]
        {
            int sequence = 0;
            while (running.load(std::memory_order_relaxed))
            {
                hub.publish(make_event(sequence++));
                published.fetch_add(1, std::memory_order_relaxed);
                std::this_thread::yield();
            }
        });

    REQUIRE(wait_until([&published] { return published.load() > 0; }));

    vss::gui::EventBridge::options options;
    options.interval_ms = 1;
    {
        vss::gui::EventBridge bridge(hub, options);
        bridge.start();
        for (int index = 0; index < 10; ++index)
            spin(2);
    }

    running.store(false);
    publisher.join();
    hub.stop();
    hub.join();
    REQUIRE(published.load() > 0);
}

TEST_CASE("engine backend: ports, links and sends go through the adapter")
{
    vss::studio studio;
    {
        vss::gui::EngineStudioBackend backend(studio);
        const auto first = backend.create_virtual_port(
            QStringLiteral("a"), vss::virtual_port_kind::loopback, true);
        const auto second = backend.create_virtual_port(
            QStringLiteral("b"), vss::virtual_port_kind::loopback, true);
        REQUIRE(first.has_value());
        REQUIRE(second.has_value());
        REQUIRE_EQ(backend.ports().size(), std::size_t{2});
        // Only a pseudo terminal on a platform that can inspect other
        // processes has a definite answer; a loopback port never does.
        REQUIRE_EQ(backend.caps().peer_open, vss::peer_open_supported());
        REQUIRE(backend.caps().line_format);
        REQUIRE_EQ(backend.port(*first)->peer, vss::gui::peer_state::unknown);

        const auto link =
            backend.connect_ports(*first, *second, vss::link_options{});
        REQUIRE(link.has_value());
        REQUIRE_EQ(backend.links().size(), std::size_t{1});
        REQUIRE_EQ(backend.links()[0].shape, vss::gui::link_shape::two_way);

        REQUIRE(backend.send(*first, QByteArrayLiteral("hello")).has_value());
        // The send is written on the endpoint's own strand, so the counter
        // moves some time later. Poll the view until it does.
        REQUIRE(wait_until(
            [&backend, &first]
            {
                backend.refresh();
                return backend.port(*first)->stats.bytes_tx >= 5;
            }));
    }
}

TEST_CASE("engine backend: the line wiring of a link can be read and changed")
{
    vss::studio studio;
    vss::gui::EngineStudioBackend backend(studio);
    REQUIRE(backend.caps().line_matrix);

    const auto first = backend.create_virtual_port(
        QStringLiteral("a"), vss::virtual_port_kind::loopback, true);
    const auto second = backend.create_virtual_port(
        QStringLiteral("b"), vss::virtual_port_kind::loopback, true);
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    const auto link =
        backend.connect_ports(*first, *second, vss::link_options{});
    REQUIRE(link.has_value());

    // A new link has the null-modem cable.
    const auto standard = backend.link_wiring(*link);
    REQUIRE(standard.has_value());
    REQUIRE(*standard == vss::gui::line_wiring{});

    vss::gui::line_wiring wiring;
    wiring.dtr_to_dsr = false;
    wiring.dtr_to_ri = true;
    wiring.rts_to_cts = false;
    wiring.rts_to_dcd = true;
    REQUIRE(backend.set_link_wiring(*link, wiring).has_value());
    const auto changed = backend.link_wiring(*link);
    REQUIRE(changed.has_value());
    REQUIRE(*changed == wiring);

    REQUIRE(!backend.set_link_wiring(vss::link_id{9999}, wiring).has_value());
    REQUIRE(!backend.link_wiring(vss::link_id{9999}).has_value());
}
