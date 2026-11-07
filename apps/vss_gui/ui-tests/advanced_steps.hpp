//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The steps a person takes on the Advanced canvas.
//

#pragma once

#include "ui_harness.hpp"

#include "topology/add_sheet.hpp"
#include "topology/toolbox.hpp"
#include "topology/topology_view.hpp"

#include <QString>

#include <functional>
#include <vector>

namespace vss::gui::ui::adv
{

/// The toolbox tile of this label.
QListWidgetItem* tile(Session& session, const QString& label);

/// Arms the Add sheet: `fill` runs on it before Add is clicked (it may type
/// names). Without `fill` the default names are kept.
void expect_add_sheet(
    Session& session,
    const std::function<void(topology::AddSheet*)>& fill = {});

/// The visible name field of an Add sheet, top to bottom (0 is the first).
QLineEdit* sheet_name(topology::AddSheet* sheet, int which = 0);

/// Double-clicks a toolbox tile. The Add sheet that opens is answered by
/// `expect_add_sheet`.
void add_tile(Session& session, const QString& label);

/// Adds one port by tile with this name, and checks it appeared.
vss::endpoint_id add_named(Session& session, const QString& label,
                           const QString& name);

/// Clicks a node once.
void click_node(Session& session, vss::endpoint_id id,
                Qt::KeyboardModifiers mods = {});

/// Drags a wire from the right handle of one node onto another node, and
/// picks the kind from the link chooser that opens.
void wire(Session& session, vss::endpoint_id from, vss::endpoint_id to,
          wire_kind kind = wire_kind::two_way);

/// The same drag, when the chooser is expected to refuse: it opens no menu.
/// Returns true when no chooser opened.
bool wire_refused(Session& session, vss::endpoint_id from, vss::endpoint_id to);

/// Selects every node with Ctrl+A and deletes them, confirming.
void clear_canvas(Session& session);

/// Drops a toolbox tile on the canvas at a viewport point, the way a drag
/// from the toolbox ends.
void drop_tile(Session& session, canvas_item item, const QPoint& at);

/// Selects the ports with Ctrl+click.
void select_ports(Session& session, const std::vector<vss::endpoint_id>& ports);

/// Presses Delete on the canvas and confirms.
void delete_selection(Session& session, bool expect_confirm = true);

/// The canvas and the Ports dock agree with the backend.
void require_consistent(Session& session);

} // namespace vss::gui::ui::adv
