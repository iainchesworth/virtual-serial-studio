//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "test_framework.hpp"

#include "backend/fake_studio_backend.hpp"
#include "panes/inspector_panel.hpp"
#include "panes/monitor_model.hpp"
#include "panes/monitor_panel.hpp"
#include "panes/port_model.hpp"
#include "panes/ports_panel.hpp"
#include "panes/selection_service.hpp"
#include "panes/send_panel.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QListView>
#include <QPushButton>
#include <QSettings>
#include <QTableView>

#include <algorithm>
#include <chrono>
#include <thread>

namespace
{

vss::gui::gui_event data_event(vss::endpoint_id id, const QString& name,
                               vss::data_direction direction,
                               const QByteArray& data)
{
    vss::gui::gui_event event;
    event.kind = vss::event_kind::data;
    event.endpoint = id;
    event.name = name;
    event.direction = direction;
    event.data = data;
    return event;
}

vss::gui::port_view make_port(vss::endpoint_id id, const QString& name,
                              const QString& path, vss::endpoint_kind kind,
                              bool open = true)
{
    vss::gui::port_view port;
    port.id = id;
    port.name = name;
    port.path = path;
    port.kind = kind;
    port.state = open ? vss::endpoint_state::open : vss::endpoint_state::closed;
    port.open = open;
    return port;
}

} // namespace

TEST_CASE("ports: state words, role text and filtering")
{
    vss::gui::PortModel model;
    auto terminal =
        make_port(1, QStringLiteral("terminal"), QStringLiteral("COM7"),
                  vss::endpoint_kind::virtual_port);
    terminal.peer = vss::gui::peer_state::not_open;
    terminal.stats.bytes_rx = 12;
    terminal.stats.bytes_tx = 2048;
    auto device = make_port(2, QStringLiteral("device"), QStringLiteral("COM3"),
                            vss::endpoint_kind::physical);
    device.serial.baud_rate = 115200;
    auto logger = make_port(3, QStringLiteral("logger"),
                            QStringLiteral("loopback:logger"),
                            vss::endpoint_kind::virtual_port);
    logger.studio_only = true;

    vss::gui::link_view link;
    link.id = 1;
    link.sources = {2};
    link.targets = {1};
    model.set_snapshot({terminal, device, logger}, {link});

    REQUIRE_EQ(model.rowCount(), 3);
    REQUIRE_EQ(vss::gui::PortModel::state_text(terminal),
               QStringLiteral("Waiting for program"));
    REQUIRE_EQ(vss::gui::PortModel::state_text(device), QStringLiteral("Open"));
    REQUIRE_EQ(vss::gui::PortModel::state_text(logger),
               QStringLiteral("Studio only"));
    REQUIRE(vss::gui::PortModel::role_text(terminal, {terminal, device, logger},
                                           {link})
                .contains(QStringLiteral("linked to device")));
    REQUIRE(vss::gui::PortModel::role_text(device, {terminal, device, logger},
                                           {link})
                .contains(QStringLiteral("shared with 1 programs")));
    REQUIRE(vss::gui::PortModel::serial_text(device).contains(
        QStringLiteral("115200")));

    const QModelIndex row = model.index(0, 0);
    REQUIRE(row.data(vss::gui::PortModel::port_id_role).toULongLong() == 1);
    REQUIRE(row.data(Qt::AccessibleTextRole)
                .toString()
                .contains(QStringLiteral("COM7")));
    REQUIRE(row.data(vss::gui::PortModel::bytes_tx_role)
                .toString()
                .contains(QStringLiteral("kB")));

    model.set_filter(QStringLiteral("COM3"));
    REQUIRE_EQ(model.rowCount(), 1);
    REQUIRE_EQ(model.port_id(0), 2);
    REQUIRE_EQ(model.row_for_id(2), 0);
    model.set_filter(QStringLiteral("nope"));
    REQUIRE_EQ(model.rowCount(), 0);
}

