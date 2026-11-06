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
#include "panes/ports_panel.hpp"
#include "shell/action_registry.hpp"
#include "shell/command_palette.hpp"
#include "shell/launcher.hpp"
#include "shell/level_service.hpp"
#include "shell/setup_group.hpp"
#include "shell/shell_window.hpp"
#include "shell/undo_service.hpp"
#include "shell/undo_steps.hpp"
#include "support/setup_facts.hpp"

#include "theme/theme.hpp"
#include "widgets/dialogs.hpp"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
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
    QAction* redo = window.findChild<QAction*>(QStringLiteral("edit.redo"));
    REQUIRE(undo != nullptr);
    // Supported, but there is no change to reverse yet.
    REQUIRE(!undo->isEnabled());
    REQUIRE(!redo->isEnabled());
    REQUIRE_EQ(undo->text(), QStringLiteral("Undo"));
    REQUIRE(undo->toolTip() != QStringLiteral("Not available in this build"));
}

TEST_CASE("undo: the history reverses and repeats steps in order")
{
    vss::gui::UndoService history;
    int changes = 0;
    int value = 0;
    QObject::connect(&history, &vss::gui::UndoService::changed,
                     [&changes] { ++changes; });
    REQUIRE(!history.can_undo());
    REQUIRE(!history.undo());
    REQUIRE(!history.redo());

    const auto step = [&value](const QString& text, int amount)
    {
        return vss::gui::undo_step{text,
                                   [&value, amount]
                                   {
                                       value -= amount;
                                       return true;
                                   },
                                   [&value, amount]
                                   {
                                       value += amount;
                                       return true;
                                   }};
    };
    value += 1;
    history.push(step(QStringLiteral("one"), 1));
    value += 10;
    history.push(step(QStringLiteral("ten"), 10));
    REQUIRE_EQ(value, 11);
    REQUIRE_EQ(history.undo_text(), QStringLiteral("ten"));

    REQUIRE(history.undo());
    REQUIRE_EQ(value, 1);
    REQUIRE_EQ(history.redo_text(), QStringLiteral("ten"));
    REQUIRE_EQ(history.undo_text(), QStringLiteral("one"));
    REQUIRE(history.redo());
    REQUIRE_EQ(value, 11);

    // A new change drops what could have been redone.
    REQUIRE(history.undo());
    value += 100;
    history.push(step(QStringLiteral("hundred"), 100));
    REQUIRE(!history.can_redo());
    REQUIRE(history.redo_text().isEmpty());
    REQUIRE(changes > 0);
}

TEST_CASE("undo: a step that cannot be applied clears the history")
{
    vss::gui::UndoService history;
    history.push({QStringLiteral("first"), [] { return true; },
                  [] { return true; }});
    history.push({QStringLiteral("second"), [] { return false; },
                  [] { return true; }});
    REQUIRE(!history.undo());
    // The step after the failure assumed it, so nothing is left.
    REQUIRE(!history.can_undo());
    REQUIRE(!history.can_redo());

    for (std::size_t count = 0; count < vss::gui::UndoService::max_depth + 5;
         ++count)
        history.push({QStringLiteral("step"), [] { return true; },
                      [] { return true; }});
    std::size_t undone = 0;
    while (history.undo())
        ++undone;
    REQUIRE_EQ(undone, vss::gui::UndoService::max_depth);
}

