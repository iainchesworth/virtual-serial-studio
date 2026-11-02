//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "test_framework.hpp"

#include "backend/fake_studio_backend.hpp"
#include "dialogs/connect_ports.hpp"
#include "dialogs/network_endpoint.hpp"
#include "dialogs/share_wizard.hpp"
#include "dialogs/templates_dialog.hpp"
#include "shell/action_registry.hpp"
#include "shell/command_palette.hpp"
#include "shell/launcher.hpp"
#include "shell/level_service.hpp"
#include "shell/shell_window.hpp"

#include "theme/theme.hpp"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QSettings>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>

namespace
{

void clear_settings()
{
    QSettings settings;
    settings.clear();
}

} // namespace

TEST_CASE("actions: levels, search and shortcut uniqueness")
{
    vss::gui::ActionRegistry registry;
    bool simple_ran = false;
    bool advanced_ran = false;
    registry.add({QStringLiteral("simple.action"),
                  QStringLiteral("Simple"),
                  QStringLiteral("plain"),
                  {},
                  {},
                  vss::gui::action_level::simple,
                  false,
                  [&simple_ran] { simple_ran = true; },
                  {}});
    registry.add({QStringLiteral("advanced.action"),
                  QStringLiteral("Inject a fault"),
                  QStringLiteral("fault parity framing"),
                  {},
                  {},
                  vss::gui::action_level::advanced,
                  false,
                  [&advanced_ran] { advanced_ran = true; },
                  {}});

    REQUIRE(registry.action(QStringLiteral("simple.action")) != nullptr);
    REQUIRE(registry.action(QStringLiteral("advanced.action")) != nullptr);
    REQUIRE(registry.action(QStringLiteral("missing")) == nullptr);
    REQUIRE(registry.shortcuts_unique());

    registry.set_level(vss::gui::action_level::simple);
    REQUIRE(registry.action(QStringLiteral("simple.action"))->isVisible());
    REQUIRE(!registry.action(QStringLiteral("advanced.action"))->isVisible());
    const auto matches = registry.search(QStringLiteral("fault"));
    REQUIRE_EQ(matches.size(), 1);
    REQUIRE_EQ(matches.first()->data().toString(),
               QStringLiteral("advanced.action"));

    registry.set_level(vss::gui::action_level::advanced);
    REQUIRE(registry.action(QStringLiteral("advanced.action"))->isVisible());

    registry.action(QStringLiteral("simple.action"))->trigger();
    registry.action(QStringLiteral("advanced.action"))->trigger();
    REQUIRE(simple_ran);
    REQUIRE(advanced_ran);

    registry.add({QStringLiteral("duplicate.action"),
                  QStringLiteral("Duplicate"),
                  {},
                  {},
                  registry.action(QStringLiteral("simple.action"))->shortcut(),
                  vss::gui::action_level::simple,
                  false,
                  {},
                  {}});
    // Both shortcuts are empty here; an explicit duplicate proves the check.
    registry.action(QStringLiteral("duplicate.action"))
        ->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+9")));
    registry.action(QStringLiteral("simple.action"))
        ->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+9")));
    REQUIRE(!registry.shortcuts_unique());
    REQUIRE_EQ(registry.duplicate_shortcuts().size(), 1);
}

TEST_CASE("palette: groups hidden actions and turns Advanced on")
{
    clear_settings();
    vss::gui::ActionRegistry registry;
    vss::gui::LevelService level;
    bool advanced_ran = false;
    registry.add({QStringLiteral("add"),
                  QStringLiteral("Add port"),
                  QStringLiteral("new"),
                  {},
                  {},
                  vss::gui::action_level::simple,
                  false,
                  [] {},
                  {}});
    registry.add({QStringLiteral("fault"),
                  QStringLiteral("Inject a fault"),
                  QStringLiteral("parity framing"),
                  {},
                  {},
                  vss::gui::action_level::advanced,
                  false,
                  [&advanced_ran] { advanced_ran = true; },
                  {}});
    registry.set_level(level.level());

    vss::gui::CommandPalette palette(registry, level);
    palette.set_query(QStringLiteral("fault"));
    REQUIRE_EQ(palette.available_count(), 0);
    REQUIRE_EQ(palette.hidden_count(), 1);
    QAction* selected = palette.selected_action();
    REQUIRE(selected != nullptr);
    REQUIRE_EQ(selected->data().toString(), QStringLiteral("fault"));

    palette.activate_current();
    REQUIRE(level.advanced());
    REQUIRE(advanced_ran);
}

