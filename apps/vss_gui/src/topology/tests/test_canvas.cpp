//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

// The Advanced canvas editor: toolbox, Add sheet, wiring, multi-select,
// Settings, Duplicate, Delete and undo.  See
// docs/gui-design/docs/screens/07-canvas-editor.md.

#include "test_framework.hpp"

#include "advanced/link_settings_dialog.hpp"
#include "advanced/port_settings_dialog.hpp"
#include "backend/canvas_edit.hpp"
#include "backend/fake_studio_backend.hpp"
#include "shell/canvas_controller.hpp"
#include "shell/shell_window.hpp"
#include "shell/undo_service.hpp"
#include "topology/add_sheet.hpp"
#include "topology/node_item.hpp"
#include "topology/toolbox.hpp"
#include "topology/topology_view.hpp"

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>

namespace topo = vss::gui::topology;
using vss::gui::canvas_item;
using vss::gui::slot_kind;
using vss::gui::wire_kind;

namespace
{

void clear_canvas_settings()
{
    QSettings settings;
    settings.clear();
}

void mouse(QWidget* target, QEvent::Type type, const QPoint& position,
           Qt::MouseButton button, Qt::MouseButtons buttons,
           Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    QMouseEvent event(type, QPointF(position),
                      QPointF(target->mapToGlobal(position)), button, buttons,
                      modifiers);
    QApplication::sendEvent(target, &event);
    QApplication::processEvents();
}

void press_key(QWidget* target, int key,
               Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    QKeyEvent press(QEvent::KeyPress, key, modifiers);
    QApplication::sendEvent(target, &press);
    QKeyEvent release(QEvent::KeyRelease, key, modifiers);
    QApplication::sendEvent(target, &release);
    QApplication::processEvents();
}

QStringList action_texts(const QMenu* menu)
{
    QStringList out;
    for (const QAction* action : menu->actions())
    {
        if (!action->isSeparator())
            out.append(action->text());
    }
    return out;
}

/// Two linked fixtures and a lone port.
struct canvas_fixture
{
    vss::gui::FakeStudioBackend backend;
    vss::endpoint_id rx = 0;
    vss::endpoint_id tx = 0;
    vss::endpoint_id lone = 0;

    canvas_fixture()
    {
        vss::gui::capabilities caps = backend.caps();
        caps.port_settings = true;
        backend.set_capabilities(caps);
        rx = *backend.create_mock_port(QStringLiteral("rx-sim"), {},
                                       QStringLiteral("scripted"));
        tx = *backend.create_mock_port(QStringLiteral("tx-sim"), {},
                                       QStringLiteral("scripted"));
        lone = *backend.create_auto_port(QStringLiteral("port-1"), true);
        REQUIRE(backend.connect_ports(rx, tx, {}));
    }
};

std::optional<vss::gui::port_view> named(const vss::gui::StudioBackend& backend,
                                         const QString& name)
{
    for (const auto& port : backend.ports())
    {
        if (port.name == name)
            return port;
    }
    return std::nullopt;
}

} // namespace

