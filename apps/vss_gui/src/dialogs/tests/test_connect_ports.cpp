//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "test_framework.hpp"

#include "backend/fake_studio_backend.hpp"
#include "dialogs/connect_ports.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QPushButton>

#include <vector>

namespace
{

struct fixture_ports
{
    vss::endpoint_id alpha = 0;
    vss::endpoint_id beta = 0;
    vss::endpoint_id gamma = 0;
    vss::endpoint_id delta = 0;
    vss::endpoint_id epsilon = 0;
};

fixture_ports make_ports(vss::gui::FakeStudioBackend& backend)
{
    fixture_ports ports;
    const auto make =
        [&backend](const QString& name, vss::virtual_port_kind kind)
    {
        const auto created = backend.create_virtual_port(name, kind, true);
        REQUIRE(created.has_value());
        return *created;
    };
    ports.alpha = make(QStringLiteral("alpha"), vss::virtual_port_kind::pty);
    ports.beta = make(QStringLiteral("beta"), vss::virtual_port_kind::pty);
    ports.gamma =
        make(QStringLiteral("gamma"), vss::virtual_port_kind::loopback);
    ports.delta = make(QStringLiteral("delta"), vss::virtual_port_kind::pty);
    ports.epsilon =
        make(QStringLiteral("epsilon"), vss::virtual_port_kind::loopback);
    backend.refresh();
    return ports;
}

void show(QDialog& dialog)
{
    dialog.show();
    QApplication::processEvents();
}

vss::gui::link_view only_link(const vss::gui::FakeStudioBackend& backend)
{
    REQUIRE_EQ(backend.links().size(), std::size_t{1});
    return backend.links().front();
}

} // namespace

TEST_CASE("connect: every mode makes the right link")
{
    {
        vss::gui::FakeStudioBackend backend;
        const fixture_ports ports = make_ports(backend);
        vss::gui::ConnectPortsDialog dialog(backend);
        show(dialog);
        REQUIRE_EQ(dialog.current_mode(),
                   vss::gui::ConnectPortsDialog::mode::two_way);
        dialog.select_from(ports.alpha);
        dialog.select_to(ports.beta);
        REQUIRE(dialog.primary_enabled());
        REQUIRE(dialog.submit());
        REQUIRE_EQ(backend.links().size(), std::size_t{1});
        const vss::gui::link_view link = only_link(backend);
        REQUIRE_EQ(link.shape, vss::gui::link_shape::two_way);
        REQUIRE_EQ(link.sources.size(), 1);
        REQUIRE_EQ(link.sources.front(), ports.alpha);
        REQUIRE_EQ(link.targets.front(), ports.beta);
        REQUIRE(dialog.result().has_value());
    }
    {
        vss::gui::FakeStudioBackend backend;
        const fixture_ports ports = make_ports(backend);
        vss::gui::ConnectPortsDialog dialog(backend);
        show(dialog);
        REQUIRE(dialog.set_mode(vss::gui::ConnectPortsDialog::mode::redirect));
        dialog.select_from(ports.alpha);
        dialog.select_to(ports.beta);
        REQUIRE(dialog.submit());
        REQUIRE_EQ(only_link(backend).shape, vss::gui::link_shape::one_way);
    }
    {
        vss::gui::FakeStudioBackend backend;
        const fixture_ports ports = make_ports(backend);
        vss::gui::ConnectPortsDialog dialog(backend);
        show(dialog);
        REQUIRE(dialog.set_mode(vss::gui::ConnectPortsDialog::mode::monitor));
        dialog.select_from(ports.alpha);
        dialog.select_to(ports.beta);
        REQUIRE(dialog.submit());
        const vss::gui::link_view link = only_link(backend);
        REQUIRE_EQ(link.shape, vss::gui::link_shape::monitor);
        REQUIRE(!link.options.emulate_modem_lines);
    }
    {
        vss::gui::FakeStudioBackend backend;
        const fixture_ports ports = make_ports(backend);
        vss::gui::ConnectPortsDialog dialog(backend);
        show(dialog);
        REQUIRE(dialog.set_mode(vss::gui::ConnectPortsDialog::mode::split));
        dialog.select_split_source(ports.alpha);
        dialog.set_target_ids({ports.beta, ports.delta});
        REQUIRE(dialog.submit());
        const vss::gui::link_view link = only_link(backend);
        REQUIRE_EQ(link.shape, vss::gui::link_shape::split);
        REQUIRE_EQ(link.sources.front(), ports.alpha);
        REQUIRE_EQ(link.targets.size(), 2);
    }
    {
        vss::gui::FakeStudioBackend backend;
        const fixture_ports ports = make_ports(backend);
        vss::gui::ConnectPortsDialog dialog(backend);
        show(dialog);
        REQUIRE(dialog.set_mode(vss::gui::ConnectPortsDialog::mode::join));
        dialog.select_join_target(ports.delta);
        dialog.set_source_ids({ports.alpha, ports.beta});
        REQUIRE(dialog.submit());
        const vss::gui::link_view link = only_link(backend);
        REQUIRE_EQ(link.shape, vss::gui::link_shape::join);
        REQUIRE_EQ(link.targets.front(), ports.delta);
        REQUIRE_EQ(link.sources.size(), 2);
    }
}

