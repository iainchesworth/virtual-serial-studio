//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Journey tests: each one is a task from docs/gui-design/docs/screens/01, done
// the way a person does it, with key presses and mouse clicks sent to the real
// main window and its dialogs through QTest. The backend is the in-memory
// fake, so the results are the same on every platform.
//
// The dialogs run their own event loop (exec()), which blocks the test body.
// A step is therefore armed first and runs from a timer once the dialog it is
// waiting for is on screen.
//

#include "test_framework.hpp"

#include "backend/fake_studio_backend.hpp"
#include "flows/add_port_dialog.hpp"
#include "flows/port_ready_dialog.hpp"
#include "flows/watch_dialog.hpp"
#include "shell/command_palette.hpp"
#include "shell/launcher.hpp"
#include "shell/shell_window.hpp"

#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QString>
#include <QTest>
#include <QTimer>

#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace
{

using vss::gui::FakeStudioBackend;
using vss::gui::ShellWindow;

void clear_settings()
{
    QSettings settings;
    settings.clear();
}

/// A step waiting for a dialog. It runs once `ready` accepts the active modal
/// widget. If that never happens the step closes whatever is open, so a missing
/// dialog fails the test instead of hanging it.
class Waiting final
{
public:
    using Ready = std::function<bool(QWidget*)>;
    using Step = std::function<void(QWidget*)>;

    static constexpr int poll_milliseconds = 10;
    static constexpr int give_up_milliseconds = 8000;

    Waiting(Ready ready, Step step)
        : ready_(std::move(ready)), step_(std::move(step)),
          timer_(new QTimer(QApplication::instance()))
    {
        timer_->setInterval(poll_milliseconds);
        QObject::connect(timer_, &QTimer::timeout, timer_, [this] { poll(); });
        timer_->start();
    }

    Waiting(const Waiting&) = delete;
    Waiting& operator=(const Waiting&) = delete;

    ~Waiting() { delete timer_; }

    /// True once the step has run. A failed requirement inside the step is
    /// thrown here, on the test's own stack, because an exception must not
    /// leave a timer slot.
    bool ran() const
    {
        if (!error_.empty())
            throw vss_test::failure(error_);
        return ran_;
    }

private:
    void poll()
    {
        elapsed_ += poll_milliseconds;
        QWidget* modal = QApplication::activeModalWidget();
        if (modal != nullptr && ready_(modal))
        {
            timer_->stop();
            ran_ = true;
            try
            {
                step_(modal);
            }
            catch (const std::exception& error)
            {
                error_ = error.what();
                modal->close();
            }
            return;
        }
        if (elapsed_ >= give_up_milliseconds)
        {
            timer_->stop();
            if (modal != nullptr)
                modal->close();
        }
    }

    Ready ready_;
    Step step_;
    QTimer* timer_;
    int elapsed_ = 0;
    bool ran_ = false;
    std::string error_;
};

template <class Dialog> bool is_a(QWidget* widget)
{
    return qobject_cast<Dialog*>(widget) != nullptr;
}

QPushButton* button(QWidget* parent, const QString& text)
{
    for (auto* candidate : parent->findChildren<QPushButton*>())
    {
        if (candidate->text() == text)
            return candidate;
    }
    return nullptr;
}

QAction* named_action(ShellWindow& window, const char* name)
{
    return window.findChild<QAction*>(QString::fromLatin1(name));
}

vss::gui::LauncherCard* launcher_card(ShellWindow& window, const QString& title)
{
    for (auto* card : window.findChildren<vss::gui::LauncherCard*>())
    {
        if (card->accessibleName() == title)
            return card;
    }
    return nullptr;
}

/// A shown window with the launcher up, as a first start looks.
struct Session
{
    Session()
    {
        clear_settings();
        // Closing the window must not stop on the unsaved-changes prompt.
        QSettings().setValue(QStringLiteral("ui/askOnDiscard"), false);
        window = std::make_unique<ShellWindow>(backend);
        window->resize(1280, 800);
        window->show();
        QApplication::processEvents();
    }

    ~Session()
    {
        window->close();
        QApplication::processEvents();
    }

    FakeStudioBackend backend;
    std::unique_ptr<ShellWindow> window;
};

} // namespace

TEST_CASE("journey: give my program a serial port, with the keyboard")
{
    Session session;
    auto& window = *session.window;

    auto* card = launcher_card(window, QStringLiteral("Give my program a "
                                                      "serial port"));
    REQUIRE(card != nullptr);
    REQUIRE(card->isVisible());

    Waiting finish([](QWidget* widget)
                   { return is_a<vss::gui::PortReadyDialog>(widget); },
                   [](QWidget* dialog)
                   {
                       auto* path = dialog->findChild<QLabel*>(
                           QStringLiteral("portReadyPathValue"));
                       REQUIRE(path != nullptr);
                       REQUIRE(!path->text().isEmpty());
                       auto* done = button(dialog, QStringLiteral("Done"));
                       REQUIRE(done != nullptr);
                       QTest::keyClick(done, Qt::Key_Space);
                   });
    Waiting create(
        [](QWidget* widget) { return is_a<vss::gui::AddPortDialog>(widget); },
        [](QWidget* dialog)
        {
            auto* name =
                dialog->findChild<QLineEdit*>(QStringLiteral("addPortName"));
            REQUIRE(name != nullptr);
            name->selectAll();
            QTest::keyClicks(name, QStringLiteral("terminal"));
            QTest::keyClick(name, Qt::Key_Return);
        });

    card->setFocus();
    QTest::keyClick(card, Qt::Key_Return);
    QApplication::processEvents();

    REQUIRE(create.ran());
    REQUIRE(finish.ran());
    REQUIRE_EQ(session.backend.ports().size(), 1);
    REQUIRE_EQ(session.backend.ports().front().name,
               QStringLiteral("terminal"));
    REQUIRE(!card->isVisible());

    auto* ports_dock =
        window.findChild<QDockWidget*>(QStringLiteral("portsDock"));
    REQUIRE(ports_dock != nullptr);
    REQUIRE(ports_dock->isVisible());
    auto* list = ports_dock->findChild<QListView*>();
    REQUIRE(list != nullptr);
    REQUIRE_EQ(list->model()->rowCount(), 1);
}