TEST_CASE("monitor: 10,000 row burst stays bounded and responsive")
{
    vss::gui::MonitorModel model;
    vss::gui::event_batch events;
    for (int index = 0; index < 10'000; ++index)
    {
        events.push_back(data_event(
            static_cast<vss::endpoint_id>(index % 3 + 1),
            QStringLiteral("terminal"),
            index % 2 == 0 ? vss::data_direction::rx : vss::data_direction::tx,
            QByteArrayLiteral("x")));
    }
    QElapsedTimer timer;
    timer.start();
    model.append_events(events);
    const qint64 elapsed = timer.elapsed();
    REQUIRE_EQ(model.rowCount(), 10'000);
    REQUIRE_EQ(model.total_rows(), 10'000);
    REQUIRE(elapsed < 5000);

    vss::gui::event_batch more;
    for (int index = 0; index < 25; ++index)
    {
        more.push_back(data_event(1, QStringLiteral("terminal"),
                                  vss::data_direction::rx,
                                  QByteArrayLiteral("y")));
    }
    model.append_events(more);
    REQUIRE_EQ(model.total_rows(), 10'000);
    REQUIRE_EQ(model.rowCount(), 10'000);
}

TEST_CASE("monitor: ascii, hex, filtering, pause and drop rows")
{
    vss::gui::MonitorModel model;
    vss::gui::event_batch events;
    events.push_back(data_event(1, QStringLiteral("terminal"),
                                vss::data_direction::rx,
                                QByteArrayLiteral("Hi\r\n")));
    events.push_back(data_event(1, QStringLiteral("terminal"),
                                vss::data_direction::tx,
                                QByteArrayLiteral("AB")));
    events.push_back(data_event(2, QStringLiteral("device"),
                                vss::data_direction::rx,
                                QByteArrayLiteral("Z")));
    model.append_events(events);
    REQUIRE_EQ(model.rowCount(), 3);

    REQUIRE_EQ(model.index(0, 4).data().toString(), QStringLiteral("Hi\\r\\n"));
    model.set_hex(true);
    REQUIRE_EQ(model.index(0, 4).data().toString(),
               QStringLiteral("48 69 0D 0A"));
    REQUIRE_EQ(
        vss::gui::MonitorModel::ascii_preview(QByteArrayLiteral("a\x01")),
        QStringLiteral("a."));

    model.set_filters(0, true, false, true, {});
    REQUIRE_EQ(model.rowCount(), 2);
    model.set_filters(0, true, true, true, QStringLiteral("device"));
    REQUIRE_EQ(model.rowCount(), 1);
    REQUIRE_EQ(model.port_at(0), 2);

    model.set_filters(0, true, true, true, {});
    const int before = model.rowCount();
    model.set_paused(true);
    model.append_events(
        {data_event(1, QStringLiteral("terminal"), vss::data_direction::rx,
                    QByteArrayLiteral("paused"))});
    REQUIRE_EQ(model.rowCount(), before);
    model.set_paused(false);

    model.append_drop(QStringLiteral("terminal"), 42);
    model.set_filters(0, true, true, true, {});
    REQUIRE(model.rowCount() == before + 1);
    REQUIRE(model.index(model.rowCount() - 1, 4)
                .data()
                .toString()
                .contains(QStringLiteral("not the owner")));
}

TEST_CASE("send: hex validation, CRLF and the fake backend")
{
    vss::gui::FakeStudioBackend backend;
    const auto id = backend.create_virtual_port(
        QStringLiteral("send-test"), vss::virtual_port_kind::pty, true);
    REQUIRE(id.has_value());
    vss::gui::SendPanel panel(backend);
    panel.set_selected_port(*id);

    auto* hex = panel.findChild<QCheckBox*>();
    for (QCheckBox* box : panel.findChildren<QCheckBox*>())
    {
        if (box->text() == QStringLiteral("Hex"))
            hex = box;
    }
    REQUIRE(hex != nullptr);
    hex->setChecked(true);
    panel.set_text(QStringLiteral("48 65"));
    QApplication::processEvents();
    panel.send_now();
    REQUIRE_EQ(backend.sent().size(), 1);
    REQUIRE_EQ(backend.sent().front(), QByteArrayLiteral("He\r\n"));

    panel.set_text(QStringLiteral("4G"));
    QApplication::processEvents();
    QPushButton* send = nullptr;
    for (QPushButton* button : panel.findChildren<QPushButton*>())
    {
        if (button->text() == QStringLiteral("Send"))
            send = button;
    }
    REQUIRE(send != nullptr);
    REQUIRE(!send->isEnabled());
    bool found_error = false;
    for (QLabel* label : panel.findChildren<QLabel*>())
    {
        if (label->text().contains(QStringLiteral("Hex needs pairs")))
            found_error = true;
    }
    REQUIRE(found_error);
}

TEST_CASE("ports panel: selection stays in sync with the service")
{
    vss::gui::FakeStudioBackend backend;
    vss::gui::SelectionService selection;
    const auto first = backend.create_virtual_port(
        QStringLiteral("one"), vss::virtual_port_kind::pty, true);
    const auto second = backend.create_virtual_port(
        QStringLiteral("two"), vss::virtual_port_kind::pty, true);
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());

    vss::gui::PortsPanel panel(backend, selection);
    panel.resize(360, 300);
    panel.show();
    QApplication::processEvents();
    REQUIRE_EQ(selection.port(), *first);
    panel.select_port(*second);
    QApplication::processEvents();
    REQUIRE_EQ(selection.port(), *second);

    auto* list = panel.findChild<QListView*>();
    REQUIRE(list != nullptr);
    REQUIRE_EQ(list->model()->rowCount(), 2);
    panel.set_advanced(true);
    auto* table = panel.findChild<QTableView*>();
    REQUIRE(table != nullptr);
    REQUIRE_EQ(table->model()->rowCount(), 2);
}

TEST_CASE("monitor panel: exact empty state and no row widgets")
{
    vss::gui::FakeStudioBackend backend;
    vss::gui::MonitorPanel panel(backend);
    panel.resize(900, 240);
    panel.show();
    QApplication::processEvents();
    bool empty = false;
    for (QLabel* label : panel.findChildren<QLabel*>())
    {
        if (label->text() ==
            QStringLiteral("No traffic yet. Send some bytes, or open the "
                           "port in a program."))
            empty = true;
    }
    REQUIRE(empty);
    auto* table = panel.findChild<QTableView*>();
    REQUIRE(table != nullptr);
    REQUIRE(table->viewport()->findChildren<QWidget*>().isEmpty());
    panel.set_advanced(true);
    QApplication::processEvents();
    REQUIRE(!panel.findChildren<QComboBox*>().isEmpty());
}

TEST_CASE("inspector: waiting sentence and control line chips")
{
    vss::gui::FakeStudioBackend backend;
    const auto id = backend.create_virtual_port(
        QStringLiteral("inspector-test"), vss::virtual_port_kind::pty, true);
    REQUIRE(id.has_value());
    backend.set_peer_state(*id, vss::gui::peer_state::not_open);
    vss::gui::InspectorPanel panel(backend);
    panel.set_selected_port(*id);
    QApplication::processEvents();

    bool waiting = false;
    for (QLabel* label : panel.findChildren<QLabel*>())
    {
        if (label->text().contains(QStringLiteral("No program has opened")))
            waiting = true;
    }
    REQUIRE(waiting);

    bool has_dtr = false;
    for (QLabel* label : panel.findChildren<QLabel*>())
    {
        if (label->text().contains(QStringLiteral("DTR")))
            has_dtr = true;
    }
    REQUIRE(has_dtr);
}
