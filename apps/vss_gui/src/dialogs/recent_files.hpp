//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_DIALOGS_RECENT_FILES_HPP
#define VSS_GUI_DIALOGS_RECENT_FILES_HPP

#include <QDateTime>
#include <QString>
#include <QVector>

namespace vss::gui
{

/// One saved setup in the Recent list.
struct recent_entry
{
    QString path;
    QString summary;
    QDateTime last_opened;
};

/// Reads `ui/recentFiles` and the per-file summary and date settings.
QVector<recent_entry> recent_entries();

/// Adds `path` at the front, updates its summary and date, and writes the
/// compatibility keys the launcher reads.
void record_recent(const QString& path, const QString& summary);

/// The summary recorded for a path, or empty.
QString recent_summary(const QString& path);

} // namespace vss::gui

#endif // VSS_GUI_DIALOGS_RECENT_FILES_HPP
