//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

// Windows: a value under HKCU\Software\Microsoft\Windows\CurrentVersion\Run.

#include "support/sign_in_start.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>

namespace vss::gui::support
{

sign_in_location default_sign_in_location()
{
    sign_in_location location;
    location.where = QStringLiteral(
        "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");
    return location;
}

sign_in_location test_sign_in_location(const QString& /*scratch*/)
{
    sign_in_location location;
    location.where =
        QStringLiteral("HKEY_CURRENT_USER\\Software\\VSS GUI Tests\\Run");
    location.name = QStringLiteral("VSS GUI Tests");
    return location;
}

bool sign_in_start_enabled(const sign_in_location& location)
{
    if (location.where.isEmpty())
        return false;
    const QSettings run(location.where, QSettings::NativeFormat);
    return !run.value(location.name).toString().isEmpty();
}

bool set_sign_in_start(bool enabled, const sign_in_location& location)
{
    if (location.where.isEmpty())
        return false;
    QSettings run(location.where, QSettings::NativeFormat);
    if (enabled)
    {
        run.setValue(location.name,
                     sign_in_command(QDir::toNativeSeparators(
                         QCoreApplication::applicationFilePath())));
    }
    else
    {
        run.remove(location.name);
    }
    run.sync();
    return run.status() == QSettings::NoError &&
           sign_in_start_enabled(location) == enabled;
}

} // namespace vss::gui::support