TEST_CASE("canvas: editing adds Settings, Duplicate and Delete to the menus")
{
    canvas_fixture fixture;
    topo::TopologyView view;
    view.resize(1000, 700);
    view.show();
    view.set_topology(fixture.backend.ports(), fixture.backend.links());
    QApplication::processEvents();

    REQUIRE(!view.node_menu_labels(fixture.rx).contains(QStringLiteral("Settings...")));
    view.set_editing(true);
    const QStringList labels = view.node_menu_labels(fixture.rx);
    REQUIRE_EQ(labels.first(), QStringLiteral("Inspect"));
    REQUIRE_EQ(labels.at(1), QStringLiteral("Settings..."));
    REQUIRE(labels.contains(QStringLiteral("Duplicate")));
    REQUIRE(labels.contains(QStringLiteral("Connect from here")));
    REQUIRE_EQ(labels.last(), QStringLiteral("Delete"));

    const QStringList canvas = view.canvas_menu_labels();
    REQUIRE_EQ(canvas.first(), QStringLiteral("Add port"));
    REQUIRE(canvas.contains(QStringLiteral("Add bundle")));
    REQUIRE(canvas.contains(QStringLiteral("Select all")));
    REQUIRE_EQ(canvas.last(), QStringLiteral("Fit"));

    const vss::link_id link = fixture.backend.links().front().id;
    REQUIRE(view.link_menu_labels(link).contains(QStringLiteral("Settings...")));

    // Right-click a node, choose Settings: the canvas asks for its settings.
    QVector<vss::endpoint_id> asked;
    QObject::connect(&view, &topo::TopologyView::settings_requested,
                     [&asked](const QVector<vss::endpoint_id>& ports)
                     { asked = ports; });
    const QPoint at = view.mapFromScene(view.node_rect(fixture.tx).center());
    QContextMenuEvent context(QContextMenuEvent::Mouse, at,
                              view.viewport()->mapToGlobal(at));
    QApplication::sendEvent(view.viewport(), &context);
    QApplication::processEvents();
    QMenu* menu = view.findChild<QMenu*>();
    REQUIRE(menu != nullptr);
    for (QAction* action : menu->actions())
    {
        if (action->text() == QStringLiteral("Settings..."))
            action->trigger();
    }
    menu->close();
    REQUIRE_EQ(asked, QVector<vss::endpoint_id>{fixture.tx});
}

TEST_CASE("canvas: select several with Ctrl+click, the band and Ctrl+A")
{
    canvas_fixture fixture;
    topo::TopologyView view;
    view.resize(1000, 700);
    view.show();
    view.set_editing(true);
    view.set_topology(fixture.backend.ports(), fixture.backend.links());
    QApplication::processEvents();

    const QPoint rx = view.mapFromScene(view.node_rect(fixture.rx).center());
    const QPoint lone =
        view.mapFromScene(view.node_rect(fixture.lone).center());
    mouse(view.viewport(), QEvent::MouseButtonPress, rx, Qt::LeftButton,
          Qt::LeftButton);
    mouse(view.viewport(), QEvent::MouseButtonRelease, rx, Qt::LeftButton,
          Qt::NoButton);
    mouse(view.viewport(), QEvent::MouseButtonPress, lone, Qt::LeftButton,
          Qt::LeftButton, Qt::ControlModifier);
    mouse(view.viewport(), QEvent::MouseButtonRelease, lone, Qt::LeftButton,
          Qt::NoButton, Qt::ControlModifier);
    REQUIRE_EQ(view.selected_ports().size(), 2);
    REQUIRE(view.summary().contains(QStringLiteral("2 selected")));

    QVector<vss::endpoint_id> deleted;
    QObject::connect(&view, &topo::TopologyView::delete_requested,
                     [&deleted](const QVector<vss::endpoint_id>& ports,
                                const QVector<vss::link_id>&)
                     { deleted = ports; });
    press_key(&view, Qt::Key_Delete);
    REQUIRE_EQ(deleted.size(), 2);

    press_key(&view, Qt::Key_Escape);
    REQUIRE(view.selected_ports().isEmpty());

    press_key(&view, Qt::Key_A, Qt::ControlModifier);
    REQUIRE_EQ(view.selected_ports().size(), 3);
    REQUIRE_EQ(view.selected_links().size(), 1);
    press_key(&view, Qt::Key_Escape);

    // A rubber band round every node from empty canvas.
    QRectF all = view.node_rect(fixture.rx)
                     .united(view.node_rect(fixture.tx))
                     .united(view.node_rect(fixture.lone));
    const QPoint start = view.mapFromScene(all.topLeft() - QPointF(10, 10));
    const QPoint end = view.mapFromScene(all.bottomRight() + QPointF(10, 10));
    mouse(view.viewport(), QEvent::MouseButtonPress, start, Qt::LeftButton,
          Qt::LeftButton);
    mouse(view.viewport(), QEvent::MouseMove, end, Qt::NoButton,
          Qt::LeftButton);
    mouse(view.viewport(), QEvent::MouseButtonRelease, end, Qt::LeftButton,
          Qt::NoButton);
    REQUIRE_EQ(view.selected_ports().size(), 3);
}

