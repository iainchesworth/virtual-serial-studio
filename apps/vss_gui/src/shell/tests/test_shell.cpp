//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "test_framework.hpp"

#include "backend/fake_studio_backend.hpp"
#include "shell/action_registry.hpp"
#include "shell/command_palette.hpp"
#include "shell/launcher.hpp"
#include "shell/level_service.hpp"
#include "shell/shell_window.hpp"
#include "support/setup_facts.hpp"

#include "theme/theme.hpp"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QLabel>
#include <QSettings>
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

TEST_CASE("shell: undo and redo are capability gated")
{
    clear_settings();
    {
        vss::gui::FakeStudioBackend backend;
        vss::gui::ShellWindow window(backend);
        QAction* undo = window.findChild<QAction*>(QStringLiteral("edit.undo"));
        QAction* redo = window.findChild<QAction*>(QStringLiteral("edit.redo"));
        REQUIRE(undo != nullptr);
        REQUIRE(redo != nullptr);
        REQUIRE(!undo->isEnabled());
        REQUIRE(!redo->isEnabled());
        REQUIRE(!undo->toolTip().isEmpty());
    }

    vss::gui::capabilities caps;
    caps.undo = true;
    vss::gui::FakeStudioBackend backend;
    backend.set_capabilities(caps);
    vss::gui::ShellWindow window(backend);
    QAction* undo = window.findChild<QAction*>(QStringLiteral("edit.undo"));
    REQUIRE(undo != nullptr);
    REQUIRE(undo->isEnabled());
}

TEST_CASE("shell: the shortcuts dialog is generated from the registry")
{
    vss::gui::ActionRegistry registry;
    registry.add({QStringLiteral("file.new"),
                  QStringLiteral("New"),
                  QStringLiteral("document"),
                  {},
                  QKeySequence::New,
                  vss::gui::action_level::simple,
                  false,
                  [] {},
                  {}});
    registry.add({QStringLiteral("file.save"),
                  QStringLiteral("Save"),
                  QStringLiteral("file"),
                  {},
                  QKeySequence::Save,
                  vss::gui::action_level::simple,
                  false,
                  [] {},
                  {}});
    registry.add({QStringLiteral("edit.find"),
                  QStringLiteral("Find"),
                  QStringLiteral("search"),
                  {},
                  {},
                  vss::gui::action_level::simple,
                  false,
                  [] {},
                  {}});

    const auto entries = vss::gui::shortcut_entries(registry);
    REQUIRE_EQ(entries.size(), 2);
    REQUIRE_EQ(entries.at(0).action, QStringLiteral("New"));
    REQUIRE_EQ(entries.at(0).shortcut, QKeySequence::New);
    REQUIRE_EQ(entries.at(1).action, QStringLiteral("Save"));

    vss::gui::support::ShortcutsDialog dialog(entries);
    REQUIRE_EQ(dialog.total_row_count(), entries.size());
    QStringList expected;
    for (const auto& entry : entries)
        expected << entry.action;
    REQUIRE_EQ(dialog.action_texts(), expected);
}

TEST_CASE("shell: the unsaved prompt follows ui/askOnDiscard")
{
    clear_settings();
    QSettings().setValue(QStringLiteral("ui/askOnDiscard"), false);
    vss::gui::FakeStudioBackend backend;
    vss::gui::ShellWindow window(backend);
    window.show();
    QApplication::processEvents();

    const auto port = backend.create_virtual_port(
        QStringLiteral("terminal"), vss::virtual_port_kind::loopback, true);
    REQUIRE(port.has_value());
    QApplication::processEvents();
    REQUIRE(window.windowTitle().contains(QLatin1Char('*')));

    // With the setting off, close must not open the dialog.
    window.close();
    QApplication::processEvents();
    REQUIRE(!window.isVisible());
    clear_settings();
}

TEST_CASE("shell: the status bar readiness comes from the shared facts")
{
    clear_settings();
    vss::gui::setup_facts facts;
    facts.platform = QStringLiteral("windows");
    facts.com0com_found = true;
    facts.serial_devices = 0;

    {
        vss::gui::FakeStudioBackend backend;
        backend.set_facts(facts);
        vss::gui::ShellWindow window(backend);
        window.show();
        QApplication::processEvents();
        bool ready = false;
        for (QLabel* label : window.findChildren<QLabel*>())
        {
            if (label->text() == QStringLiteral("Setup ready"))
                ready = true;
        }
        REQUIRE(ready);
    }

    facts.com0com_found = false;
    {
        vss::gui::FakeStudioBackend backend;
        backend.set_facts(facts);
        vss::gui::ShellWindow window(backend);
        window.show();
        QApplication::processEvents();
        bool attention = false;
        for (QLabel* label : window.findChildren<QLabel*>())
        {
            if (label->text() == QStringLiteral("Setup needs attention"))
                attention = true;
        }
        REQUIRE(attention);
    }
    clear_settings();
}
