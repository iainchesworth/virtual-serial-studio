//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#pragma once

#include <QString>

namespace vss::gui::support
{

/// The per-user entry that starts Studio, hidden, when the user signs in.
///
/// Windows: a value under `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`.
/// Linux and the BSDs: an XDG autostart file,
/// `$XDG_CONFIG_HOME/autostart/virtual-serial-studio.desktop`.  macOS has no
/// entry yet (a login item needs a signed bundle helper), so the setting is
/// disabled there with "Not available in this build".  The entry itself is
/// the setting: nothing about it is kept in QSettings, so removing it by hand
/// turns the switch off.
struct sign_in_location
{
    /// Windows: the registry key that holds the value.  Elsewhere: the
    /// folder that holds the autostart file.  Tests point it elsewhere.
    QString where;
    QString name = QStringLiteral("Virtual Serial Studio");
};

/// The real location for this platform.  `where` is empty where there is none.
sign_in_location default_sign_in_location();

/// True when this platform can start Studio at sign-in.
bool sign_in_start_available();

/// The command the entry runs: the program, quoted, then `--background`.
QString sign_in_command(const QString& program);

bool sign_in_start_enabled(const sign_in_location& location =
                               default_sign_in_location());

/// Adds or removes the entry.  False, with nothing changed, when it cannot.
bool set_sign_in_start(bool enabled, const sign_in_location& location =
                                         default_sign_in_location());

} // namespace vss::gui::support