TEST_CASE("connect: result sentences and the live count line")
{
    vss::gui::FakeStudioBackend backend;
    const fixture_ports ports = make_ports(backend);
    vss::gui::ConnectPortsDialog dialog(backend);
    show(dialog);

    dialog.select_from(ports.alpha);
    dialog.select_to(ports.beta);
    REQUIRE_EQ(dialog.accessible_result_text(),
               QStringLiteral("Everything alpha sends arrives at beta, and the "
                              "other way round."));

    REQUIRE(dialog.set_mode(vss::gui::ConnectPortsDialog::mode::redirect));
    REQUIRE_EQ(dialog.accessible_result_text(),
               QStringLiteral("Everything alpha sends arrives at beta. beta "
                              "cannot reply."));

    REQUIRE(dialog.set_mode(vss::gui::ConnectPortsDialog::mode::monitor));
    REQUIRE_EQ(dialog.accessible_result_text(),
               QStringLiteral("beta receives a read-only copy of what alpha "
                              "sends. It never drives modem lines."));

    REQUIRE(dialog.set_mode(vss::gui::ConnectPortsDialog::mode::split));
    dialog.select_split_source(ports.alpha);
    dialog.set_target_ids({ports.beta, ports.delta});
    REQUIRE_EQ(dialog.count_text(),
               QStringLiteral("2 ports chosen. That is enough for a split."));
    REQUIRE_EQ(dialog.accessible_result_text(),
               QStringLiteral("Everything alpha receives is copied to beta and "
                              "delta. Whatever either of them writes goes back "
                              "to alpha."));
    REQUIRE(dialog.primary_enabled());

    dialog.set_target_ids({ports.beta});
    REQUIRE_EQ(dialog.count_text(), QStringLiteral("Choose at least 2 ports."));
    REQUIRE(!dialog.primary_enabled());
    dialog.set_target_ids({});
    REQUIRE(!dialog.primary_enabled());
    dialog.set_target_ids({ports.beta, ports.delta});
    REQUIRE(dialog.primary_enabled());

    REQUIRE(dialog.set_mode(vss::gui::ConnectPortsDialog::mode::join));
    dialog.select_join_target(ports.delta);
    dialog.set_source_ids({ports.alpha, ports.beta});
    REQUIRE_EQ(dialog.count_text(),
               QStringLiteral("2 ports chosen. That is enough for a join."));
    REQUIRE_EQ(dialog.accessible_result_text(),
               QStringLiteral("Everything alpha and beta write goes to delta. "
                              "Their bytes are mixed in the order they arrive. "
                              "delta does not send anything back to them."));
}