TEST_CASE("canvas: a wire dragged from a handle opens the link chooser")
{
    canvas_fixture fixture;
    topo::TopologyView view;
    view.resize(1000, 700);
    view.show();
    view.set_editing(true);
    view.set_topology(fixture.backend.ports(), fixture.backend.links());
    QApplication::processEvents();

    // rx and tx are linked both ways, so any second link between them closes
    // a loop, as the engine says.
    REQUIRE(view.would_loop(fixture.rx, fixture.tx));
    REQUIRE(view.would_loop(fixture.rx, fixture.tx, wire_kind::one_way));
    REQUIRE(!view.would_loop(fixture.lone, fixture.tx));

    const QRectF from = view.node_rect(fixture.lone);
    const QPoint handle =
        view.mapFromScene(QPointF(from.right(), from.center().y()));
    const QPoint target =
        view.mapFromScene(view.node_rect(fixture.tx).center());
    mouse(view.viewport(), QEvent::MouseButtonPress, handle, Qt::LeftButton,
          Qt::LeftButton);
    REQUIRE(view.is_wiring());
    REQUIRE_EQ(view.wire_source(), fixture.lone);
    mouse(view.viewport(), QEvent::MouseMove, target, Qt::NoButton,
          Qt::LeftButton);

    vss::endpoint_id wired_from = 0;
    vss::endpoint_id wired_to = 0;
    wire_kind wired = wire_kind::monitor;
    QObject::connect(&view, &topo::TopologyView::wire_requested,
                     [&](vss::endpoint_id a, vss::endpoint_id b, wire_kind k)
                     {
                         wired_from = a;
                         wired_to = b;
                         wired = k;
                     });
    mouse(view.viewport(), QEvent::MouseButtonRelease, target, Qt::LeftButton,
          Qt::NoButton);
    QMenu* chooser = view.findChild<QMenu*>();
    REQUIRE(chooser != nullptr);
    REQUIRE_EQ(action_texts(chooser),
               view.wire_menu_labels(fixture.lone, fixture.tx));
    REQUIRE_EQ(action_texts(chooser).first(), QStringLiteral("Two-way"));
    chooser->actions().first()->trigger();
    chooser->close();
    QApplication::processEvents();
    REQUIRE_EQ(wired_from, fixture.lone);
    REQUIRE_EQ(wired_to, fixture.tx);
    REQUIRE(wired == wire_kind::two_way);
    REQUIRE(!view.is_wiring());

    // The keyboard: Ctrl+L, then finish on a port.
    view.setFocus(Qt::TabFocusReason);
    view.begin_wire(fixture.lone);
    REQUIRE(view.is_wiring());
    press_key(&view, Qt::Key_Escape);
    REQUIRE(!view.is_wiring());
    view.begin_wire(fixture.lone);
    view.finish_wire(fixture.rx, wire_kind::one_way);
    REQUIRE_EQ(wired_to, fixture.rx);
    REQUIRE(wired == wire_kind::one_way);
}

TEST_CASE("canvas: the toolbox lists every tile and degrades a missing one")
{
    topo::Toolbox toolbox;
    vss::gui::capabilities caps;
    caps.shared_bus = false;
    toolbox.set_capabilities(caps);
    const QStringList labels = toolbox.tile_labels();
    REQUIRE_EQ(labels.size(), 14);
    REQUIRE_EQ(labels.first(), QStringLiteral("Port"));
    REQUIRE(labels.contains(QStringLiteral("Pair")));
    REQUIRE(labels.contains(QStringLiteral("Bench")));
    REQUIRE(!toolbox.tile_enabled(canvas_item::bus));
    REQUIRE(toolbox.tile_enabled(canvas_item::pair));

    canvas_item added = canvas_item::port;
    QObject::connect(&toolbox, &topo::Toolbox::add_requested,
                     [&added](canvas_item item) { added = item; });
    toolbox.show();
    toolbox.setFocus();
    toolbox.focus_item(canvas_item::pair);
    press_key(&toolbox, Qt::Key_Space);
    REQUIRE(added == canvas_item::pair);

    QMimeData data;
    data.setData(topo::toolbox_mime_type(), QByteArray::number(
                                                static_cast<int>(canvas_item::bench)));
    REQUIRE(topo::item_from_mime(&data) == canvas_item::bench);
}

