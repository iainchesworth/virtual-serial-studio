//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_DIALOGS_SHARE_PLAN_HPP
#define VSS_GUI_DIALOGS_SHARE_PLAN_HPP

#include "backend/view_types.hpp"

#include <QString>
#include <QStringList>
#include <QVector>

#include <vector>

namespace vss::gui
{

/// The two ends the OS reported for one com0com null-modem pair.
struct com0com_pair_paths
{
    int pair = 0;
    QStringList ends;
};

QVector<com0com_pair_paths>
known_com0com_pairs(const std::vector<device_view>& devices);

/// One advertised path for every pair neither end of which the studio has
/// already claimed.  On Windows an auto virtual port opens one end and
/// advertises the other.
QStringList free_com0com_pair_paths(const std::vector<device_view>& devices,
                                    const std::vector<port_view>& ports);

/// How the programs in the share wizard map to ports other programs can open.
struct share_plan
{
    int programs = 1;
    int free_pairs = 0;
    bool windows = false;
    /// Programs that get a port another program can open.
    int openable = 1;
    /// Programs whose port only works inside Studio.
    int studio_only = 0;

    bool needs_warning() const { return studio_only > 0; }
};

share_plan make_share_plan(int programs, int free_pairs, bool windows);

/// The warn note's rich text: headline bold, cause and effect in one
/// sentence.  Empty when no warning is needed.
QString share_warning_rich(const share_plan& plan);
QString share_warning_plain(const share_plan& plan);

/// The port each program will open, as far as it can be known before the
/// share runs.  Studio-only rows use the engine's listener names.
QStringList planned_share_paths(const share_plan& plan,
                                const QStringList& free_pair_paths);

/// "first" to "thirty-second", used by the warning sentence.
QString ordinal_word(int value);

} // namespace vss::gui

#endif // VSS_GUI_DIALOGS_SHARE_PLAN_HPP
