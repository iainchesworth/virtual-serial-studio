//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "test_framework.hpp"

#include "backend/fake_studio_backend.hpp"
#include "shell/shell_window.hpp"
#include "theme/theme.hpp"
#include "topology/links_list.hpp"
#include "topology/node_item.hpp"
#include "topology/topology_layout.hpp"
#include "topology/topology_text.hpp"
#include "topology/topology_view.hpp"
#include "widgets/notes.hpp"

#include <vss/config/document.hpp>

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDir>
#include <QDockWidget>
#include <QFontMetricsF>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace topo = vss::gui::topology;

namespace
{

void clear_settings()
{
    QSettings settings;
    settings.clear();
}

/// The sample setup of the Workspace mockup: device plus three virtual ports.
struct graph_fixture
{
    vss::gui::FakeStudioBackend backend;
    vss::endpoint_id device = 0;
    vss::endpoint_id terminal = 0;
    vss::endpoint_id test_tool = 0;
    vss::endpoint_id logger = 0;
    vss::link_id link = 0;

    graph_fixture()
    {
        if (const auto created = backend.attach_serial(
                QStringLiteral("device"), QStringLiteral("serial://COM3"), {},
                true))
        {
            device = *created;
        }
        if (const auto created = backend.create_virtual_port(
                QStringLiteral("terminal"), vss::virtual_port_kind::pty, true))
        {
            terminal = *created;
        }
        if (const auto created = backend.create_virtual_port(
                QStringLiteral("test-tool"), vss::virtual_port_kind::pty, true))
        {
            test_tool = *created;
        }
        if (const auto created = backend.create_virtual_port(
                QStringLiteral("logger"), vss::virtual_port_kind::loopback,
                true))
        {
            logger = *created;
        }
        if (const auto created =
                backend.split(device, {terminal, test_tool, logger}, {}))
        {
            link = *created;
        }
    }
};

void click(QWidget* target, const QPoint& position)
{
    const QPointF local(position);
    const QPointF global(target->mapToGlobal(position));
    QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(target, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, local, global,
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(target, &release);
    QApplication::processEvents();
}

void double_click(QWidget* target, const QPoint& position)
{
    click(target, position);
    const QPointF local(position);
    const QPointF global(target->mapToGlobal(position));
    QMouseEvent press(QEvent::MouseButtonDblClick, local, global,
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(target, &press);
    QApplication::processEvents();
}

void send_key(QWidget* target, int key,
              Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    QKeyEvent press(QEvent::KeyPress, key, modifiers);
    QApplication::sendEvent(target, &press);
    QKeyEvent release(QEvent::KeyRelease, key, modifiers);
    QApplication::sendEvent(target, &release);
    QApplication::processEvents();
}

const topo::topo_node_placement* placement(const topo::topology_layout& layout,
                                           vss::endpoint_id id)
{
    return layout.find_node(id);
}

QVector<topo::topo_node_input> equal_nodes(int count)
{
    QVector<topo::topo_node_input> nodes;
    for (int index = 1; index <= count; ++index)
        nodes.append(
            {static_cast<vss::endpoint_id>(index), QSizeF(168.0, 56.0)});
    return nodes;
}

} // namespace

TEST_CASE("topology: empty state, hint and accessible summary")
{
    topo::TopologyView view;
    view.resize(800, 600);
    view.show();
    QApplication::processEvents();

    REQUIRE_EQ(view.node_count(), 0);
    REQUIRE_EQ(view.link_count(), 0);
    REQUIRE_EQ(view.accessibleName(), QStringLiteral("Topology"));
    REQUIRE_EQ(view.accessibleDescription(),
               QStringLiteral("No ports yet. Add a port to see it here."));
    REQUIRE_EQ(view.summary(),
               QStringLiteral("No ports yet. Add a port to see it here."));

    auto* empty = view.findChild<QLabel*>(QStringLiteral("topologyEmptyState"));
    REQUIRE(empty != nullptr);
    REQUIRE(empty->isVisible());
    REQUIRE_EQ(empty->text(), QStringLiteral("Add a port to see it here."));
    REQUIRE_EQ(empty->accessibleName(),
               QStringLiteral("Add a port to see it here."));

    auto* hint = view.findChild<QLabel*>(QStringLiteral("topologyHint"));
    REQUIRE(hint != nullptr);
    REQUIRE_EQ(hint->text(),
               QStringLiteral("Click a box to inspect it. Click a line to "
                              "select the link."));
    REQUIRE_EQ(hint->accessibleName(),
               QStringLiteral("Click a box to inspect it. Click a line to "
                              "select the link."));

    view.set_topology({}, {});
    REQUIRE(empty->isVisible());
    view.close();
}

TEST_CASE("topology: nodes, sizes, link names and the generated summary")
{
    graph_fixture fixture;
    REQUIRE_NE(fixture.link, 0);

    topo::TopologyView view;
    view.resize(1000, 700);
    view.show();
    view.set_topology(fixture.backend.ports(), fixture.backend.links());
    QApplication::processEvents();

    REQUIRE_EQ(view.node_count(), 4);
    REQUIRE_EQ(view.link_count(), 1);
    REQUIRE(view.node_is_device(fixture.device));
    REQUIRE(!view.node_is_device(fixture.terminal));
    REQUIRE_EQ(view.node_size(fixture.device).height(), 64.0);
    REQUIRE(view.node_size(fixture.terminal).height() >= 56.0);
    REQUIRE(view.node_size(fixture.terminal).width() >= 168.0);
    REQUIRE_EQ(view.summary(),
               QStringLiteral("4 ports, 1 link: split from device to terminal, "
                              "test-tool and logger"));
    REQUIRE_EQ(view.accessibleDescription(), view.summary());
    REQUIRE_EQ(view.link_description(fixture.link),
               QStringLiteral("Split link, device to 3 ports, two-way"));
    REQUIRE_EQ(view.link_descriptions().size(), 1);
    REQUIRE_EQ(view.node_badges(fixture.device).size(), 0);

    // The split link's visual labels are Split above and two-way below.
    REQUIRE_EQ(vss::gui::topology_text::link_primary_label(
                   vss::gui::link_shape::split),
               QStringLiteral("Split"));
    REQUIRE_EQ(vss::gui::topology_text::link_primary_label(
                   vss::gui::link_shape::two_way),
               QStringLiteral("two-way"));
    REQUIRE_EQ(
        vss::gui::topology_text::link_primary_label(vss::gui::link_shape::join),
        QStringLiteral("Join"));
    REQUIRE_EQ(vss::gui::topology_text::link_primary_label(
                   vss::gui::link_shape::monitor),
               QStringLiteral("monitor"));
    REQUIRE_EQ(vss::gui::topology_text::link_primary_label(
                   vss::gui::link_shape::one_way),
               QString());

    view.close();
}

TEST_CASE("topology: owner, dropped and unlinked badges")
{
    std::vector<vss::gui::port_view> ports(3);
    ports[0].id = 1;
    ports[0].name = QStringLiteral("device");
    ports[0].path = QStringLiteral("COM1");
    ports[0].kind = vss::endpoint_kind::virtual_port;
    ports[0].stats.dropped = 3;
    ports[1].id = 2;
    ports[1].name = QStringLiteral("recipe-tool");
    ports[1].path = QStringLiteral("COM9");
    ports[1].kind = vss::endpoint_kind::virtual_port;
    ports[2].id = 3;
    ports[2].name = QStringLiteral("modem");
    ports[2].path = QStringLiteral("scripted");
    ports[2].kind = vss::endpoint_kind::mock;
    ports[2].studio_only = true;

    vss::gui::link_view link;
    link.id = 1;
    link.shape = vss::gui::link_shape::join;
    link.mode = vss::link_mode::one_way;
    link.sources = {2};
    link.targets = {1};
    link.options.policy = vss::merge_policy::owner;
    link.owner = 2;

    topo::TopologyView view;
    view.set_topology(ports, {link});

    REQUIRE_EQ(view.node_badges(2), QStringList{QStringLiteral("Owner")});
    REQUIRE_EQ(view.node_badges(1), QStringList{QStringLiteral("Dropped")});
    REQUIRE_EQ(view.node_badges(3), QStringList{QStringLiteral("not linked")});
    REQUIRE(view.node_is_unlinked(3));
    REQUIRE(!view.node_is_unlinked(1));
    // The target of a join uses the taller 64 px box even when it is virtual.
    REQUIRE_EQ(view.node_size(1).height(), 64.0);
    REQUIRE_EQ(view.node_size(2).height(), 56.0);
}

TEST_CASE("topology: layout for split, join, straight, chain and unlinked")
{
    const topo::topology_metrics metrics;
    const QVector<topo::topo_node_input> nodes = equal_nodes(6);

    // Split: one source, three targets with one bar in the gap.
    {
        const QVector<topo::topo_link_input> links{
            {1, vss::gui::link_shape::split, {1}, {2, 3, 4}, false}};
        const topo::topology_layout layout =
            topo::compute_topology_layout(equal_nodes(4), links, metrics);
        REQUIRE_EQ(layout.links.size(), 1);
        const topo::topo_link_placement& link = layout.links.first();
        REQUIRE(!link.bar.isNull());
        REQUIRE_EQ(link.segments.size(), 4);

        const QRectF source = layout.node_rect(1);
        const QRectF first = layout.node_rect(2);
        const QRectF second = layout.node_rect(3);
        const QRectF third = layout.node_rect(4);
        REQUIRE(source.right() < first.left());
        REQUIRE_NEAR(first.left() - source.right(), metrics.horizontal_gap,
                     0.01);
        REQUIRE_NEAR(second.top() - first.bottom(), metrics.vertical_gap, 0.01);
        REQUIRE_NEAR(third.top() - second.bottom(), metrics.vertical_gap, 0.01);
        REQUIRE(link.bar.x1() > source.right());
        REQUIRE(link.bar.x1() < first.left());
        const qreal group_centre = (first.top() + third.bottom()) / 2.0;
        REQUIRE_NEAR(source.center().y(), group_centre, 0.5);
        REQUIRE_NEAR(link.primary_anchor.x(),
                     (source.right() + link.bar.x1()) / 2.0, 0.01);
    }

    // Join mirrors the split: two sources into one target.
    {
        const QVector<topo::topo_node_input> join_nodes{
            {2, QSizeF(168.0, 56.0)},
            {3, QSizeF(168.0, 56.0)},
            {4, QSizeF(168.0, 56.0)}};
        const QVector<topo::topo_link_input> links{
            {1, vss::gui::link_shape::join, {2, 3}, {4}, false}};
        const topo::topology_layout layout =
            topo::compute_topology_layout(join_nodes, links, metrics);
        REQUIRE_EQ(layout.links.size(), 1);
        const topo::topo_link_placement& link = layout.links.first();
        REQUIRE(!link.bar.isNull());
        REQUIRE_EQ(link.segments.size(), 3);

        const QRectF first = layout.node_rect(2);
        const QRectF second = layout.node_rect(3);
        const QRectF target = layout.node_rect(4);
        REQUIRE(first.right() < target.left());
        REQUIRE_NEAR(second.top() - first.bottom(), metrics.vertical_gap, 0.01);
        const qreal group_centre = (first.top() + second.bottom()) / 2.0;
        REQUIRE_NEAR(target.center().y(), group_centre, 0.5);
    }

    // Two-way: one straight line, no arrow head.
    {
        const QVector<topo::topo_link_input> links{
            {1, vss::gui::link_shape::two_way, {1}, {2}, false}};
        const topo::topology_layout layout =
            topo::compute_topology_layout(equal_nodes(2), links, metrics);
        REQUIRE_EQ(layout.links.first().segments.size(), 1);
        REQUIRE(layout.links.first().bar.isNull());
        REQUIRE(layout.links.first().arrows.isEmpty());
        REQUIRE_NEAR(layout.node_rect(1).center().y(),
                     layout.node_rect(2).center().y(), 0.5);
    }

    // One-way and monitor end in a filled arrow head.
    for (const vss::gui::link_shape shape :
         {vss::gui::link_shape::one_way, vss::gui::link_shape::monitor})
    {
        const QVector<topo::topo_link_input> links{{1, shape, {1}, {2}, true}};
        const topo::topology_layout layout =
            topo::compute_topology_layout(equal_nodes(2), links, metrics);
        REQUIRE_EQ(layout.links.first().arrows.size(), 1);
        REQUIRE_EQ(layout.links.first().segments.size(), 1);
    }

    // A chain puts one column per hop, left to right.
    {
        const QVector<topo::topo_link_input> links{
            {1, vss::gui::link_shape::two_way, {1}, {2}, false},
            {2, vss::gui::link_shape::one_way, {2}, {3}, true}};
        const topo::topology_layout layout =
            topo::compute_topology_layout(equal_nodes(3), links, metrics);
        REQUIRE(layout.node_rect(1).right() < layout.node_rect(2).left());
        REQUIRE(layout.node_rect(2).right() < layout.node_rect(3).left());
    }

    // An unlinked port gets its own column after the graph, clear of the
    // linked nodes.
    {
        const QVector<topo::topo_link_input> links{
            {1, vss::gui::link_shape::two_way, {1}, {2}, false}};
        const topo::topology_layout layout =
            topo::compute_topology_layout(nodes, links, metrics);
        REQUIRE(placement(layout, 5) != nullptr);
        const QRectF linked = layout.node_rect(2);
        const QRectF unlinked = layout.node_rect(5);
        REQUIRE(unlinked.left() >= linked.right() + metrics.horizontal_gap);
        REQUIRE(!unlinked.intersects(linked));
    }
}

TEST_CASE("topology: mouse selection of nodes, links and empty canvas")
{
    graph_fixture fixture;
    topo::TopologyView view;
    view.resize(1000, 700);
    view.show();
    view.set_topology(fixture.backend.ports(), fixture.backend.links());
    QApplication::processEvents();

    vss::endpoint_id selected_port = 0;
    vss::link_id selected_link = 0;
    QObject::connect(&view, &topo::TopologyView::port_selected,
                     [&selected_port](vss::endpoint_id id)
                     { selected_port = id; });
    QObject::connect(&view, &topo::TopologyView::link_selected,
                     [&selected_link](vss::link_id id) { selected_link = id; });

    click(view.viewport(),
          view.mapFromScene(view.node_rect(fixture.terminal).center()));
    REQUIRE_EQ(view.selected_port(), fixture.terminal);
    REQUIRE_EQ(selected_port, fixture.terminal);
    REQUIRE_EQ(view.selected_link(), 0);
    REQUIRE_EQ(selected_link, 0);

    const QPoint link_position =
        view.mapFromScene(view.link_anchor(fixture.link));
    click(view.viewport(), link_position);
    REQUIRE_EQ(view.selected_link(), fixture.link);
    REQUIRE_EQ(selected_link, fixture.link);
    REQUIRE_EQ(view.selected_port(), 0);
    REQUIRE_EQ(selected_port, 0);

    // Clicking the background clears the selection; the empty corner is far
    // from every item.
    click(view.viewport(), QPoint(6, 6));
    REQUIRE_EQ(view.selected_port(), 0);
    REQUIRE_EQ(view.selected_link(), 0);

    view.close();
}

TEST_CASE("topology: Tab visits nodes then links, Enter and Delete act")
{
    graph_fixture fixture;
    topo::TopologyView view;
    view.resize(1000, 700);
    view.show();
    view.set_topology(fixture.backend.ports(), fixture.backend.links());
    QApplication::processEvents();

    send_key(&view, Qt::Key_Tab);
    REQUIRE_NE(view.focused_port(), 0);

    QVector<vss::endpoint_id> nodes;
    nodes.append(view.focused_port());
    for (int index = 1; index < view.node_count(); ++index)
    {
        send_key(&view, Qt::Key_Tab);
        REQUIRE_NE(view.focused_port(), 0);
        REQUIRE_EQ(view.focused_link(), 0);
        nodes.append(view.focused_port());
    }
    std::sort(nodes.begin(), nodes.end());
    nodes.erase(std::unique(nodes.begin(), nodes.end()), nodes.end());
    REQUIRE_EQ(nodes.size(), view.node_count());

    send_key(&view, Qt::Key_Tab);
    REQUIRE_EQ(view.focused_port(), 0);
    REQUIRE_EQ(view.focused_link(), fixture.link);
    REQUIRE_EQ(view.selected_link(), fixture.link);

    // Forward past the last item hands focus back to the window.
    send_key(&view, Qt::Key_Tab);
    REQUIRE_EQ(view.focused_port(), 0);
    REQUIRE_EQ(view.focused_link(), 0);

    // Backwards from outside enters at the last item again.
    send_key(&view, Qt::Key_Backtab);
    REQUIRE_EQ(view.focused_link(), fixture.link);

    vss::endpoint_id inspected = 0;
    vss::link_id removed_link = 0;
    vss::endpoint_id removed_port = 0;
    QObject::connect(&view, &topo::TopologyView::inspect_requested,
                     [&inspected](vss::endpoint_id id) { inspected = id; });
    QObject::connect(&view, &topo::TopologyView::remove_link_requested,
                     [&removed_link](vss::link_id id) { removed_link = id; });
    QObject::connect(&view, &topo::TopologyView::remove_port_requested,
                     [&removed_port](vss::endpoint_id id)
                     { removed_port = id; });

    // Back to the first node with Shift+Tab through the links.
    send_key(&view, Qt::Key_Backtab);
    REQUIRE_NE(view.focused_port(), 0);
    send_key(&view, Qt::Key_Return);
    REQUIRE_NE(inspected, 0);
    REQUIRE_EQ(inspected, view.focused_port());

    const vss::endpoint_id focused = view.focused_port();
    const QRectF before = view.node_rect(focused);
    send_key(&view, Qt::Key_Right);
    REQUIRE_NEAR(view.node_rect(focused).left(), before.left() + 8.0, 0.01);
    send_key(&view, Qt::Key_Right, Qt::ShiftModifier);
    REQUIRE_NEAR(view.node_rect(focused).left(), before.left() + 40.0, 0.01);
    send_key(&view, Qt::Key_Down);
    REQUIRE_NEAR(view.node_rect(focused).top(), before.top() + 8.0, 0.01);

    send_key(&view, Qt::Key_Delete);
    REQUIRE_EQ(removed_port, focused);
    REQUIRE_EQ(removed_link, 0);

    send_key(&view, Qt::Key_Tab);
    send_key(&view, Qt::Key_Delete);
    REQUIRE_EQ(removed_link, fixture.link);

    view.close();
}

TEST_CASE("topology: double-click and the context menu")
{
    graph_fixture fixture;
    topo::TopologyView view;
    view.resize(1000, 700);
    view.show();
    view.set_topology(fixture.backend.ports(), fixture.backend.links());
    QApplication::processEvents();

    vss::endpoint_id inspected = 0;
    vss::endpoint_id copied = 0;
    vss::endpoint_id connected = 0;
    QObject::connect(&view, &topo::TopologyView::inspect_requested,
                     [&inspected](vss::endpoint_id id) { inspected = id; });
    QObject::connect(&view, &topo::TopologyView::copy_name_requested,
                     [&copied](vss::endpoint_id id) { copied = id; });
    QObject::connect(&view, &topo::TopologyView::connect_requested,
                     [&connected](vss::endpoint_id id) { connected = id; });

    double_click(view.viewport(),
                 view.mapFromScene(view.node_rect(fixture.terminal).center()));
    REQUIRE_EQ(inspected, fixture.terminal);

    view.setFocus(Qt::TabFocusReason);
    send_key(&view, Qt::Key_Right); // make sure a node has the focus
    const QStringList expected = view.node_menu_labels(view.focused_port());
    QStringList wanted;
    wanted << QStringLiteral("Inspect") << QStringLiteral("Copy name")
           << QStringLiteral("Connect from here") << QStringLiteral("Remove");
    REQUIRE_EQ(expected, wanted);
    REQUIRE(!expected.contains(QStringLiteral("Rename")));

    send_key(&view, Qt::Key_F10, Qt::ShiftModifier);
    QMenu* menu = view.findChild<QMenu*>();
    REQUIRE(menu != nullptr);
    QStringList labels;
    for (const QAction* action : menu->actions())
    {
        if (!action->isSeparator())
            labels.append(action->text());
    }
    REQUIRE_EQ(labels, expected);

    const vss::endpoint_id focused = view.focused_port();
    menu->actions().first()->trigger();
    REQUIRE_EQ(inspected, focused);
    for (QAction* action : menu->actions())
    {
        if (action->text() == QStringLiteral("Copy name"))
            action->trigger();
        else if (action->text() == QStringLiteral("Connect from here"))
            action->trigger();
    }
    REQUIRE_EQ(copied, focused);
    REQUIRE_EQ(connected, focused);
    menu->close();
    QApplication::processEvents();

    // The link menu only offers Remove link.
    REQUIRE_EQ(view.link_menu_labels(fixture.link),
               QStringList{QStringLiteral("Remove link")});
    view.close();
}

TEST_CASE("topology: fit, zoom and Ctrl+wheel stay inside the limits")
{
    graph_fixture fixture;
    topo::TopologyView view;
    view.resize(800, 600);
    view.show();
    view.set_topology(fixture.backend.ports(), fixture.backend.links());
    QApplication::processEvents();

    view.fit_content();
    const qreal fitted = view.zoom();
    REQUIRE(fitted >= 0.25);
    REQUIRE(fitted <= 1.25 + 0.001);

    const QPoint centre =
        view.mapFromScene(view.scene()->itemsBoundingRect().center());
    REQUIRE(std::abs(centre.x() - view.viewport()->width() / 2) < 40);
    REQUIRE(std::abs(centre.y() - view.viewport()->height() / 2) < 40);

    view.zoom_in();
    REQUIRE(view.zoom() > fitted);
    const qreal zoomed = view.zoom();
    view.zoom_out();
    REQUIRE(view.zoom() < zoomed);

    for (int index = 0; index < 20; ++index)
        view.zoom_out();
    REQUIRE_NEAR(view.zoom(), 0.25, 0.001);

    view.fit_content();
    const qreal before_wheel = view.zoom();
    QWheelEvent wheel(QPointF(200.0, 200.0),
                      QPointF(view.viewport()->mapToGlobal(QPoint(200, 200))),
                      QPoint(), QPoint(0, 120), Qt::NoButton,
                      Qt::ControlModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(view.viewport(), &wheel);
    QApplication::processEvents();
    REQUIRE(view.zoom() > before_wheel);

    // Ctrl+0 fits again.
    view.zoom_in();
    QKeyEvent ctrl_zero(QEvent::KeyPress, Qt::Key_0, Qt::ControlModifier);
    QApplication::sendEvent(&view, &ctrl_zero);
    QApplication::processEvents();
    REQUIRE_NEAR(view.zoom(), fitted, 0.001);

    view.close();
}

TEST_CASE("topology: compact nodes at the minimum window and 200% text")
{
    graph_fixture fixture;
    topo::TopologyView view;
    view.resize(440, 400);
    view.show();
    view.set_topology(fixture.backend.ports(), fixture.backend.links());
    QApplication::processEvents();

    REQUIRE(view.is_compact());
    REQUIRE(view.node_size(fixture.terminal).height() >= 48.0);
    REQUIRE(view.node_size(fixture.terminal).height() <= 52.0);

    view.resize(1000, 700);
    QApplication::processEvents();
    REQUIRE(!view.is_compact());
    REQUIRE(view.node_size(fixture.terminal).height() >= 56.0);

    // At 200 percent text the box grows instead of clipping the name.
    topo::node_data data;
    data.name = QStringLiteral("test-tool");
    data.detail = QStringLiteral("COM8");
    data.detail_mono = true;

    const QFont name_font =
        vss::gui::Theme::instance().font(vss::gui::text_style::label);
    const QFont detail_font =
        vss::gui::Theme::instance().font(vss::gui::text_style::data);
    const QFont badge_font =
        vss::gui::Theme::instance().font(vss::gui::text_style::caption);

    const QSizeF normal =
        topo::node_size_for(data, false, name_font, detail_font, badge_font);
    REQUIRE(normal.height() >= 56.0);

    QFont large_name = name_font;
    large_name.setPointSizeF(name_font.pointSizeF() * 2.0);
    QFont large_detail = detail_font;
    large_detail.setPointSizeF(detail_font.pointSizeF() * 2.0);
    const QSizeF large =
        topo::node_size_for(data, false, large_name, large_detail, badge_font);
    REQUIRE(large.height() > normal.height());
    REQUIRE(large.width() >= normal.width());
    const QFontMetricsF metrics(large_name);
    REQUIRE(metrics.horizontalAdvance(data.name) <= large.width() - 24.0 + 0.5);

    view.close();
}

TEST_CASE("topology: links list exposes every link with a 24px target")
{
    graph_fixture fixture;
    topo::LinksList list;
    list.resize(420, 240);
    list.show();
    list.set_links(fixture.backend.links(), fixture.backend.ports());
    QApplication::processEvents();

    REQUIRE_EQ(list.count(), 1);
    REQUIRE_EQ(list.accessibleName(), QStringLiteral("Links"));
    REQUIRE_EQ(
        list.item_texts(),
        QStringList{QStringLiteral("Split link, device to 3 ports, two-way")});

    auto* widget = list.findChild<QListWidget*>();
    REQUIRE(widget != nullptr);
    REQUIRE_EQ(widget->accessibleName(), QStringLiteral("Links"));
    REQUIRE_EQ(widget->count(), 1);
    REQUIRE(widget->item(0)->sizeHint().height() >= 24);
    REQUIRE(!widget->item(0)->text().isEmpty());

    vss::link_id chosen = 0;
    QObject::connect(&list, &topo::LinksList::link_selected,
                     [&chosen](vss::link_id id) { chosen = id; });
    widget->setCurrentRow(0);
    QApplication::processEvents();
    REQUIRE_EQ(chosen, fixture.link);

    list.set_selected(fixture.link);
    REQUIRE_EQ(list.selected(), fixture.link);
    list.set_selected(0);
    REQUIRE_EQ(list.selected(), 0);

    list.set_links({}, fixture.backend.ports());
    REQUIRE_EQ(list.count(), 0);
    REQUIRE_EQ(list.item_texts(), QStringList{QStringLiteral("No links yet.")});

    list.close();
}

TEST_CASE("topology: the generated summary follows the links")
{
    graph_fixture fixture;
    topo::TopologyView view;
    view.set_topology(fixture.backend.ports(), fixture.backend.links());
    REQUIRE_EQ(view.summary(),
               QStringLiteral("4 ports, 1 link: split from device to terminal, "
                              "test-tool and logger"));

    REQUIRE(fixture.backend.disconnect(fixture.link).has_value());
    view.set_topology(fixture.backend.ports(), fixture.backend.links());
    REQUIRE_EQ(view.link_count(), 0);
    REQUIRE_EQ(view.summary(), QStringLiteral("4 ports, no links"));
    REQUIRE_EQ(view.accessibleDescription(), view.summary());
}

TEST_CASE("topology: loop refusal highlights the cycle")
{
    vss::gui::FakeStudioBackend backend;
    const auto first = backend.create_virtual_port(
        QStringLiteral("device"), vss::virtual_port_kind::loopback, true);
    const auto second = backend.create_virtual_port(
        QStringLiteral("terminal"), vss::virtual_port_kind::pty, true);
    const auto third = backend.create_virtual_port(
        QStringLiteral("logger"), vss::virtual_port_kind::pty, true);
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    REQUIRE(third.has_value());
    REQUIRE(backend.connect_ports(*first, *second, {}).has_value());
    REQUIRE(backend.connect_ports(*second, *third, {}).has_value());
    REQUIRE(backend.connect_ports(*third, *first, {}).has_value());

    topo::TopologyView view;
    view.resize(900, 640);
    view.show();
    view.set_topology(backend.ports(), backend.links());
    QApplication::processEvents();

    REQUIRE(view.show_loop_note());
    REQUIRE_EQ(view.loop_link_count(), 3);
    REQUIRE_EQ(view.loop_ports().size(), 4);
    REQUIRE_EQ(
        view.loop_note_text(),
        QStringLiteral("That link would create a loop. Data would travel from "
                       "device to terminal and back to device forever. Nothing "
                       "was changed. Remove a link first, or choose different "
                       "ports."));

    auto* note = view.findChild<vss::gui::NoteBanner*>(
        QStringLiteral("topologyLoopNote"));
    REQUIRE(note != nullptr);
    REQUIRE(note->isVisible());
    REQUIRE_EQ(note->get_kind(), vss::gui::NoteBanner::kind::danger);
    REQUIRE(note->action_button() != nullptr);
    REQUIRE_EQ(note->action_button()->text(), QStringLiteral("Show the loop"));
    note->action_button()->click();
    QApplication::processEvents();

    view.dismiss_loop_note();
    REQUIRE_EQ(view.loop_link_count(), 0);
    REQUIRE(view.show_loop_note());
    REQUIRE_EQ(view.loop_link_count(), 3);
    view.close();
}

TEST_CASE("topology: no loop note for an acyclic graph")
{
    graph_fixture fixture;
    topo::TopologyView view;
    view.set_topology(fixture.backend.ports(), fixture.backend.links());
    REQUIRE(!view.show_loop_note());
    REQUIRE_EQ(view.loop_link_count(), 0);
}

TEST_CASE("topology: a session nudge survives a refresh and stays out of JSON")
{
    graph_fixture fixture;
    topo::TopologyView view;
    view.resize(1000, 700);
    view.show();
    view.set_topology(fixture.backend.ports(), fixture.backend.links());
    QApplication::processEvents();

    send_key(&view, Qt::Key_Tab);
    const vss::endpoint_id focused = view.focused_port();
    REQUIRE_NE(focused, 0);
    const QPointF before = view.node_rect(focused).topLeft();
    send_key(&view, Qt::Key_Right);
    send_key(&view, Qt::Key_Down);
    const QPointF nudged = view.node_rect(focused).topLeft();
    REQUIRE_NEAR(nudged.x(), before.x() + 8.0, 0.01);
    REQUIRE_NEAR(nudged.y(), before.y() + 8.0, 0.01);

    view.set_topology(fixture.backend.ports(), fixture.backend.links());
    QApplication::processEvents();
    const QPointF kept = view.node_rect(focused).topLeft();
    REQUIRE_NEAR(kept.x(), nudged.x(), 0.01);
    REQUIRE_NEAR(kept.y(), nudged.y(), 0.01);

    const auto json = vss::config::to_json(fixture.backend.snapshot());
    REQUIRE(json.has_value());
    REQUIRE(
        !QString::fromStdString(*json).contains(QStringLiteral("position")));

    view.close();
}

TEST_CASE("topology: light and dark themes repaint from Theme tokens")
{
    graph_fixture fixture;
    topo::TopologyView view;
    view.resize(900, 640);
    view.show();
    view.set_topology(fixture.backend.ports(), fixture.backend.links());

    auto& theme = vss::gui::Theme::instance();
    theme.set_mode(vss::gui::theme_mode::light);
    QApplication::processEvents();
    const QImage light = view.grab().toImage();
    REQUIRE(!light.isNull());
    REQUIRE_EQ(light.pixelColor(light.width() - 4, 4),
               theme.colour(vss::gui::colour_role::panel));

    theme.set_mode(vss::gui::theme_mode::dark);
    QApplication::processEvents();
    const QImage dark = view.grab().toImage();
    REQUIRE(!dark.isNull());
    REQUIRE_EQ(dark.pixelColor(dark.width() - 4, 4),
               theme.colour(vss::gui::colour_role::panel));
    REQUIRE(light != dark);

    theme.set_mode(vss::gui::theme_mode::light);
    QApplication::processEvents();
    view.close();
}

TEST_CASE("topology: names and 24px targets on every interactive item")
{
    graph_fixture fixture;
    topo::TopologyView view;
    view.resize(900, 640);
    view.show();
    view.set_topology(fixture.backend.ports(), fixture.backend.links());
    QApplication::processEvents();

    REQUIRE(!view.accessibleName().isEmpty());
    REQUIRE(!view.accessibleDescription().isEmpty());
    REQUIRE_EQ(view.accessibleName(), QStringLiteral("Topology"));

    for (const auto& port : fixture.backend.ports())
    {
        const QRectF rect = view.node_rect(port.id);
        REQUIRE(rect.isValid());
        REQUIRE(rect.width() >= 24.0);
        REQUIRE(rect.height() >= 24.0);
        REQUIRE(!view.link_descriptions().isEmpty() || view.link_count() == 0);
    }

    REQUIRE_EQ(view.link_descriptions().size(), view.link_count());
    REQUIRE(!view.link_description(fixture.link).isEmpty());
    view.close();
}

TEST_CASE("shell: the topology replaces the placeholder and shares selection")
{
    clear_settings();
    graph_fixture fixture;
    vss::gui::ShellWindow window(fixture.backend);
    window.resize(1280, 800);
    window.show();
    QApplication::processEvents();

    auto* view = window.findChild<topo::TopologyView*>();
    REQUIRE(view != nullptr);
    REQUIRE_EQ(view->node_count(), 4);
    REQUIRE_EQ(view->link_count(), 1);

    QAction* remove_link =
        window.findChild<QAction*>(QStringLiteral("topology.removeLink"));
    REQUIRE(remove_link != nullptr);
    REQUIRE(!remove_link->isEnabled());
    QAction* edit_remove =
        window.findChild<QAction*>(QStringLiteral("edit.remove"));
    REQUIRE(edit_remove != nullptr);
    // WP3's Ports dock selects the first port when the workspace opens.
    REQUIRE(edit_remove->isEnabled());

    click(view->viewport(),
          view->mapFromScene(view->node_rect(fixture.terminal).center()));
    REQUIRE_EQ(view->selected_port(), fixture.terminal);
    REQUIRE(edit_remove->isEnabled());
    REQUIRE(!remove_link->isEnabled());

    click(view->viewport(),
          view->mapFromScene(view->link_anchor(fixture.link)));
    REQUIRE_EQ(view->selected_link(), fixture.link);
    REQUIRE(remove_link->isEnabled());

    auto* links_action =
        window.findChild<QAction*>(QStringLiteral("view.links"));
    REQUIRE(links_action != nullptr);
    auto* links_dock =
        window.findChild<QDockWidget*>(QStringLiteral("linksDock"));
    REQUIRE(links_dock != nullptr);
    REQUIRE(!links_dock->isVisible());
    links_action->trigger();
    QApplication::processEvents();
    REQUIRE(links_dock->isVisible());

    auto* list = window.findChild<topo::LinksList*>();
    REQUIRE(list != nullptr);
    REQUIRE_EQ(list->count(), 1);
    auto* list_widget = list->findChild<QListWidget*>();
    REQUIRE(list_widget != nullptr);

    // The list drives the shared selection too.
    click(view->viewport(),
          view->mapFromScene(view->node_rect(fixture.terminal).center()));
    REQUIRE(!remove_link->isEnabled());
    list_widget->setCurrentRow(0);
    QApplication::processEvents();
    REQUIRE(view->selected_link() == fixture.link);
    REQUIRE(remove_link->isEnabled());

    window.close();
    clear_settings();
}

TEST_CASE("shell: topology actions name the next work package")
{
    clear_settings();
    graph_fixture fixture;
    vss::gui::ShellWindow window(fixture.backend);
    window.resize(1280, 800);
    window.show();
    QApplication::processEvents();

    auto* view = window.findChild<topo::TopologyView*>();
    REQUIRE(view != nullptr);

    QAction* connect =
        window.findChild<QAction*>(QStringLiteral("topology.connect"));
    QAction* share =
        window.findChild<QAction*>(QStringLiteral("topology.share"));
    QAction* watch =
        window.findChild<QAction*>(QStringLiteral("topology.watch"));
    QAction* wiring =
        window.findChild<QAction*>(QStringLiteral("topology.wiring"));
    QAction* owner =
        window.findChild<QAction*>(QStringLiteral("topology.owner"));
    QAction* fit = window.findChild<QAction*>(QStringLiteral("topology.fit"));
    REQUIRE(connect != nullptr);
    REQUIRE(share != nullptr);
    REQUIRE(watch != nullptr);
    REQUIRE(wiring != nullptr);
    REQUIRE(owner != nullptr);
    REQUIRE(fit != nullptr);
    REQUIRE(connect->isEnabled());

    connect->trigger();
    REQUIRE(
        window.statusBar()->currentMessage().contains(QStringLiteral("WP6")));
    share->trigger();
    REQUIRE(
        window.statusBar()->currentMessage().contains(QStringLiteral("WP6")));
    watch->trigger();
    REQUIRE(
        window.statusBar()->currentMessage().contains(QStringLiteral("WP6")));
    // WP7 has replaced the Wiring and Owner placeholders with modal tools;
    // they are covered by the advanced suite.  Do not open a modal here.
    REQUIRE(wiring != nullptr);
    REQUIRE(owner != nullptr);

    const qreal before = view->zoom();
    fit->trigger();
    QApplication::processEvents();
    REQUIRE(view->zoom() > 0.0);
    REQUIRE(before > 0.0);

    window.close();
    clear_settings();
}
