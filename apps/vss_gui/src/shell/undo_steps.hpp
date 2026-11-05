//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_SHELL_UNDO_STEPS_HPP
#define VSS_GUI_SHELL_UNDO_STEPS_HPP

#include "backend/studio_backend.hpp"
#include "shell/undo_service.hpp"

#include <QString>

#include <optional>

namespace vss::gui
{

/// The link with the same shape and the same ports as `spec`, or empty.  Links
/// are matched by what they join, not by id, because the engine gives a link
/// that is made again a new id.
std::optional<link_view> find_link(const StudioBackend& backend,
                                   const link_view& spec);

/// A link was just made: Undo removes it, Redo makes it again.  `spec` is the
/// link as it was made (its shape, ports, options and owner).
undo_step connect_step(StudioBackend& backend, const link_view& spec,
                       const QString& text);

/// A link was just removed: Undo makes it again, Redo removes it.
undo_step remove_link_step(StudioBackend& backend, const link_view& spec,
                           const QString& text);

/// The owner of a link changed from `before` to `after` (zero means none).
undo_step owner_step(StudioBackend& backend, const link_view& spec,
                     vss::endpoint_id before, vss::endpoint_id after,
                     const QString& text);

/// A port was renamed from `before` to `after`.
undo_step rename_step(StudioBackend& backend, vss::endpoint_id port,
                      const QString& before, const QString& after,
                      const QString& text);

} // namespace vss::gui

#endif // VSS_GUI_SHELL_UNDO_STEPS_HPP