TEST_CASE("undo: link steps use exact inverse operations")
{
    vss::gui::FakeStudioBackend backend;
    const auto first = backend.create_auto_port(QStringLiteral("a"), true);
    const auto second = backend.create_auto_port(QStringLiteral("b"), true);
    const auto third = backend.create_auto_port(QStringLiteral("c"), true);
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    REQUIRE(third.has_value());

    vss::gui::UndoService history;
    const auto linked = backend.connect_ports(*first, *second, {});
    REQUIRE(linked.has_value());
    const auto made = backend.link(*linked);
    REQUIRE(made.has_value());
    history.push(
        vss::gui::connect_step(backend, *made, QStringLiteral("Connect")));

    REQUIRE(history.undo());
    REQUIRE(backend.links().empty());
    REQUIRE(history.redo());
    REQUIRE_EQ(backend.links().size(), std::size_t{1});
    REQUIRE(vss::gui::find_link(backend, *made).has_value());

    // Removing a link and undoing it makes the same link again, with its
    // options.
    vss::link_options options;
    options.policy = vss::merge_policy::owner;
    options.emulate_modem_lines = false;
    const auto joined = backend.join({*first, *second}, *third, options);
    REQUIRE(joined.has_value());
    const auto join_view = backend.link(*joined);
    REQUIRE(join_view.has_value());
    REQUIRE(backend.set_owner(*joined, *second).has_value());
    const auto owned = backend.link(*joined);
    REQUIRE(owned.has_value());

    history.clear();
    REQUIRE(backend.disconnect(*joined).has_value());
    history.push(vss::gui::remove_link_step(backend, *owned,
                                            QStringLiteral("Remove link")));
    REQUIRE_EQ(backend.links().size(), std::size_t{1});
    REQUIRE(history.undo());
    REQUIRE_EQ(backend.links().size(), std::size_t{2});
    const auto again = vss::gui::find_link(backend, *owned);
    REQUIRE(again.has_value());
    REQUIRE_EQ(again->owner, *second);
    REQUIRE(!again->options.emulate_modem_lines);
    REQUIRE(again->options.policy == vss::merge_policy::owner);

    // An owner change is reversed and repeated on whatever link has these
    // ports now.
    history.clear();
    history.push(vss::gui::owner_step(backend, *again, *second, *first,
                                      QStringLiteral("Change owner")));
    REQUIRE(backend.set_owner(again->id, *first).has_value());
    REQUIRE(history.undo());
    REQUIRE_EQ(vss::gui::find_link(backend, *again)->owner, *second);
    REQUIRE(history.redo());
    REQUIRE_EQ(vss::gui::find_link(backend, *again)->owner, *first);

    // A rename is reversed by naming the port as it was.
    history.clear();
    REQUIRE(backend.rename_port(*first, QStringLiteral("renamed")).has_value());
    history.push(vss::gui::rename_step(backend, *first, QStringLiteral("a"),
                                       QStringLiteral("renamed"),
                                       QStringLiteral("Rename port")));
    REQUIRE(history.undo());
    REQUIRE_EQ(backend.port(*first)->name, QStringLiteral("a"));
    REQUIRE(history.redo());
    REQUIRE_EQ(backend.port(*first)->name, QStringLiteral("renamed"));
    REQUIRE(history.undo());
    history.clear();
    history.push(vss::gui::owner_step(backend, *again, *second, *first,
                                      QStringLiteral("Change owner")));
    REQUIRE(backend.set_owner(again->id, *first).has_value());

    // When the link has gone, the step says so instead of guessing.
    REQUIRE(backend.disconnect(vss::gui::find_link(backend, *again)->id)
                .has_value());
    REQUIRE(!history.undo());
    REQUIRE(!history.can_undo());
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

    // The Studio driver on its own counts as ready.
    facts.driver_found = true;
    facts.driver_version = QStringLiteral("2");
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
        // The launcher names the driver it found; it reads the host platform.
        if (vss::gui::support::host_platform() == QLatin1String("windows"))
        {
            bool names_driver = false;
            for (QLabel* label : window.findChildren<QLabel*>())
            {
                if (label->text().contains(QStringLiteral(
                        "Virtual Serial Studio driver detected")))
                    names_driver = true;
            }
            REQUIRE(names_driver);
        }
    }
    clear_settings();
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