TEST_CASE("shell: launcher, workspace, level and layout")
{
    clear_settings();
    vss::gui::FakeStudioBackend backend;
    vss::gui::ShellWindow window(backend);
    window.resize(1280, 800);
    window.show();
    QApplication::processEvents();

    auto* launcher = window.findChild<vss::gui::Launcher*>();
    auto* ports_dock =
        window.findChild<QDockWidget*>(QStringLiteral("portsDock"));
    auto* inspector_dock =
        window.findChild<QDockWidget*>(QStringLiteral("inspectorDock"));
    auto* monitor_dock =
        window.findChild<QDockWidget*>(QStringLiteral("monitorDock"));
    REQUIRE(launcher != nullptr);
    REQUIRE(ports_dock != nullptr);
    REQUIRE(inspector_dock != nullptr);
    REQUIRE(monitor_dock != nullptr);
    REQUIRE(launcher->isVisible());
    REQUIRE(!ports_dock->isVisible());
    REQUIRE(!window.windowTitle().isEmpty());

    const auto port = backend.create_virtual_port(
        QStringLiteral("terminal"), vss::virtual_port_kind::loopback, true);
    REQUIRE(port.has_value());
    QApplication::processEvents();
    REQUIRE(!launcher->isVisible());
    REQUIRE(ports_dock->isVisible());
    REQUIRE(inspector_dock->isVisible());
    REQUIRE(monitor_dock->isVisible());

    QAction* advanced =
        window.findChild<QAction*>(QStringLiteral("view.advanced"));
    QAction* mock = window.findChild<QAction*>(QStringLiteral("ports.mock"));
    REQUIRE(advanced != nullptr);
    REQUIRE(mock != nullptr);
    REQUIRE(!mock->isVisible());
    advanced->trigger();
    QApplication::processEvents();
    REQUIRE(advanced->isChecked());
    REQUIRE(mock->isVisible());

    const QByteArray state = window.saveState();
    REQUIRE(!state.isEmpty());

    backend.remove_port(*port);
    QApplication::processEvents();
    REQUIRE(launcher->isVisible());
    REQUIRE(!ports_dock->isVisible());
    REQUIRE(window.run_selftest());

    window.close();
    QApplication::processEvents();
    QSettings settings;
    REQUIRE(
        !settings.value(QStringLiteral("ui/geometry")).toByteArray().isEmpty());
    REQUIRE(!settings.value(QStringLiteral("ui/windowState"))
                 .toByteArray()
                 .isEmpty());
}

TEST_CASE("levels: the Advanced choice survives a restart")
{
    clear_settings();
    vss::gui::LevelService first;
    REQUIRE(!first.advanced());
    first.set_advanced(true);
    vss::gui::LevelService second;
    REQUIRE(second.advanced());
    second.set_advanced(false);
    vss::gui::LevelService third;
    REQUIRE(!third.advanced());
    clear_settings();
}

TEST_CASE("palette: renders a review screenshot in both themes")
{
    clear_settings();
    vss::gui::ActionRegistry registry;
    vss::gui::LevelService level;
    registry.add({QStringLiteral("add"),
                  QStringLiteral("Add port"),
                  QStringLiteral("new virtual"),
                  QStringLiteral("add"),
                  QKeySequence(QStringLiteral("Ctrl+Shift+N")),
                  vss::gui::action_level::simple,
                  false,
                  [] {},
                  {}});
    registry.add({QStringLiteral("connect"),
                  QStringLiteral("Connect ports"),
                  QStringLiteral("link"),
                  QStringLiteral("connect"),
                  QKeySequence(QStringLiteral("Ctrl+L")),
                  vss::gui::action_level::simple,
                  false,
                  [] {},
                  {}});
    registry.add({QStringLiteral("fault"),
                  QStringLiteral("Inject a fault"),
                  QStringLiteral("parity framing overrun"),
                  QStringLiteral("fault"),
                  {},
                  vss::gui::action_level::advanced,
                  false,
                  [] {},
                  {}});
    registry.add({QStringLiteral("mock"),
                  QStringLiteral("Add mock device"),
                  QStringLiteral("fixture"),
                  QStringLiteral("fixture"),
                  {},
                  vss::gui::action_level::advanced,
                  false,
                  [] {},
                  {}});
    registry.set_level(level.level());

    const QString directory = QDir(QCoreApplication::applicationDirPath())
                                  .filePath(QStringLiteral("../gui-shots"));
    REQUIRE(QDir().mkpath(directory));

    vss::gui::CommandPalette palette(registry, level);
    palette.resize(760, 520);
    palette.show();
    QApplication::processEvents();
    palette.set_query(QStringLiteral("fault"));
    QApplication::processEvents();
    REQUIRE(palette.grab().save(
        QDir(directory).filePath(QStringLiteral("palette-light.png"))));

    vss::gui::Theme::instance().set_mode(vss::gui::theme_mode::dark);
    QApplication::processEvents();
    REQUIRE(palette.grab().save(
        QDir(directory).filePath(QStringLiteral("palette-dark.png"))));
    vss::gui::Theme::instance().set_mode(vss::gui::theme_mode::light);
    palette.close();
}