TEST_CASE("canvas: a tile dropped on the canvas asks to add it there")
{
    topo::TopologyView view;
    view.resize(800, 600);
    view.show();
    view.set_editing(true);
    QApplication::processEvents();

    canvas_item item = canvas_item::port;
    bool at_point = false;
    QObject::connect(&view, &topo::TopologyView::add_requested,
                     [&](canvas_item value, const QPointF&, bool at)
                     {
                         item = value;
                         at_point = at;
                     });
    QMimeData data;
    data.setData(topo::toolbox_mime_type(),
                 QByteArray::number(static_cast<int>(canvas_item::pair)));
    const QPointF where(200.0, 150.0);
    QDragEnterEvent enter(where, Qt::CopyAction, &data,
                          Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &enter);
    REQUIRE(enter.isAccepted());
    QDropEvent drop(where, Qt::CopyAction, &data, Qt::LeftButton,
                    Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &drop);
    REQUIRE(item == canvas_item::pair);
    REQUIRE(at_point);

    // Not editing: the canvas takes no drops.
    view.set_editing(false);
    QDragEnterEvent refused(where, Qt::CopyAction, &data,
                            Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &refused);
    REQUIRE(!refused.isAccepted());
}

TEST_CASE("canvas: new nodes go where the tile was dropped")
{
    vss::gui::FakeStudioBackend backend;
    topo::TopologyView view;
    view.resize(1000, 700);
    view.show();
    view.set_editing(true);
    REQUIRE(backend.create_auto_port(QStringLiteral("first"), true));
    view.set_topology(backend.ports(), backend.links());

    const auto made = vss::gui::build_bundle(
        backend, vss::gui::default_request(canvas_item::pair, backend.ports()));
    REQUIRE(made.has_value());
    view.place_next_at(QPointF(400.0, 300.0));
    view.set_topology(backend.ports(), backend.links());
    const auto port = named(backend, made->created.first());
    REQUIRE(port.has_value());
    const QRectF rect = view.node_rect(port->id);
    REQUIRE(std::abs(rect.left() - 400.0) < 40.0);
    REQUIRE(std::abs(rect.top() - 300.0) < 80.0);
    REQUIRE(view.is_arranged());
}

TEST_CASE("canvas: the Add sheet names its slots and refuses a clash")
{
    vss::gui::FakeStudioBackend backend;
    REQUIRE(backend.create_auto_port(QStringLiteral("port-1"), true));

    topo::AddSheet pair(canvas_item::pair, backend.ports());
    REQUIRE_EQ(pair.slot_rows(), 2);
    REQUIRE_EQ(pair.slot_row_label(0), QStringLiteral("End A"));
    pair.set_slot_kind(0, slot_kind::new_fixture);
    pair.set_slot_kind(1, slot_kind::new_fixture);
    pair.set_slot_name(0, QStringLiteral("rx-sim"));
    pair.set_slot_name(1, QStringLiteral("tx-sim"));
    const auto request = pair.request();
    REQUIRE(request.slots.at(0).kind == slot_kind::new_fixture);
    REQUIRE_EQ(request.slots.at(1).name, QStringLiteral("tx-sim"));
    REQUIRE(pair.add_button()->isEnabled());

    pair.set_slot_name(1, QStringLiteral("rx-sim"));
    REQUIRE(!pair.add_button()->isEnabled());
    pair.set_slot_name(1, QStringLiteral("port-1"));
    REQUIRE(!pair.add_button()->isEnabled());
    pair.set_slot_kind(1, slot_kind::existing, QStringLiteral("port-1"));
    REQUIRE(pair.add_button()->isEnabled());

    topo::AddSheet split(canvas_item::split, backend.ports());
    REQUIRE_EQ(split.slot_rows(), 3);
    split.set_count(4);
    REQUIRE_EQ(split.slot_rows(), 5);
    REQUIRE_EQ(split.slot_row_label(4), QStringLiteral("Target 4"));
}

