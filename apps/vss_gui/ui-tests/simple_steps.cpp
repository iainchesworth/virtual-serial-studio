//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "simple_steps.hpp"

#include "topology/topology_view.hpp"

#include <QAbstractItemModel>
#include <QMenuBar>

#include <algorithm>

namespace vss::gui::ui::steps
{

simple::JobForm* open_job(Session& session, simple::job kind)
{
    auto* page = session.simple_page();
    if (page == nullptr)
        throw vss_test::failure("the window has no Simple page");
    simple::JobTile* tile = page->jobs()->tile(kind);
    if (tile == nullptr)
        throw vss_test::failure("the jobs page has no tile for " +
                                simple::job_title(kind).toStdString());
    if (!tile->isVisible())
    {
        // A setup is open: "New" returns to the jobs, as a person does.
        click(button(page, QStringLiteral("Set up something new")));
    }
    click(tile);
    wait_for("the job form", [page] { return page->form() != nullptr; });
    return page->form();
}

std::vector<QLineEdit*> program_fields(simple::JobForm* form)
{
    std::vector<QLineEdit*> fields;
    for (auto* field :
         form->findChildren<QLineEdit*>(QStringLiteral("jobFormLabel")))
    {
        if (field->isVisibleTo(form))
            fields.push_back(field);
    }
    std::sort(fields.begin(), fields.end(),
              [form](QLineEdit* left, QLineEdit* right)
              {
                  return left->mapTo(form, QPoint(0, 0)).y() <
                         right->mapTo(form, QPoint(0, 0)).y();
              });
    return fields;
}

QPushButton* form_button(simple::JobForm* form, const QString& text)
{
    return button(form, text);
}

simple::SetupPage* create(Session& session)
{
    auto* page = session.simple_page();
    simple::JobForm* form = page->form();
    if (form == nullptr)
        throw vss_test::failure("no job form is open to create from");
    if (!form->create_button()->isEnabled())
        throw vss_test::failure("Create is disabled; the form says: " +
                                form->blocker().toStdString());
    click(form->create_button());
    wait_for("the setup page",
             [page] { return page->setup_page() != nullptr; });
    session.settle();
    return page->setup_page();
}

simple::SetupPage* setup_page(Session& session)
{
    auto* shown = session.simple_page()->setup_page();
    if (shown == nullptr || !shown->isVisible())
        throw vss_test::failure("no setup page is showing");
    return shown;
}

void select_setup_row(Session& session, int row)
{
    click_row(session.simple_page()->list(), row);
    session.settle();
}

void remove_shown_setup(Session& session)
{
    auto* shown = setup_page(session);
    session.pilot().expect_button("confirm removing the setup",
                                  QStringLiteral("Remove setup"));
    click(button(shown, QStringLiteral("Remove setup")));
    session.settle();
    session.pilot().check();
    if (!session.backend().ports().empty() ||
        !session.backend().links().empty())
        throw vss_test::failure(
            "the setup is still there after Remove setup: " +
            std::to_string(session.backend().ports().size()) + " ports");
    if (!session.simple_page()->jobs()->isVisible())
        throw vss_test::failure("the jobs page did not come back");
}

void require_counts(Session& session, const counts& expected)
{
    REQUIRE_EQ(session.backend().ports().size(), expected.ports);
    REQUIRE_EQ(session.backend().links().size(), expected.links);
}

void verify_in_advanced(Session& session)
{
    session.step(QStringLiteral("look at the same setup in Advanced mode"));
    session.set_mode(Session::mode::advanced);
    session.settle();
    REQUIRE_EQ(session.ports_rows(),
               static_cast<int>(session.backend().ports().size()));
    auto* canvas = session.canvas();
    REQUIRE(canvas != nullptr);
    REQUIRE_EQ(canvas->node_count(),
               static_cast<int>(session.backend().ports().size()));
    REQUIRE_EQ(canvas->link_count(),
               static_cast<int>(session.backend().links().size()));
    session.set_mode(Session::mode::simple);
    session.settle();
}

void choose_device(simple::JobForm* form, const QString& path, int which)
{
    std::vector<QComboBox*> combos;
    for (auto* combo :
         form->findChildren<QComboBox*>(QStringLiteral("jobFormDevice")))
    {
        if (combo->isVisibleTo(form))
            combos.push_back(combo);
    }
    std::sort(combos.begin(), combos.end(),
              [form](QComboBox* left, QComboBox* right)
              {
                  return left->mapTo(form, QPoint(0, 0)).y() <
                         right->mapTo(form, QPoint(0, 0)).y();
              });
    if (which >= static_cast<int>(combos.size()))
        throw vss_test::failure(
            "the form shows " + std::to_string(combos.size()) +
            " device lists, not " + std::to_string(which + 1));
    QComboBox* combo = combos[static_cast<std::size_t>(which)];
    const int row = combo->findData(path);
    if (row < 0)
        throw vss_test::failure("the device list does not offer " +
                                path.toStdString());
    choose(combo, combo->itemText(row));
}

void plug_in_devices(Session& session, const QStringList& paths)
{
    std::vector<device_view> devices;
    for (const QString& path : paths)
    {
        device_view device;
        device.path = path;
        device.description = QStringLiteral("USB Serial Device");
        devices.push_back(device);
    }
    session.fake().set_devices(std::move(devices));
}

} // namespace vss::gui::ui::steps
