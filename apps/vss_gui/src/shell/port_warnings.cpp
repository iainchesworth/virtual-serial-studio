//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "shell/port_warnings.hpp"

#include "flows/common.hpp"

#include <QCoreApplication>
#include <QStringList>

#include <algorithm>

namespace vss::gui
{
std::vector<port_view> ports_in_use(const std::vector<port_view>& ports)
{
    std::vector<port_view> out;
    std::copy_if(ports.begin(), ports.end(), std::back_inserter(out),
                 [](const port_view& port)
                 { return port.open && port.peer == peer_state::open; });
    return out;
}

QString removal_warning(const port_view& port)
{
    if (!port.open || port.peer != peer_state::open)
        return {};
    return QCoreApplication::translate("PortWarnings", "A program has %1 open. Removing the port takes it away from "
              "that program, which may stop or show an error.")
        .arg(port_display_name(port));
}

QString exit_warning(const std::vector<port_view>& in_use)
{
    if (in_use.empty())
        return {};
    QStringList names;
    for (const auto& port : in_use)
        names << port_display_name(port);
    if (in_use.size() == 1)
    {
        return QCoreApplication::translate("PortWarnings", "A program has %1 open. Closing Studio removes the port "
                  "under it, and the program may stop or show an error.")
            .arg(names.first());
    }
    return QCoreApplication::translate("PortWarnings", "Programs have these ports open: %1. Closing Studio removes the "
              "ports under them, and the programs may stop or show an error.")
        .arg(names.join(QStringLiteral(", ")));
}

QString new_setup_warning(const std::vector<port_view>& in_use)
{
    if (in_use.empty())
        return {};
    QStringList names;
    for (const auto& port : in_use)
        names << port_display_name(port);
    return QCoreApplication::translate(
               "PortWarnings",
               "Programs have these ports open: %1. A new setup removes every "
               "port, and the programs may stop or show an error.")
        .arg(names.join(QStringLiteral(", ")));
}

} // namespace vss::gui