TEST_CASE("shell: every toolbar action has a name and a unique shortcut")
{
    clear_settings();
    vss::gui::FakeStudioBackend backend;
    vss::gui::ShellWindow window(backend);
    window.show();
    QApplication::processEvents();

    const auto buttons = window.findChildren<QToolButton*>();
    REQUIRE(!buttons.isEmpty());
    for (const QToolButton* button : buttons)
    {
        if (!button->isVisibleTo(&window))
            continue;
        REQUIRE(!button->accessibleName().isEmpty() ||
                !button->text().isEmpty());
        REQUIRE(button->height() >= 24 || button->sizeHint().height() >= 24);
    }

    QStringList shortcuts;
    for (QAction* action : window.findChildren<QAction*>())
    {
        const QString key =
            action->shortcut().toString(QKeySequence::NativeText);
        if (key.isEmpty())
            continue;
        REQUIRE(!shortcuts.contains(key));
        shortcuts << key;
    }
    REQUIRE(shortcuts.size() > 8);
}

namespace
{

template <class Dialog>
bool trigger_and_close(vss::gui::ShellWindow& window, const QString& action_id)
{
    auto* action = window.findChild<QAction*>(action_id);
    REQUIRE(action != nullptr);
    REQUIRE(action->isEnabled());
    bool found = false;
    QTimer::singleShot(0, &window,
                       [&found]
                       {
                           for (QWidget* widget :
                                QApplication::topLevelWidgets())
                           {
                               if (auto* dialog = qobject_cast<Dialog*>(widget))
                               {
                                   found = true;
                                   dialog->reject();
                               }
                           }
                       });
    action->trigger();
    return found;
}

} // namespace

TEST_CASE("shell: WP6 actions open the real dialogs")
{
    clear_settings();
    vss::gui::FakeStudioBackend backend;
    vss::gui::setup_facts facts;
    facts.platform = QStringLiteral("windows");
    backend.set_facts(facts);
    vss::gui::device_view device;
    device.path = QStringLiteral("COM3");
    device.description = QStringLiteral("Serial device");
    backend.set_devices({device});
    for (const char* name : {"alpha", "beta", "gamma"})
    {
        const auto created = backend.create_virtual_port(
            QString::fromLatin1(name), vss::virtual_port_kind::pty, true);
        REQUIRE(created.has_value());
    }
    backend.refresh();

    vss::gui::ShellWindow window(backend);
    window.resize(1280, 800);
    window.show();
    QApplication::processEvents();

    REQUIRE(trigger_and_close<vss::gui::ConnectPortsDialog>(
        window, QStringLiteral("topology.connect")));
    REQUIRE(trigger_and_close<vss::gui::ShareWizard>(
        window, QStringLiteral("topology.share")));
    REQUIRE(trigger_and_close<vss::gui::TemplatesDialog>(
        window, QStringLiteral("file.templates")));

    auto* advanced =
        window.findChild<QAction*>(QStringLiteral("view.advanced"));
    auto* share_toolbar =
        window.findChild<QAction*>(QStringLiteral("topology.shareToolbar"));
    auto* network = window.findChild<QAction*>(QStringLiteral("ports.network"));
    REQUIRE(advanced != nullptr);
    REQUIRE(share_toolbar != nullptr);
    REQUIRE(network != nullptr);
    REQUIRE(!share_toolbar->isVisible());
    advanced->trigger();
    QApplication::processEvents();
    REQUIRE(share_toolbar->isVisible());
    REQUIRE(trigger_and_close<vss::gui::NetworkEndpointDialog>(
        window, QStringLiteral("ports.network")));

    // The toolbar-only duplicate must not double an entry in the palette.
    vss::gui::ActionRegistry registry;
    registry.add({QStringLiteral("menu.share"),
                  QStringLiteral("Share a device"),
                  QStringLiteral("share"),
                  {},
                  {},
                  vss::gui::action_level::simple,
                  false,
                  [] {},
                  {},
                  true});
    registry.add({QStringLiteral("toolbar.share"),
                  QStringLiteral("Share a device"),
                  QStringLiteral("share"),
                  {},
                  {},
                  vss::gui::action_level::advanced,
                  false,
                  [] {},
                  {},
                  false});
    REQUIRE_EQ(registry.search(QStringLiteral("share")).size(), 1);
    REQUIRE_EQ(
        registry.search(QStringLiteral("share")).front()->data().toString(),
        QStringLiteral("menu.share"));
    REQUIRE(registry.action(QStringLiteral("toolbar.share")) != nullptr);

    window.close();
    QApplication::processEvents();
}
