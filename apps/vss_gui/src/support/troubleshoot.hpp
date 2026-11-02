//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_SUPPORT_TROUBLESHOOT_HPP
#define VSS_GUI_SUPPORT_TROUBLESHOOT_HPP

#include "backend/view_types.hpp"

#include <QString>
#include <QVector>

namespace vss::gui::support
{

/// Everything "Why is nothing arriving?" is allowed to reason about.
///
/// The checklist never invents a fact.  Where the engine cannot know (whether
/// a program opened the advertised path, whether that program read the data),
/// the row is "Not checked yet" with the reason.  `now` is injected so the
/// one-minute windows are deterministic in tests.
struct troubleshoot_facts
{
    vss::endpoint_id port = 0;
    QVector<port_view> ports;
    QVector<link_view> links;
    /// True when `port_view::peer` carries a definite answer from the engine.
    bool peer_open_known = false;
    vss::time_point now{};
};

enum class check_state
{
    passed,
    attention,
    not_checked
};

struct check_row
{
    check_state state = check_state::not_checked;
    QString icon;
    QString title;
    QString sentence;
    QString chip;
    /// The amber row the design highlights (the missing program).
    bool highlight = false;
};

struct troubleshoot_report
{
    QVector<check_row> rows;
};

/// Builds the five checklist rows.  Pure and deterministic.
troubleshoot_report build_troubleshoot_report(const troubleshoot_facts& facts);

/// The path shown in the dialog title and in result sentences.
QString port_display_path(const port_view& port);

} // namespace vss::gui::support

#endif // VSS_GUI_SUPPORT_TROUBLESHOOT_HPP
