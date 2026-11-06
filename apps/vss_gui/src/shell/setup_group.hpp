//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_SHELL_SETUP_GROUP_HPP
#define VSS_GUI_SHELL_SETUP_GROUP_HPP

#include "backend/view_types.hpp"

#include <QString>
#include <QVector>

#include <vector>

namespace vss::gui
{

/// Everything that was built together: the ports joined to one another
/// through links, directly or through other ports.  A linked pair is two
/// ports and a link.  A shared device is the device, its program ports and
/// the split between them.  An attached device is the device, the virtual
/// port and their link.
struct setup_group
{
    /// In the order the ports snapshot lists them.
    QVector<vss::endpoint_id> ports;
    QVector<vss::link_id> links;

    bool empty() const noexcept { return ports.isEmpty(); }
    /// More than the one port that was asked about.
    bool has_company() const noexcept { return ports.size() > 1; }
};

/// The setup a port belongs to.  A port without links is a setup of one.  An
/// unknown id gives an empty group.
setup_group find_setup(const std::vector<port_view>& ports,
                       const std::vector<link_view>& links,
                       vss::endpoint_id port);

/// The setup a link belongs to, or an empty group for an unknown link.
setup_group find_setup_of_link(const std::vector<port_view>& ports,
                               const std::vector<link_view>& links,
                               vss::link_id link);

/// "COM7 and COM8" or "COM3, terminal and logger": the name a person knows
/// for each port, using its path when an outside program can open it.
QString setup_port_names(const setup_group& group,
                         const std::vector<port_view>& ports);

} // namespace vss::gui

#endif // VSS_GUI_SHELL_SETUP_GROUP_HPP
