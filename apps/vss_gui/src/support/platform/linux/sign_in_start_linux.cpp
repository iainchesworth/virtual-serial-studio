//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

// Linux: an XDG autostart file,
// $XDG_CONFIG_HOME/autostart/virtual-serial-studio.desktop.

#include "support/sign_in_start.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>

namespace vss::gui::support
{
namespace
{

QString desktop_file(const sign_in_location& location)
{
    return QDir(location.where)
        .filePath(QStringLiteral("virtual-serial-studio.desktop"));
}

} // namespace

sign_in_location default_sign_in_location()
{
    sign_in_location location;
    location.where = QDir(QStandardPaths::writableLocation(
                              QStandardPaths::GenericConfigLocation))
                         .filePath(QStringLiteral("autostart"));
    return location;
}

sign_in_location test_sign_in_location(const QString& scratch)
{
    sign_in_location location;
    location.where = QDir(scratch).filePath(QStringLiteral("autostart"));
    location.name = QStringLiteral("VSS GUI Tests");
    return location;
}

bool sign_in_start_enabled(const sign_in_location& location)
{
    return !location.where.isEmpty() && QFile::exists(desktop_file(location));
}

bool set_sign_in_start(bool enabled, const sign_in_location& location)
{
    if (location.where.isEmpty())
        return false;
    const QString path = desktop_file(location);
    if (!enabled)
        return !QFile::exists(path) || QFile::remove(path);
    if (!QDir().mkpath(location.where))
        return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const QString text =
        QStringLiteral("[Desktop Entry]\n"
                       "Type=Application\n"
                       "Name=%1\n"
                       "Exec=%2\n"
                       "NoDisplay=true\n"
                       "X-GNOME-Autostart-enabled=true\n")
            .arg(location.name,
                 sign_in_command(QCoreApplication::applicationFilePath()));
    const QByteArray bytes = text.toUtf8();
    if (file.write(bytes) != bytes.size())
    {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

} // namespace vss::gui::support
