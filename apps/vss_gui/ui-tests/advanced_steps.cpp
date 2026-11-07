//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "advanced_steps.hpp"

#include <QAbstractItemModel>

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <algorithm>

namespace vss::gui::ui::adv
{

QListWidgetItem* tile(Session& session, const QString& label)
{
    auto* toolbox = session.toolbox();
    if (toolbox == nullptr || !toolbox->isVisible())
        throw vss_test::failure("the toolbox is not showing");
    const auto found = toolbox->findItems(label, Qt::MatchExactly);
    if (found.isEmpty())
        throw vss_test::failure(
            "the toolbox has no tile " + label.toStdString() + "; it has: " +
            toolbox->tile_labels().join(QStringLiteral(" | ")).toStdString());
    return found.front();
}

void expect_add_sheet(Session& session,
                      const std::function<void(topology::AddSheet*)>& fill)
{
    session.pilot().expect_type<topology::AddSheet>(
        "the Add sheet",
        [fill](QWidget* dialog)
        {
            auto* sheet = qobject_cast<topology::AddSheet*>(dialog);
            if (fill)
                fill(sheet);
            click(sheet->add_button());
        });
}

QLineEdit* sheet_name(topology::AddSheet* sheet, int which)
{
    std::vector<QLineEdit*> fields;
    for (auto* edit : sheet->findChildren<QLineEdit*>())
    {
        if (edit->isVisibleTo(sheet) &&
            edit->accessibleName().endsWith(QStringLiteral(" name")))
            fields.push_back(edit);
    }
    std::sort(fields.begin(), fields.end(),
              [sheet](QLineEdit* left, QLineEdit* right)
              {
                  return left->mapTo(sheet, QPoint(0, 0)).y() <
                         right->mapTo(sheet, QPoint(0, 0)).y();
              });
    if (which >= static_cast<int>(fields.size()))
        throw vss_test::failure("the Add sheet shows " +
                                std::to_string(fields.size()) + " name fields");
    return fields[static_cast<std::size_t>(which)];
}

void add_tile(Session& session, const QString& label)
{
    QListWidgetItem* item = tile(session, label);
    auto* toolbox = session.toolbox();
    toolbox->scrollToItem(item);
    const QPoint centre = toolbox->visualItemRect(item).center();
    click_at(toolbox->viewport(), centre);
    if (qEnvironmentVariableIsSet("VSS_UI_KEYBOARD_TILES"))
        key(toolbox, Qt::Key_Return);
    else
        double_click_at(toolbox->viewport(), centre);
    session.settle();
}

vss::endpoint_id add_named(Session& session, const QString& label,
                           const QString& name)
{
    const std::size_t before = session.backend().ports().size();
    expect_add_sheet(session, [name](topology::AddSheet* sheet)
                     { type_into(sheet_name(sheet), name); });
    add_tile(session, label);
    session.pilot().check();
    if (session.backend().ports().size() != before + 1)
        throw vss_test::failure("adding " + name.toStdString() +
                                " did not add one port");
    QStringList names;
    for (const auto& port : session.backend().ports())
        names << port.name;
    for (const auto& port : session.backend().ports())
    {
        if (port.name == name)
            return port.id;
    }
    throw vss_test::failure(
        "expected a port named " + name.toStdString() +
        "; the ports are: " + names.join(QStringLiteral(", ")).toStdString());
}

void click_node(Session& session, vss::endpoint_id id,
                Qt::KeyboardModifiers mods)
{
    auto* view = session.canvas();
    click_at(view->viewport(), node_point(view, id), mods);
}

void wire(Session& session, vss::endpoint_id from, vss::endpoint_id to,
          wire_kind kind)
{
    auto* view = session.canvas();
    const QRectF source = view->node_rect(from);
    const QPoint handle =
        view->mapFromScene(QPointF(source.right(), source.center().y()));
    drag(view->viewport(), handle, node_point(view, to));
    session.settle();
    QMenu* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
    if (menu == nullptr)
        throw vss_test::failure("dragging the wire opened no link chooser");
    const QStringList labels = view->wire_menu_labels(from, to);
    choose_entry(menu, labels.value(static_cast<int>(kind)));
    session.settle();
}

bool wire_refused(Session& session, vss::endpoint_id from, vss::endpoint_id to)
{
    auto* view = session.canvas();
    const QRectF source = view->node_rect(from);
    const QPoint handle =
        view->mapFromScene(QPointF(source.right(), source.center().y()));
    drag(view->viewport(), handle, node_point(view, to));
    session.settle();
    QMenu* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
    if (menu == nullptr)
        return true;
    // Every kind refused still opens the chooser, with its entries disabled.
    bool any_enabled = false;
    for (const QAction* entry : menu->actions())
        any_enabled =
            any_enabled || (!entry->isSeparator() && entry->isEnabled());
    close_menu(menu);
    session.settle();
    return !any_enabled;
}

void clear_canvas(Session& session)
{
    if (session.backend().ports().empty())
        return;
    session.canvas()->setFocus();
    key(session.canvas(), Qt::Key_A, Qt::ControlModifier);
    const bool lone = session.backend().ports().size() == 1 &&
                      session.backend().links().empty();
    delete_selection(session, !lone);
}

void drop_tile(Session& session, canvas_item item, const QPoint& at)
{
    auto* view = session.canvas();
    QMimeData data;
    data.setData(topology::toolbox_mime_type(),
                 QByteArray::number(static_cast<int>(item)));
    Probe::instance().beat();
    QDragEnterEvent enter(QPointF(at), Qt::CopyAction, &data, Qt::LeftButton,
                          Qt::NoModifier);
    QApplication::sendEvent(view->viewport(), &enter);
    if (!enter.isAccepted())
        throw vss_test::failure("the canvas refused the dragged tile");
    QDropEvent drop(QPointF(at), Qt::CopyAction, &data, Qt::LeftButton,
                    Qt::NoModifier);
    QApplication::sendEvent(view->viewport(), &drop);
    Probe::instance().beat();
    session.settle();
}

void select_ports(Session& session, const std::vector<vss::endpoint_id>& ports)
{
    bool first = true;
    for (const auto id : ports)
    {
        click_node(session, id,
                   first ? Qt::KeyboardModifiers{} : Qt::ControlModifier);
        first = false;
    }
}

void delete_selection(Session& session, bool expect_confirm)
{
    if (expect_confirm)
        session.pilot().expect(
            "confirm Delete",
            [](QWidget* d)
            {
                return button_starting(d, {QStringLiteral("Delete"),
                                           QStringLiteral("Remove")}) !=
                       nullptr;
            },
            [](QWidget* d)
            {
                click(button_starting(
                    d, {QStringLiteral("Delete"), QStringLiteral("Remove")}));
            });
    session.canvas()->setFocus();
    key(session.canvas(), Qt::Key_Delete);
    session.settle();
    session.pilot().check();
}

void require_consistent(Session& session)
{
    const auto ports = session.backend().ports();
    const auto links = session.backend().links();
    REQUIRE_EQ(session.canvas()->node_count(), static_cast<int>(ports.size()));
    REQUIRE_EQ(session.canvas()->link_count(), static_cast<int>(links.size()));
    REQUIRE_EQ(session.ports_rows(), static_cast<int>(ports.size()));
}

} // namespace vss::gui::ui::adv
