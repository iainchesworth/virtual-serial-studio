//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_SUPPORT_BUNDLE_HPP
#define VSS_GUI_SUPPORT_BUNDLE_HPP

#include "backend/studio_backend.hpp"
#include "backend/view_types.hpp"
#include "support/event_log.hpp"

#include <QByteArray>
#include <QDate>
#include <QString>
#include <QVector>

#include <vector>

namespace vss::gui::support
{

/// What the user ticked in the diagnostic bundle dialog.
struct bundle_selection
{
    bool setup = true;
    bool event_log = true;
    bool environment = true;
    bool counters = true;
    /// Off by default, and the only option that can contain payload bytes.
    bool traffic = false;

    int file_count() const;
};

struct bundle_file
{
    QString name;
    QByteArray data;
};

/// `vss-bundle-<date>.zip`; `date` is injectable so tests are stable.
QString bundle_default_name(const QDate& date = QDate::currentDate());

/// The files for the selected options, in the order the dialog lists them.
/// Every textual field passes through `redact_secrets`.
QVector<bundle_file> build_bundle_files(const StudioBackend& backend,
                                        const EventLog& log,
                                        const bundle_selection& selection);

/// The human-readable state word used in `counters.csv`.
QString port_state_label(vss::endpoint_state state);

/// Renders the traffic ring as hex and ASCII with a header.  Only called when
/// the user opted in.
QString render_traffic(const std::vector<traffic_view>& traffic);

} // namespace vss::gui::support

#endif // VSS_GUI_SUPPORT_BUNDLE_HPP
