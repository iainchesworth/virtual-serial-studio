//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_BACKEND_OTHER_PORTS_HPP
#define VSS_GUI_BACKEND_OTHER_PORTS_HPP

#include "backend/view_types.hpp"

#include <QCoreApplication>
#include <QString>
#include <QStringList>

#include <cstddef>
#include <vector>

namespace vss::gui
{

/// "COM5", "COM5 and COM6", or "COM5, COM6, COM7 and 2 more": the ports
/// another program made, as a screen names them.  Empty for none.
inline QString other_ports_names(const std::vector<device_view>& ports)
{
    constexpr std::size_t shown_limit = 3;
    QStringList names;
    for (std::size_t i = 0; i < ports.size() && i < shown_limit; ++i)
        names.push_back(ports[i].path);
    if (names.isEmpty())
        return {};
    if (ports.size() > shown_limit)
        return QCoreApplication::translate("vss::gui::other_ports",
                                           "%1 and %2 more")
            .arg(names.join(QStringLiteral(", ")))
            .arg(ports.size() - shown_limit);
    if (names.size() == 1)
        return names.front();
    const QString last = names.takeLast();
    return QCoreApplication::translate("vss::gui::other_ports", "%1 and %2")
        .arg(names.join(QStringLiteral(", ")), last);
}

/// The sentence that says why those ports are not in the list.  It reads the
/// same for one port and for several.
inline QString other_ports_reason(const std::vector<device_view>& ports)
{
    const QString names = other_ports_names(ports);
    if (names.isEmpty())
        return {};
    return QCoreApplication::translate(
               "vss::gui::other_ports",
               "Another program made %1. Studio cannot show or watch ports it "
               "did not make itself.")
        .arg(names);
}

} // namespace vss::gui

#endif // VSS_GUI_BACKEND_OTHER_PORTS_HPP