TEST_CASE("journey: connect two programs")
{
    Session session;
    auto& window = *session.window;

    auto* card = launcher_card(window, QStringLiteral("Connect two programs"));
    REQUIRE(card != nullptr);

    Waiting create(
        [](QWidget* widget) { return is_a<vss::gui::AddPortDialog>(widget); },
        [](QWidget* dialog)
        {
            auto* primary = button(dialog, QStringLiteral("Create pair"));
            REQUIRE(primary != nullptr);
            REQUIRE(primary->isEnabled());
            QTest::mouseClick(primary, Qt::LeftButton);
        });

    QTest::mouseClick(card, Qt::LeftButton);
    QApplication::processEvents();

    REQUIRE(create.ran());
    REQUIRE_EQ(session.backend.ports().size(), 2);
    REQUIRE_EQ(session.backend.links().size(), 1);
    auto* status = window.findChild<QStatusBar*>();
    REQUIRE(status != nullptr);
    REQUIRE(status->currentMessage().startsWith(
        QStringLiteral("Linked pair created.")));
}

TEST_CASE("journey: watch traffic on a port")
{
    Session session;
    auto& window = *session.window;

    const auto port = session.backend.create_virtual_port(
        QStringLiteral("device"), vss::virtual_port_kind::loopback, true);
    REQUIRE(port.has_value());
    QApplication::processEvents();

    auto* watch_action = named_action(window, "topology.watch");
    REQUIRE(watch_action != nullptr);

    Waiting start(
        [](QWidget* widget) { return is_a<vss::gui::WatchDialog>(widget); },
        [](QWidget* dialog)
        {
            auto* primary = button(dialog, QStringLiteral("Start watching"));
            REQUIRE(primary != nullptr);
            REQUIRE(primary->isEnabled());
            QTest::mouseClick(primary, Qt::LeftButton);
        });

    watch_action->trigger();
    QApplication::processEvents();

    REQUIRE(start.ran());
    REQUIRE_EQ(session.backend.ports().size(), 2);
    REQUIRE_EQ(session.backend.links().size(), 1);
    auto* monitor =
        window.findChild<QDockWidget*>(QStringLiteral("monitorDock"));
    REQUIRE(monitor != nullptr);
    REQUIRE(monitor->isVisible());
}

TEST_CASE("journey: run an action from the command palette")
{
    Session session;
    auto& window = *session.window;

    const auto port = session.backend.create_virtual_port(
        QStringLiteral("device"), vss::virtual_port_kind::loopback, true);
    REQUIRE(port.has_value());
    QApplication::processEvents();

    auto* advanced = named_action(window, "view.advanced");
    REQUIRE(advanced != nullptr);
    REQUIRE(!advanced->isChecked());

    Waiting choose([](QWidget* widget)
                   { return is_a<vss::gui::CommandPalette>(widget); },
                   [](QWidget* dialog)
                   {
                       auto* search = dialog->findChild<QLineEdit*>();
                       REQUIRE(search != nullptr);
                       QTest::keyClicks(search, QStringLiteral("advanced"));
                       QTest::keyClick(search, Qt::Key_Return);
                   });

    QTest::keyClick(&window, Qt::Key_K, Qt::ControlModifier);
    QApplication::processEvents();

    REQUIRE(choose.ran());
    REQUIRE(advanced->isChecked());
}

TEST_CASE("journey: remove a port with the Delete key")
{
    Session session;
    auto& window = *session.window;

    const auto port = session.backend.create_virtual_port(
        QStringLiteral("device"), vss::virtual_port_kind::loopback, true);
    REQUIRE(port.has_value());
    QApplication::processEvents();

    auto* ports_dock =
        window.findChild<QDockWidget*>(QStringLiteral("portsDock"));
    REQUIRE(ports_dock != nullptr);
    auto* list = ports_dock->findChild<QListView*>();
    REQUIRE(list != nullptr);
    list->setFocus();
    list->setCurrentIndex(list->model()->index(0, 0));
    QApplication::processEvents();

    Waiting confirm(
        [](QWidget* widget)
        { return button(widget, QStringLiteral("Remove port")) != nullptr; },
        [](QWidget* dialog)
        {
            QTest::mouseClick(button(dialog, QStringLiteral("Remove port")),
                              Qt::LeftButton);
        });

    QTest::keyClick(list, Qt::Key_Delete);
    QApplication::processEvents();

    REQUIRE(confirm.ran());
    REQUIRE(session.backend.ports().empty());
    auto* card = launcher_card(window, QStringLiteral("Give my program a "
                                                      "serial port"));
    REQUIRE(card != nullptr);
    REQUIRE(card->isVisible());
}