TEST_CASE("canvas: port Settings change a fixture and say what they do")
{
    canvas_fixture fixture;
    vss::gui::PortSettingsDialog dialog(fixture.backend, {fixture.rx});
    REQUIRE(dialog.note_text().contains(QStringLiteral("fixture")));
    REQUIRE_EQ(dialog.apply_button()->text(),
               QStringLiteral("Apply and restart"));
    REQUIRE(dialog.fields_enabled());
    REQUIRE(!dialog.timeouts_enabled());
    REQUIRE(dialog.summary_text().startsWith(QStringLiteral("115200 8N1")));

    vss::serial_settings slow;
    slow.baud_rate = 9600;
    slow.data_bits = 7;
    slow.parity = vss::parity_mode::even;
    dialog.set_settings(slow);
    REQUIRE(dialog.summary_text().startsWith(QStringLiteral("9600 7E1")));
    dialog.show();
    dialog.apply();
    QElapsedTimer timer;
    timer.start();
    while (dialog.isVisible() && timer.elapsed() < 5000)
        QApplication::processEvents();
    REQUIRE(dialog.result() == QDialog::Accepted);
    REQUIRE(fixture.backend.port(fixture.rx)->serial == slow);

    vss::gui::PortSettingsDialog virtual_port(fixture.backend, {fixture.lone});
    REQUIRE(virtual_port.note_text().contains(QStringLiteral("own speed")));
    REQUIRE_EQ(virtual_port.apply_button()->text(), QStringLiteral("Apply"));

    vss::gui::FakeStudioBackend limited;
    vss::gui::capabilities caps = limited.caps();
    caps.port_settings = false;
    limited.set_capabilities(caps);
    const auto fixture_id = limited.create_mock_port(
        QStringLiteral("m"), {}, QStringLiteral("scripted"));
    vss::gui::PortSettingsDialog degraded(limited, {*fixture_id});
    REQUIRE(!degraded.apply_button()->isEnabled());
}

TEST_CASE("canvas: link Settings keep the jitter within the delay")
{
    canvas_fixture fixture;
    const auto link = fixture.backend.links().front();
    vss::gui::LinkSettingsDialog dialog(link, QStringLiteral("rx-sim and tx-sim"));
    REQUIRE(dialog.modem_lines_shown());
    REQUIRE(!dialog.policy_shown());
    dialog.set_delay(10);
    dialog.set_jitter(20);
    REQUIRE(!dialog.apply_button()->isEnabled());
    dialog.set_jitter(5);
    REQUIRE(dialog.apply_button()->isEnabled());
    REQUIRE_EQ(dialog.options().delay_ms, 10u);
}

