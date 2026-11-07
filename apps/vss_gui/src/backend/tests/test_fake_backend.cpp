//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "backend/fake_studio_backend.hpp"

#include "test_framework.hpp"

#include <vss/core/error.hpp>
#include <vss/core/types.hpp>
#include <vss/ports/virtual_port.hpp>

#include <QByteArray>
#include <QString>

#include <algorithm>

TEST_CASE("fake backend: creates ports and suffixes duplicate names")
{
    vss::gui::FakeStudioBackend backend;
    int changes = 0;
    QObject::connect(&backend, &vss::gui::StudioBackend::ports_changed,
                     [&changes] { ++changes; });

    const auto first = backend.create_virtual_port(
        QStringLiteral("port-1"), vss::virtual_port_kind::loopback, true);
    REQUIRE(first.has_value());

    const auto second = backend.create_virtual_port(
        QStringLiteral("port-1"), vss::virtual_port_kind::loopback, true);
    REQUIRE(second.has_value());

    REQUIRE_EQ(backend.ports().size(), std::size_t{2});
    REQUIRE_EQ(backend.ports()[0].name, QStringLiteral("port-1"));
    REQUIRE_EQ(backend.ports()[1].name, QStringLiteral("port-1-2"));
    REQUIRE(backend.ports()[0].studio_only);
    REQUIRE(backend.ports()[0].open);
    REQUIRE_EQ(backend.ports()[0].peer, vss::gui::peer_state::unknown);
    REQUIRE(changes >= 2);

    const auto removed = backend.remove_port(*first);
    REQUIRE(removed.has_value());
    REQUIRE_EQ(backend.ports().size(), std::size_t{1});
}

TEST_CASE("fake backend: links, owner, lines and send")
{
    vss::gui::FakeStudioBackend backend;
    const auto a = backend.create_virtual_port(
        QStringLiteral("a"), vss::virtual_port_kind::loopback, true);
    const auto b = backend.create_virtual_port(
        QStringLiteral("b"), vss::virtual_port_kind::loopback, true);
    const auto c = backend.create_virtual_port(
        QStringLiteral("c"), vss::virtual_port_kind::loopback, true);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE(c.has_value());

    const auto duplex = backend.connect_ports(*a, *b, vss::link_options{});
    REQUIRE(duplex.has_value());
    REQUIRE_EQ(backend.links().size(), std::size_t{1});
    REQUIRE_EQ(backend.links()[0].shape, vss::gui::link_shape::two_way);

    const auto split = backend.split(*a, {*b, *c}, vss::link_options{});
    REQUIRE(split.has_value());
    REQUIRE_EQ(backend.links()[1].shape, vss::gui::link_shape::split);

    const auto joined = backend.join({*b, *c}, *a, vss::link_options{});
    REQUIRE(joined.has_value());
    REQUIRE_EQ(backend.links()[2].shape, vss::gui::link_shape::join);

    const auto one_way = backend.redirect(*a, *b, vss::link_options{});
    REQUIRE(one_way.has_value());
    REQUIRE_EQ(backend.links()[3].shape, vss::gui::link_shape::one_way);

    const auto monitor = backend.monitor(*a, *c);
    REQUIRE(monitor.has_value());
    REQUIRE_EQ(backend.links()[4].shape, vss::gui::link_shape::monitor);

    const auto owned = backend.set_owner(*joined, *b);
    REQUIRE(owned.has_value());
    REQUIRE_EQ(backend.links()[2].owner, *b);
    REQUIRE(!backend.set_owner(*joined, *a).has_value());
    REQUIRE(!backend.set_owner(*joined, 999).has_value());
    REQUIRE(backend.release_owner(*joined).has_value());
    REQUIRE_EQ(backend.links()[2].owner, vss::endpoint_id{0});

    REQUIRE(backend.send(*a, QByteArray("hello")).has_value());
    REQUIRE_EQ(backend.sent().size(), std::size_t{1});
    REQUIRE_EQ(backend.sent()[0], QByteArray("hello"));
    REQUIRE(backend.send(999, QByteArrayLiteral("x")).error() ==
            vss::make_error_code(vss::errc::not_found));

    REQUIRE(backend.set_line(*a, vss::control_line::dtr, true).has_value());
    REQUIRE(vss_test::require_value(backend.port(*a)).lines.dtr());
}

TEST_CASE("fake backend: snapshot and apply round trip")
{
    vss::gui::FakeStudioBackend backend;
    const auto a = backend.create_virtual_port(
        QStringLiteral("a"), vss::virtual_port_kind::loopback, true);
    const auto b = backend.create_mock_port(QStringLiteral("fixture"),
                                            vss::serial_settings{},
                                            QStringLiteral("scripted"));
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE(backend.connect_ports(*a, *b, vss::link_options{}).has_value());

    const vss::config::document document = backend.snapshot();
    REQUIRE_EQ(document.ports.size(), std::size_t{2});
    REQUIRE_EQ(document.links.size(), std::size_t{1});

    vss::gui::FakeStudioBackend copy;
    const auto applied = copy.apply(document, true);
    REQUIRE(applied.has_value());
    REQUIRE_EQ(copy.ports().size(), std::size_t{2});
    REQUIRE_EQ(copy.links().size(), std::size_t{1});
    REQUIRE_EQ(copy.ports()[0].name, QStringLiteral("a"));
    REQUIRE_EQ(copy.links()[0].shape, vss::gui::link_shape::two_way);
}

TEST_CASE("fake backend: errors are returned and capabilities are visible")
{
    vss::gui::FakeStudioBackend backend;
    backend.fail_next(vss::make_error_code(vss::errc::permission_denied));

    const auto failed = backend.create_virtual_port(
        QStringLiteral("a"), vss::virtual_port_kind::loopback, true);
    REQUIRE(!failed.has_value());
    REQUIRE_EQ(failed.error(),
               vss::make_error_code(vss::errc::permission_denied));
    REQUIRE(backend.ports().empty());

    const auto unknown = backend.remove_port(42);
    REQUIRE(!unknown.has_value());
    REQUIRE_EQ(unknown.error(), vss::make_error_code(vss::errc::not_found));

    vss::gui::capabilities caps;
    caps.faults = true;
    caps.peer_open = true;
    backend.set_capabilities(caps);
    REQUIRE(backend.caps().faults);
    REQUIRE(backend.caps().peer_open);

    const auto port = backend.create_virtual_port(
        QStringLiteral("a"), vss::virtual_port_kind::loopback, true);
    REQUIRE(port.has_value());
    backend.set_peer_state(*port, vss::gui::peer_state::not_open);
    REQUIRE_EQ(vss_test::require_value(backend.port(*port)).peer,
               vss::gui::peer_state::not_open);
}
