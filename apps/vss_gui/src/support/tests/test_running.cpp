//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "test_framework.hpp"

#include "support/running_setup.hpp"
#include "support/sign_in_start.hpp"

#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>

namespace
{

vss::config::document one_port()
{
    vss::config::document document;
    vss::config::port_entry port;
    port.name = "console";
    port.kind = "virtual";
    port.virtual_kind = "loopback";
    document.ports.push_back(port);
    return document;
}

} // namespace

TEST_CASE("running setup: what was running is read back with its facts")
{
    QSettings().clear();
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const QString path =
        directory.filePath(QStringLiteral("running-setup.json"));
    const vss::gui::support::running_state state{
        QStringLiteral("C:/setups/lab.json"), true, false};
    REQUIRE(vss::gui::support::save_running_setup(path, one_port(), state));

    bool damaged = true;
    const auto setup = vss::gui::support::load_running_setup(path, &damaged);
    REQUIRE(!damaged);
    REQUIRE(setup.has_value());
    REQUIRE(setup->ports == 1);
    REQUIRE(setup->links == 0);
    REQUIRE(setup->document.ports.front().name == "console");
    REQUIRE(setup->state.document_path == QStringLiteral("C:/setups/lab.json"));
    REQUIRE(setup->state.dirty);
    REQUIRE(!setup->state.stopped);
    QSettings().clear();
}

TEST_CASE("running setup: none without a file or without ports")
{
    QSettings().clear();
    QTemporaryDir directory;
    const QString path =
        directory.filePath(QStringLiteral("running-setup.json"));
    bool damaged = true;
    REQUIRE(!vss::gui::support::load_running_setup(path, &damaged));
    REQUIRE(!damaged);

    REQUIRE(vss::gui::support::save_running_setup(path, {}, {}));
    REQUIRE(!vss::gui::support::load_running_setup(path, &damaged));
    REQUIRE(!damaged);
    REQUIRE(QFile::exists(path));
}

TEST_CASE("running setup: a damaged file is moved aside, never applied")
{
    QSettings().clear();
    QTemporaryDir directory;
    const QString path =
        directory.filePath(QStringLiteral("running-setup.json"));
    {
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write("{ not json");
    }
    bool damaged = false;
    REQUIRE(!vss::gui::support::load_running_setup(path, &damaged));
    REQUIRE(damaged);
    REQUIRE(!QFile::exists(path));
    REQUIRE(QFile::exists(
        directory.filePath(QStringLiteral("running-setup.bad.json"))));
}

TEST_CASE("running setup: a Quit is remembered with the ports")
{
    QTemporaryDir directory;
    const QString path =
        directory.filePath(QStringLiteral("running-setup.json"));
    REQUIRE(!vss::gui::support::save_running_state(path, {}));
    REQUIRE(vss::gui::support::save_running_setup(path, one_port(), {}));
    REQUIRE(vss::gui::support::save_running_state(
        path, {QStringLiteral("lab.json"), false, true}));
    const auto state = vss::gui::support::load_running_state(path);
    REQUIRE(state.stopped);
    REQUIRE(!state.dirty);
    REQUIRE(state.document_path == QStringLiteral("lab.json"));
    const auto setup = vss::gui::support::load_running_setup(path);
    REQUIRE(setup.has_value());
    REQUIRE(setup->ports == 1);
    REQUIRE(setup->state.stopped);
}

TEST_CASE("running setup: a file of another version is not applied")
{
    QTemporaryDir directory;
    const QString path =
        directory.filePath(QStringLiteral("running-setup.json"));
    {
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write(R"({"version": 99, "setup": {"ports": []}})");
    }
    bool damaged = false;
    REQUIRE(!vss::gui::support::load_running_setup(path, &damaged));
    REQUIRE(damaged);
}

TEST_CASE("sign-in start: the entry runs Studio hidden")
{
    REQUIRE(vss::gui::support::sign_in_command(
                QStringLiteral("C:/A B/vss_gui.exe")) ==
            QStringLiteral("\"C:/A B/vss_gui.exe\" --background"));
}

TEST_CASE("sign-in start: the entry is added and removed")
{
#if defined(Q_OS_MACOS)
    // No entry on macOS yet: it must say so rather than pretend.
    REQUIRE(!vss::gui::support::sign_in_start_available());
    REQUIRE(!vss::gui::support::set_sign_in_start(true));
#else
    vss::gui::support::sign_in_location location;
    QTemporaryDir directory;
#if defined(Q_OS_WIN)
    location.where =
        QStringLiteral("HKEY_CURRENT_USER\\Software\\VSS GUI Tests\\Run");
#else
    location.where = directory.filePath(QStringLiteral("autostart"));
#endif
    location.name = QStringLiteral("VSS GUI Tests");
    REQUIRE(vss::gui::support::set_sign_in_start(false, location));
    REQUIRE(!vss::gui::support::sign_in_start_enabled(location));
    REQUIRE(vss::gui::support::set_sign_in_start(true, location));
    REQUIRE(vss::gui::support::sign_in_start_enabled(location));
    REQUIRE(vss::gui::support::set_sign_in_start(false, location));
    REQUIRE(!vss::gui::support::sign_in_start_enabled(location));
#if defined(Q_OS_WIN)
    QSettings(QStringLiteral("HKEY_CURRENT_USER\\Software\\VSS GUI Tests"),
              QSettings::NativeFormat)
        .remove(QStringLiteral("Run"));
#endif
#endif
}
