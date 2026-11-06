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
    REQUIRE_EQ(model.port_id(0), vss::endpoint_id{2});
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

namespace
{

vss::gui::gui_event timed_event(vss::endpoint_id id, const QString& name,
                                vss::data_direction direction,
                                const QByteArray& data, vss::time_point when)
{
    auto event = data_event(id, name, direction, data);
    event.when = when;
    return event;
}

} // namespace

TEST_CASE("monitor: the capture export keeps every byte and the event time")
{
    vss::gui::MonitorModel model;
    const auto base = vss::clock_type::now();
    using namespace std::chrono_literals;

    // Longer than the 256 bytes the table shows.
    const QByteArray big(1000, 'q');
    vss::gui::event_batch events;
    events.push_back(timed_event(1, QStringLiteral("master"),
                                 vss::data_direction::rx, big, base + 100ms));
    events.push_back(timed_event(2, QStringLiteral("tap"),
                                 vss::data_direction::tx, QByteArrayLiteral("ab"),
                                 base + 350ms));
    model.append_events(events);
    model.append_drop(QStringLiteral("master"), 5);

    // The preview is cut for display; the capture is not.
    REQUIRE_EQ(model.row_at(0)->preview.size(), 256);
    const auto everything = model.export_capture();
    REQUIRE_EQ(everything.records.size(), std::size_t{2});
    REQUIRE_EQ(everything.records[0].data.size(), std::size_t{1000});
    REQUIRE_EQ(everything.records[0].port, std::string("master"));
    REQUIRE(everything.records[0].direction == vss::data_direction::rx);
    // Offsets are the engine's own times, 250 ms apart, from the first row.
    REQUIRE_EQ(everything.records[0].offset_ms, 0);
    REQUIRE_EQ(everything.records[1].offset_ms, 250);
    REQUIRE(everything.records[1].direction == vss::data_direction::tx);

    // The filters apply: only the tap.
    model.set_filters(2, true, true, true, {});
    const auto tap_only = model.export_capture();
    REQUIRE_EQ(tap_only.records.size(), std::size_t{1});
    REQUIRE_EQ(tap_only.records[0].port, std::string("tap"));
}

TEST_CASE("monitor: payloads kept for the capture export are bounded")
{
    vss::gui::MonitorModel model;
    const QByteArray chunk(1024 * 1024, 'z');
    const auto when = vss::clock_type::now();
    vss::gui::event_batch events;
    for (int index = 0; index < 40; ++index)
    {
        events.push_back(timed_event(1, QStringLiteral("p"),
                                     vss::data_direction::rx, chunk, when));
    }
    model.append_events(events);

    // 40 MiB came in; 32 MiB is the most that is kept, newest first.
    REQUIRE_EQ(model.total_rows(), 32);
    REQUIRE_EQ(model.export_capture().records.size(), std::size_t{32});
}