namespace
{

/// Presses `button` in the next confirmation that opens and records what it
/// said.  It looks again until the dialog is up, so a slow machine does not
/// leave the test waiting in the dialog's event loop.
struct confirm_probe
{
    QString title;
    QString body;
    QStringList buttons;
    bool seen = false;
};

void answer_next_confirm(confirm_probe* probe, const QString& button,
                         int remaining_ms = 30000)
{
    QTimer::singleShot(
        10,
        [probe, button, remaining_ms]
        {
            auto* dialog = qobject_cast<vss::gui::ConfirmDialog*>(
                QApplication::activeModalWidget());
            if (dialog == nullptr)
            {
                if (remaining_ms > 10)
                    answer_next_confirm(probe, button, remaining_ms - 10);
                return;
            }
            probe->seen = true;
            probe->title = dialog->windowTitle();
            for (auto* label : dialog->findChildren<QLabel*>())
                probe->body += label->text() + QLatin1Char(' ');
            QPushButton* chosen = nullptr;
            for (auto* push : dialog->findChildren<QPushButton*>())
            {
                probe->buttons << push->text();
                if (push->text() == button)
                    chosen = push;
            }
            if (chosen != nullptr)
                chosen->click();
            else
                dialog->reject();
        });
}

struct setup_fixture
{
    vss::gui::FakeStudioBackend backend;
    vss::endpoint_id first = 0;
    vss::endpoint_id second = 0;
    vss::endpoint_id lone = 0;

    setup_fixture()
    {
        first = *backend.create_auto_port(QStringLiteral("alpha"), true);
        second = *backend.create_auto_port(QStringLiteral("beta"), true);
        lone = *backend.create_auto_port(QStringLiteral("gamma"), true);
        REQUIRE(backend.connect_ports(first, second, {}).has_value());
        backend.refresh();
    }
};

} // namespace

TEST_CASE("setup group: a pair, a share and a lone port")
{
    setup_fixture fixture;
    const auto ports = fixture.backend.ports();
    const auto links = fixture.backend.links();

    const auto pair = vss::gui::find_setup(ports, links, fixture.second);
    REQUIRE_EQ(pair.ports.size(), 2);
    REQUIRE(pair.ports.contains(fixture.first));
    REQUIRE(pair.ports.contains(fixture.second));
    REQUIRE_EQ(pair.links.size(), 1);
    REQUIRE(pair.has_company());
    const QString names = vss::gui::setup_port_names(pair, ports);
    REQUIRE(names.startsWith(QStringLiteral("alpha")));
    REQUIRE(names.contains(QStringLiteral(" and beta")));

    const auto lone = vss::gui::find_setup(ports, links, fixture.lone);
    REQUIRE_EQ(lone.ports.size(), 1);
    REQUIRE(lone.links.isEmpty());
    REQUIRE(!lone.has_company());

    REQUIRE(vss::gui::find_setup(ports, links, 9999).empty());
    const auto by_link =
        vss::gui::find_setup_of_link(ports, links, links.front().id);
    REQUIRE_EQ(by_link.ports.size(), 2);
    REQUIRE(vss::gui::find_setup_of_link(ports, links, 9999).empty());

    // A share is the device and every program port, joined by one link.
    const auto shared = fixture.backend.share_serial(
        QStringLiteral("serial://COM3?baud=9600"), 3);
    REQUIRE(shared.has_value());
    const auto group =
        vss::gui::find_setup(fixture.backend.ports(), fixture.backend.links(),
                             shared->listeners.at(1));
    REQUIRE_EQ(group.ports.size(), 4);
    REQUIRE(group.ports.contains(shared->source));
    REQUIRE_EQ(group.links.size(), 1);
    // The pair and the lone port are not part of it.
    REQUIRE(!group.ports.contains(fixture.first));
    REQUIRE(!group.ports.contains(fixture.lone));
}

