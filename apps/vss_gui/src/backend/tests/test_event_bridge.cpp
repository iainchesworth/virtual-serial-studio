//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "backend/engine_studio_backend.hpp"
#include "backend/event_bridge.hpp"

#include "test_framework.hpp"

#include <vss/core/hub.hpp>
#include <vss/studio/studio.hpp>

#include <QByteArray>
#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <cstdint>
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

void spin(int milliseconds)
{
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

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

        for (int index = 0; index < 500 && bridge.delivered() < 40; ++index)
            spin(2);

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
        for (int index = 0; index < 500 && bridge.delivered() < 50; ++index)
            spin(2);

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
        bridge.start();
        for (int index = 0; index < 5; ++index)
            hub.publish(make_event(index));
        for (int index = 0; index < 500 && bridge.delivered() < 5; ++index)
            spin(2);
        REQUIRE_EQ(bridge.delivered(), std::uint64_t{5});

        bridge.stop();
        for (int index = 0; index < 5; ++index)
            hub.publish(make_event(index));
        spin(10);
        REQUIRE_EQ(bridge.delivered(), std::uint64_t{5});
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
        for (int index = 0; index < 500 && bridge.delivered() < 3; ++index)
            spin(2);
        REQUIRE_EQ(bridge.delivered(), std::uint64_t{3});

        bridge.stop();
        bridge.start();
        bridge.flush();
        REQUIRE_EQ(batches, 0);

        hub.publish(make_event(99));
        for (int index = 0; index < 500 && bridge.delivered() < 4; ++index)
            spin(2);
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

    for (int index = 0; index < 500 && published.load() == 0; ++index)
        spin(1);
    REQUIRE(published.load() > 0);

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
        REQUIRE(!backend.caps().peer_open);
        REQUIRE_EQ(backend.port(*first)->peer, vss::gui::peer_state::unknown);

        const auto link =
            backend.connect_ports(*first, *second, vss::link_options{});
        REQUIRE(link.has_value());
        REQUIRE_EQ(backend.links().size(), std::size_t{1});
        REQUIRE_EQ(backend.links()[0].shape, vss::gui::link_shape::two_way);

        REQUIRE(backend.send(*first, QByteArrayLiteral("hello")).has_value());
        spin(20);
        backend.refresh();
        REQUIRE(backend.port(*first)->stats.bytes_tx >= 5);
    }
}
