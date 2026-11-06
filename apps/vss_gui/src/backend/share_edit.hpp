//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_BACKEND_SHARE_EDIT_HPP
#define VSS_GUI_BACKEND_SHARE_EDIT_HPP

#include "backend/studio_backend.hpp"
#include "backend/view_types.hpp"

#include <QString>
#include <QVector>

#include <optional>
#include <vector>

namespace vss::gui
{

/// A real device (or other non-virtual endpoint) shared with programs through
/// the virtual ports on its duplex link.  An attached device is the same
/// thing with one program port, and a share is the same thing with many.
struct share_ref
{
    vss::link_id link = 0;
    vss::endpoint_id device = 0;
    QVector<vss::endpoint_id> programs;
};

/// The share a port belongs to, as the device or as one of the program
/// ports.  Empty for a pair of virtual ports, a port with no link, a bus, a
/// join or a monitor.
std::optional<share_ref> find_share(const std::vector<port_view>& ports,
                                    const std::vector<link_view>& links,
                                    vss::endpoint_id port);

/// The share a link is. Empty when the link is not one.
std::optional<share_ref> find_share_of_link(const std::vector<port_view>& ports,
                                            const std::vector<link_view>& links,
                                            vss::link_id link);

struct share_added
{
    vss::link_id link = 0;
    vss::endpoint_id port = 0;
};

/// A name for a new program port that no port uses: `program-N`.
QString next_program_name(const std::vector<port_view>& ports);

/// Adds one more program port to an existing share.
///
/// The engine cannot add a target to a link that exists, so this creates the
/// port, then replaces the link with one from the device to the old ports
/// plus the new one.  The old program ports keep their names and paths, so
/// programs that have them open are not disturbed; only the few bytes in
/// flight during the swap can be lost.  When any step fails the old link is
/// restored and the new port is removed, so the setup is as it was.
vss::result<share_added> add_program_port(StudioBackend& backend,
                                          const share_ref& share,
                                          const QString& name, bool capture);

/// Removes one program port from a share and keeps the others shared.
///
/// Removing an endpoint makes the engine drop every link it is on, so a plain
/// remove would leave the other programs with ports that no longer carry the
/// device's data.  This removes the port, then links the device to the
/// remaining ports again.  Returns the new link, or 0 when no program port
/// is left.  When the port cannot be removed nothing has changed.
vss::result<vss::link_id> remove_program_port(StudioBackend& backend,
                                              const share_ref& share,
                                              vss::endpoint_id program);

} // namespace vss::gui

#endif // VSS_GUI_BACKEND_SHARE_EDIT_HPP