TEST_CASE("shell: Remove setup names what closes and removes the whole pair")
{
    clear_settings();
    setup_fixture fixture;
    vss::gui::ShellWindow window(fixture.backend);
    window.resize(1280, 800);
    window.show();
    QApplication::processEvents();

    auto* action =
        window.findChild<QAction*>(QStringLiteral("topology.removeSetup"));
    REQUIRE(action != nullptr);
    REQUIRE(action->isEnabled());

    // Cancel keeps everything and still names the ports.
    confirm_probe cancelled;
    answer_next_confirm(&cancelled, QStringLiteral("Cancel"));
    action->trigger();
    QApplication::processEvents();
    REQUIRE(cancelled.seen);
    REQUIRE_EQ(cancelled.title, QStringLiteral("Remove this setup?"));
    REQUIRE(cancelled.body.contains(QStringLiteral("2 ports and 1 link")));
    REQUIRE(cancelled.body.contains(QStringLiteral("alpha")));
    REQUIRE(cancelled.body.contains(QStringLiteral(" and beta")));
    REQUIRE(!cancelled.body.contains(QStringLiteral("gamma")));
    REQUIRE_EQ(fixture.backend.ports().size(), std::size_t{3});

    confirm_probe confirmed;
    answer_next_confirm(&confirmed, QStringLiteral("Remove setup"));
    action->trigger();
    QApplication::processEvents();
    REQUIRE(confirmed.seen);
    REQUIRE_EQ(fixture.backend.ports().size(), std::size_t{1});
    REQUIRE_EQ(fixture.backend.ports().front().name, QStringLiteral("gamma"));
    REQUIRE(fixture.backend.links().empty());
}

TEST_CASE("shell: Delete on a port in a setup offers the whole setup")
{
    clear_settings();
    setup_fixture fixture;
    vss::gui::ShellWindow window(fixture.backend);
    window.resize(1280, 800);
    window.show();
    QApplication::processEvents();

    auto* remove = window.findChild<QAction*>(QStringLiteral("edit.remove"));
    REQUIRE(remove != nullptr);
    REQUIRE_EQ(remove->shortcut(), QKeySequence(Qt::Key_Delete));

    confirm_probe offered;
    answer_next_confirm(&offered, QStringLiteral("Remove only alpha"));
    remove->trigger();
    QApplication::processEvents();
    REQUIRE(offered.seen);
    REQUIRE_EQ(offered.title, QStringLiteral("Remove alpha?"));
    REQUIRE(offered.buttons.contains(QStringLiteral("Remove the whole setup")));
    REQUIRE(offered.buttons.contains(QStringLiteral("Cancel")));
    REQUIRE(offered.body.contains(QStringLiteral("beta")));
    REQUIRE_EQ(fixture.backend.ports().size(), std::size_t{2});
    REQUIRE(!fixture.backend.port(fixture.first).has_value());
}

TEST_CASE("shell: Delete then the whole setup closes a share in one go")
{
    clear_settings();
    vss::gui::FakeStudioBackend backend;
    const auto shared =
        backend.share_serial(QStringLiteral("serial://COM3?baud=9600"), 3);
    REQUIRE(shared.has_value());
    const auto lone = backend.create_auto_port(QStringLiteral("gamma"), true);
    REQUIRE(lone.has_value());
    backend.refresh();

    vss::gui::ShellWindow window(backend);
    window.resize(1280, 800);
    window.show();
    QApplication::processEvents();

    // Delete on the device asks about the whole setup.
    auto* ports_panel = window.findChild<vss::gui::PortsPanel*>();
    REQUIRE(ports_panel != nullptr);
    ports_panel->select_port(shared->source);
    QApplication::processEvents();
    auto* remove = window.findChild<QAction*>(QStringLiteral("edit.remove"));
    REQUIRE(remove != nullptr);
    confirm_probe probe;
    answer_next_confirm(&probe, QStringLiteral("Remove the whole setup"));
    remove->trigger();
    QApplication::processEvents();
    REQUIRE(probe.seen);
    REQUIRE_EQ(backend.ports().size(), std::size_t{1});
    REQUIRE_EQ(backend.ports().front().name, QStringLiteral("gamma"));
}