TEST_CASE("canvas: a pair of two fixtures is built from the canvas and undone")
{
    clear_canvas_settings();
    vss::gui::FakeStudioBackend backend;
    {
        vss::gui::capabilities caps = backend.caps();
        caps.port_settings = true;
        caps.undo = true;
        backend.set_capabilities(caps);
    }
    vss::gui::ShellWindow window(backend);
    window.resize(1280, 800);
    window.show();
    QApplication::processEvents();

    QAction* advanced =
        window.findChild<QAction*>(QStringLiteral("view.advanced"));
    REQUIRE(advanced != nullptr);
    if (!advanced->isChecked())
        advanced->trigger();
    QApplication::processEvents();

    // An empty Advanced setup is the empty canvas with its toolbox.
    auto* view = window.findChild<topo::TopologyView*>();
    auto* toolbox = window.findChild<topo::Toolbox*>();
    auto* canvas = window.findChild<vss::gui::CanvasController*>();
    REQUIRE(view != nullptr);
    REQUIRE(toolbox != nullptr);
    REQUIRE(canvas != nullptr);
    REQUIRE(view->is_editing());
    REQUIRE(view->isVisible());
    REQUIRE(toolbox->isVisible());

    canvas->set_sheet(
        [](canvas_item item, const QString&)
        {
            vss::gui::bundle_request request;
            request.item = item;
            request.slots = {{slot_kind::new_fixture, QStringLiteral("rx-sim")},
                             {slot_kind::new_fixture, QStringLiteral("tx-sim")}};
            return std::optional<vss::gui::bundle_request>(request);
        });
    Q_EMIT toolbox->add_requested(canvas_item::pair);
    QApplication::processEvents();
    REQUIRE_EQ(backend.ports().size(), std::size_t{2});
    REQUIRE_EQ(backend.links().size(), std::size_t{1});
    REQUIRE_EQ(view->node_count(), 2);
    REQUIRE_EQ(view->link_count(), 1);
    REQUIRE_EQ(view->selected_ports().size(), 2);
    REQUIRE(named(backend, QStringLiteral("rx-sim"))->kind ==
            vss::endpoint_kind::mock);

    QAction* undo = window.findChild<QAction*>(QStringLiteral("edit.undo"));
    QAction* redo = window.findChild<QAction*>(QStringLiteral("edit.redo"));
    REQUIRE(undo != nullptr);
    REQUIRE(redo != nullptr);
    REQUIRE(undo->isEnabled());
    undo->trigger();
    QApplication::processEvents();
    REQUIRE(backend.ports().empty());
    REQUIRE(redo->isEnabled());
    redo->trigger();
    QApplication::processEvents();
    REQUIRE_EQ(backend.ports().size(), std::size_t{2});
    REQUIRE_EQ(backend.links().size(), std::size_t{1});
    REQUIRE_EQ(view->node_count(), 2);

    // Settings from the canvas, recorded for undo.
    const auto rx = named(backend, QStringLiteral("rx-sim"));
    vss::serial_settings slow;
    slow.baud_rate = 9600;
    REQUIRE(backend.set_port_settings(rx->id, slow));
    canvas->record_settings({{QStringLiteral("rx-sim"), rx->serial}}, slow);
    undo->trigger();
    QApplication::processEvents();
    REQUIRE_EQ(named(backend, QStringLiteral("rx-sim"))->serial.baud_rate,
               115200u);

    // Duplicate, then delete everything and bring it back.
    const auto ids = view->selected_ports();
    canvas->set_confirm([](const QString&, const QString&) { return true; });
    REQUIRE(canvas->duplicate({rx->id}));
    REQUIRE(named(backend, QStringLiteral("rx-sim-copy")).has_value());
    QVector<vss::endpoint_id> all;
    for (const auto& port : backend.ports())
        all.append(port.id);
    REQUIRE(canvas->remove(all, {}));
    REQUIRE(backend.ports().empty());
    undo->trigger();
    QApplication::processEvents();
    REQUIRE_EQ(backend.ports().size(), std::size_t{3});
    REQUIRE_EQ(backend.links().size(), std::size_t{1});
    static_cast<void>(ids);

    // Closing would ask about the unsaved changes; the window just goes.
    clear_canvas_settings();
}

TEST_CASE("canvas: wiring from the canvas links two ports and undoes it")
{
    clear_canvas_settings();
    canvas_fixture fixture;
    vss::gui::UndoService undo;
    topo::TopologyView view;
    view.set_editing(true);
    view.set_topology(fixture.backend.ports(), fixture.backend.links());
    vss::gui::CanvasController canvas(fixture.backend, undo, &view, nullptr,
                                      nullptr);
    REQUIRE(canvas.wire(fixture.lone, fixture.tx, wire_kind::one_way));
    REQUIRE_EQ(fixture.backend.links().size(), std::size_t{2});
    REQUIRE(undo.undo());
    REQUIRE_EQ(fixture.backend.links().size(), std::size_t{1});
    REQUIRE(undo.redo());
    REQUIRE_EQ(fixture.backend.links().size(), std::size_t{2});

    // A wire to empty canvas makes a new fixture and links it.
    REQUIRE(canvas.wire_new(fixture.lone, slot_kind::new_fixture,
                            wire_kind::two_way));
    REQUIRE(named(fixture.backend, QStringLiteral("fixture-1")).has_value());

    // Link selected: three fresh fixtures joined into the first.
    QVector<vss::endpoint_id> fresh;
    for (const QString& name : {QStringLiteral("a"), QStringLiteral("b"),
                                QStringLiteral("c")})
        fresh.append(*fixture.backend.create_mock_port(
            name, {}, QStringLiteral("scripted")));
    const std::size_t before = fixture.backend.links().size();
    REQUIRE(canvas.link_selected(fresh, fresh.first(), canvas_item::join));
    REQUIRE_EQ(fixture.backend.links().size(), before + 1);
    REQUIRE(fixture.backend.links().back().shape ==
            vss::gui::link_shape::join);
    REQUIRE_EQ(fixture.backend.links().back().targets.first(), fresh.first());
}