TEST_CASE("connect: self-links and two loopback ports are blocked")
{
    vss::gui::FakeStudioBackend backend;
    const fixture_ports ports = make_ports(backend);
    vss::gui::ConnectPortsDialog dialog(backend);
    show(dialog);

    dialog.select_from(ports.alpha);
    dialog.select_to(ports.alpha);
    REQUIRE(!dialog.primary_enabled());
    REQUIRE_EQ(dialog.block_reason(),
               QStringLiteral("A port cannot link to itself. Choose a "
                              "different port."));
    REQUIRE(!dialog.submit());
    REQUIRE(backend.links().empty());

    dialog.select_from(ports.gamma);
    dialog.select_to(ports.epsilon);
    REQUIRE(!dialog.two_way_allowed());
    REQUIRE(!dialog.primary_enabled());
    REQUIRE_EQ(
        dialog.block_reason(),
        QStringLiteral("Studio-only ports echo what they receive, so a two-way "
                       "link between them would repeat data. Use one-way, or "
                       "link a real or paired port."));
    REQUIRE(!dialog.set_mode(vss::gui::ConnectPortsDialog::mode::two_way));

    dialog.select_to(ports.delta);
    REQUIRE(dialog.two_way_allowed());
    REQUIRE(dialog.primary_enabled());
    REQUIRE(dialog.set_mode(vss::gui::ConnectPortsDialog::mode::redirect));
    REQUIRE(dialog.submit());
    REQUIRE_EQ(backend.links().size(), std::size_t{1});
}

TEST_CASE("connect: a refused loop keeps the dialog open")
{
    vss::gui::FakeStudioBackend backend;
    const fixture_ports ports = make_ports(backend);
    vss::gui::ConnectPortsDialog dialog(backend);
    show(dialog);
    dialog.select_from(ports.alpha);
    dialog.select_to(ports.beta);

    backend.fail_next(vss::make_error_code(vss::errc::cycle_detected));
    REQUIRE(!dialog.submit());
    REQUIRE(dialog.error_visible());
    REQUIRE_EQ(dialog.error_title(),
               QStringLiteral("That link would create a loop"));
    REQUIRE(
        dialog.error_body().contains(QStringLiteral("Nothing was changed")));
    REQUIRE(dialog.last_error().contains(QStringLiteral("cycle")));
    REQUIRE(!dialog.result().has_value());
    REQUIRE(backend.links().empty());

    dialog.select_to(ports.delta);
    REQUIRE(!dialog.error_visible());
    REQUIRE(dialog.submit());
    REQUIRE_EQ(backend.links().size(), std::size_t{1});
}

TEST_CASE("connect: the Advanced owner option changes the merge policy")
{
    vss::gui::FakeStudioBackend backend;
    const fixture_ports ports = make_ports(backend);
    vss::gui::ConnectPortsDialog dialog(backend);
    show(dialog);
    REQUIRE(dialog.set_mode(vss::gui::ConnectPortsDialog::mode::join));
    dialog.select_join_target(ports.delta);
    dialog.set_source_ids({ports.alpha, ports.beta});

    REQUIRE(dialog.owner_check() != nullptr);
    REQUIRE(!dialog.owner_check()->isEnabled());
    int advanced_requests = 0;
    QObject::connect(&dialog, &vss::gui::ConnectPortsDialog::advanced_requested,
                     [&advanced_requests] { ++advanced_requests; });
    QPushButton* advanced = nullptr;
    for (QPushButton* button : dialog.findChildren<QPushButton*>())
    {
        if (button->text() == QStringLiteral("Turn on Advanced mode"))
            advanced = button;
    }
    REQUIRE(advanced != nullptr);
    advanced->click();
    QApplication::processEvents();
    REQUIRE_EQ(advanced_requests, 1);
    REQUIRE(dialog.advanced());
    REQUIRE(dialog.owner_check()->isEnabled());
    dialog.owner_check()->setChecked(true);
    REQUIRE(dialog.submit());
    REQUIRE_EQ(only_link(backend).options.policy, vss::merge_policy::owner);
}

TEST_CASE("connect: without Advanced the join keeps interleaving")
{
    vss::gui::FakeStudioBackend backend;
    const fixture_ports ports = make_ports(backend);
    vss::gui::ConnectPortsDialog dialog(backend);
    show(dialog);
    REQUIRE(dialog.set_mode(vss::gui::ConnectPortsDialog::mode::join));
    dialog.select_join_target(ports.delta);
    dialog.set_source_ids({ports.alpha, ports.beta});
    REQUIRE(dialog.submit());
    REQUIRE_EQ(only_link(backend).options.policy,
               vss::merge_policy::interleave);
}