TEST_CASE("shell: removing one program of a share keeps the others sharing")
{
    clear_settings();
    vss::gui::FakeStudioBackend backend;
    const auto shared =
        backend.share_serial(QStringLiteral("serial://COM3?baud=9600"), 3);
    REQUIRE(shared.has_value());
    backend.refresh();

    vss::gui::ShellWindow window(backend);
    window.resize(1280, 800);
    window.show();
    QApplication::processEvents();

    // Select the middle program port through the Ports dock.
    auto* ports_panel = window.findChild<vss::gui::PortsPanel*>();
    REQUIRE(ports_panel != nullptr);
    ports_panel->select_port(shared->listeners.at(1));
    QApplication::processEvents();

    auto* remove = window.findChild<QAction*>(QStringLiteral("edit.remove"));
    REQUIRE(remove != nullptr);
    const QString name = backend.port(shared->listeners.at(1))->name;
    confirm_probe probe;
    answer_next_confirm(&probe, QStringLiteral("Remove only %1").arg(name));
    remove->trigger();
    QApplication::processEvents();
    REQUIRE(probe.seen);
    REQUIRE(probe.body.contains(QStringLiteral("one of 3 programs")));
    REQUIRE(!backend.port(shared->listeners.at(1)).has_value());
    REQUIRE_EQ(backend.ports().size(), std::size_t{3});
    REQUIRE_EQ(backend.links().size(), std::size_t{1});
    REQUIRE_EQ(backend.links().front().targets.size(), 2);
}

TEST_CASE("shell: Add a program port extends the share of the selection")
{
    clear_settings();
    vss::gui::FakeStudioBackend backend;
    const auto shared =
        backend.share_serial(QStringLiteral("serial://COM3?baud=9600"), 2);
    REQUIRE(shared.has_value());
    const auto lone = backend.create_auto_port(QStringLiteral("gamma"), true);
    REQUIRE(lone.has_value());
    backend.refresh();

    vss::gui::ShellWindow window(backend);
    window.resize(1280, 800);
    window.show();
    QApplication::processEvents();

    auto* add =
        window.findChild<QAction*>(QStringLiteral("topology.addProgram"));
    REQUIRE(add != nullptr);
    auto* ports_panel = window.findChild<vss::gui::PortsPanel*>();
    REQUIRE(ports_panel != nullptr);

    // A port that is not in a share cannot take another program.
    ports_panel->select_port(*lone);
    QApplication::processEvents();
    REQUIRE(!add->isEnabled());

    ports_panel->select_port(shared->listeners.at(0));
    QApplication::processEvents();
    REQUIRE(add->isEnabled());
    add->trigger();
    QApplication::processEvents();
    REQUIRE_EQ(backend.ports().size(), std::size_t{5});
    REQUIRE_EQ(backend.links().size(), std::size_t{1});
    REQUIRE_EQ(backend.links().front().targets.size(), 3);
}

TEST_CASE("shell: Remove setup is on the toolbar and the Edit menu")
{
    clear_settings();
    setup_fixture fixture;
    vss::gui::ShellWindow window(fixture.backend);
    window.resize(1280, 800);
    window.show();
    QApplication::processEvents();

    auto* action =
        window.findChild<QAction*>(QStringLiteral("topology.removeSetup"));
    REQUIRE(action != nullptr);
    bool on_toolbar = false;
    for (auto* toolbar : window.findChildren<QToolBar*>())
        on_toolbar = on_toolbar || toolbar->actions().contains(action);
    REQUIRE(on_toolbar);
    bool in_edit_menu = false;
    for (auto* menu : window.findChildren<QMenu*>())
    {
        if (menu->title().remove(QLatin1Char('&')) == QStringLiteral("Edit"))
            in_edit_menu = menu->actions().contains(action);
    }
    REQUIRE(in_edit_menu);
}
