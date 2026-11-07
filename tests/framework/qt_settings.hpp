//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#pragma once

// QSettings for a GUI test program, kept in a directory only that process
// knows. By default QSettings use the registry on Windows and a file in the
// user's configuration directory elsewhere, shared by every process of the
// same organisation and application name. Two GUI test programs running at
// once then clear each other's settings: a test that has just turned off the
// "save changes?" prompt gets it back and its window waits on a dialog nobody
// answers. Construct one `isolated_settings` in `main`, after the application
// and organisation names are set and before the first QSettings is made.

#include <QSettings>
#include <QString>
#include <QTemporaryDir>

namespace vss_test
{

class isolated_settings
{
public:
    isolated_settings()
    {
        if (!directory_.isValid())
            return;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           directory_.path());
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope,
                           directory_.path());
    }

    isolated_settings(const isolated_settings&) = delete;
    isolated_settings& operator=(const isolated_settings&) = delete;

    QString directory() const { return directory_.path(); }

private:
    QTemporaryDir directory_;
};

} // namespace vss_test