TEST_CASE("monitor panel: Export capture is named and follows the rows")
{
    vss::gui::FakeStudioBackend backend;
    vss::gui::MonitorPanel panel(backend);
    panel.resize(900, 240);
    panel.set_advanced(true);
    panel.show();
    QApplication::processEvents();

    QPushButton* button = nullptr;
    for (QPushButton* candidate : panel.findChildren<QPushButton*>())
    {
        if (candidate->text() == QStringLiteral("Export capture"))
            button = candidate;
    }
    REQUIRE(button != nullptr);
    REQUIRE_EQ(button->accessibleName(), QStringLiteral("Export capture"));
    REQUIRE(!button->accessibleDescription().isEmpty());
    // Nothing to export yet, the same as the text export beside it.
    REQUIRE(!button->isEnabled());

    panel.handle_events({data_event(1, QStringLiteral("terminal"),
                                    vss::data_direction::rx,
                                    QByteArrayLiteral("x"))});
    QApplication::processEvents();
    REQUIRE(button->isEnabled());
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

namespace
{

vss::endpoint_id make_driver_port(vss::gui::FakeStudioBackend& backend)
{
    vss::gui::capabilities caps;
    caps.com_numbers = true;
    caps.release_port = true;
    caps.peer_open = true;
    backend.set_capabilities(caps);
    const auto id =
        backend.create_driver_port(QStringLiteral("meter"), true, 7);
    REQUIRE(id.has_value());
    return *id;
}

} // namespace

TEST_CASE("ports: a driver port shows both program states and nothing else")
{
    vss::gui::FakeStudioBackend backend;
    const auto id = make_driver_port(backend);

    // The backend says nothing about a program: the port is plainly open.
    auto port = *backend.port(id);
    REQUIRE(port.peer == vss::gui::peer_state::unknown);
    REQUIRE_EQ(vss::gui::PortModel::state_text(port), QStringLiteral("Open"));

    backend.set_peer_state(id, vss::gui::peer_state::not_open);
    port = *backend.port(id);
    REQUIRE_EQ(vss::gui::PortModel::state_text(port),
               QStringLiteral("Waiting for program"));
    REQUIRE(vss::gui::PortModel::state_kind(port) ==
            vss::gui::port_state_kind::waiting);

    backend.set_peer_state(id, vss::gui::peer_state::open);
    port = *backend.port(id);
    REQUIRE_EQ(vss::gui::PortModel::state_text(port),
               QStringLiteral("Program connected"));
    REQUIRE(vss::gui::PortModel::state_kind(port) ==
            vss::gui::port_state_kind::connected);
}

TEST_CASE("inspector: a driver port has a COM number that can be changed")
{
    vss::gui::FakeStudioBackend backend;
    const auto id = make_driver_port(backend);
    vss::gui::InspectorPanel panel(backend);
    vss::endpoint_id asked = 0;
    QObject::connect(&panel, &vss::gui::InspectorPanel::com_number_requested,
                     [&asked](vss::endpoint_id value) { asked = value; });
    panel.set_selected_port(id);
    panel.show();
    QApplication::processEvents();

    REQUIRE(panel.com_change_button()->isVisibleTo(&panel));
    REQUIRE(panel.com_change_button()->isEnabled());
    REQUIRE(!panel.com_change_button()->accessibleName().isEmpty());
    bool shows_number = false;
    for (QLabel* label : panel.findChildren<QLabel*>())
    {
        if (label->text() == QStringLiteral("COM7"))
            shows_number = true;
    }
    REQUIRE(shows_number);
    panel.com_change_button()->click();
    REQUIRE_EQ(asked, id);

    // Without the capability the button is there but off, with the reason.
    backend.set_capabilities({});
    panel.refresh();
    QApplication::processEvents();
    REQUIRE(!panel.com_change_button()->isEnabled());
    REQUIRE_EQ(panel.com_change_button()->toolTip(),
               QStringLiteral("Not available in this build"));

    // A port that is not a driver port has no COM number row.
    const auto other = backend.create_virtual_port(
        QStringLiteral("other"), vss::virtual_port_kind::loopback, true);
    REQUIRE(other.has_value());
    panel.set_selected_port(*other);
    QApplication::processEvents();
    REQUIRE(!panel.com_change_button()->isVisibleTo(&panel));
}

TEST_CASE("inspector: Release port is an Advanced action for a held port")
{
    vss::gui::FakeStudioBackend backend;
    const auto id = make_driver_port(backend);
    vss::gui::InspectorPanel panel(backend);
    vss::endpoint_id asked = 0;
    QObject::connect(&panel, &vss::gui::InspectorPanel::release_port_requested,
                     [&asked](vss::endpoint_id value) { asked = value; });
    panel.set_selected_port(id);
    panel.show();

    // Nobody has the port, or Advanced is off: no button.
    backend.set_peer_state(id, vss::gui::peer_state::open);
    panel.refresh();
    QApplication::processEvents();
    REQUIRE(!panel.release_button()->isVisibleTo(&panel));

    panel.set_advanced(true);
    QApplication::processEvents();
    REQUIRE(panel.release_button()->isVisibleTo(&panel));
    REQUIRE(panel.release_button()->isEnabled());
    REQUIRE(!panel.release_button()->accessibleName().isEmpty());
    panel.release_button()->click();
    REQUIRE_EQ(asked, id);

    backend.set_peer_state(id, vss::gui::peer_state::not_open);
    panel.refresh();
    QApplication::processEvents();
    REQUIRE(!panel.release_button()->isVisibleTo(&panel));

    // A port the studio cannot see into is never offered for release.
    backend.set_peer_state(id, vss::gui::peer_state::open);
    vss::gui::capabilities none;
    backend.set_capabilities(none);
    panel.refresh();
    QApplication::processEvents();
    REQUIRE(!panel.release_button()->isEnabled());
}
