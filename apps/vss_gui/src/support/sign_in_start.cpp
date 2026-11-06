//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "support/sign_in_start.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

namespace vss::gui::support
{
namespace
{

QString program_path()
{
    return QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
}

#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
QString desktop_file(const sign_in_location& location)
{
    return QDir(location.where)
        .filePath(QStringLiteral("virtual-serial-studio.desktop"));
}
#endif

} // namespace

sign_in_location default_sign_in_location()
{
    sign_in_location location;
#if defined(Q_OS_WIN)
    location.where = QStringLiteral(
        "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");
#elif defined(Q_OS_MACOS)
    // No login item yet; see the header.
#else
    location.where = QDir(QStandardPaths::writableLocation(
                              QStandardPaths::GenericConfigLocation))
                         .filePath(QStringLiteral("autostart"));
#endif
    return location;
}

bool sign_in_start_available()
{
    return !default_sign_in_location().where.isEmpty();
}

QString sign_in_command(const QString& program)
{
    return QStringLiteral("\"%1\" --background").arg(program);
}

bool sign_in_start_enabled(const sign_in_location& location)
{
    if (location.where.isEmpty())
        return false;
#if defined(Q_OS_WIN)
    const QSettings run(location.where, QSettings::NativeFormat);
    return !run.value(location.name).toString().isEmpty();
#elif defined(Q_OS_MACOS)
    return false;
#else
    return QFile::exists(desktop_file(location));
#endif
}

bool set_sign_in_start(bool enabled, const sign_in_location& location)
{
    if (location.where.isEmpty())
        return false;
#if defined(Q_OS_WIN)
    QSettings run(location.where, QSettings::NativeFormat);
    if (enabled)
        run.setValue(location.name, sign_in_command(program_path()));
    else
        run.remove(location.name);
    run.sync();
    return run.status() == QSettings::NoError &&
           sign_in_start_enabled(location) == enabled;
#elif defined(Q_OS_MACOS)
    static_cast<void>(enabled);
    return false;
#else
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
            .arg(location.name, sign_in_command(program_path()));
    const QByteArray bytes = text.toUtf8();
    if (file.write(bytes) != bytes.size())
    {
        file.cancelWriting();
        return false;
    }
    return file.commit();
#endif
}

} // namespace vss::gui::support
