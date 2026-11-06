//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#pragma once

#include "backend/view_types.hpp"

#include <QString>

#include <vector>

namespace vss::gui
{

/// The ports that a program is known to have open, in the order given.  Only a
/// port whose backend said `peer_state::open` counts: a port the platform
/// cannot observe is never claimed to be in use.
std::vector<port_view> ports_in_use(const std::vector<port_view>& ports);

/// The sentence that goes in front of the removal question when a program has
/// the port open, or empty when none is known to.
QString removal_warning(const port_view& port);

/// The warning shown before the window closes, naming the ports that programs
/// have open, or empty when none do.  Closing removes the ports Studio made, so
/// the programs lose them.
QString exit_warning(const std::vector<port_view>& in_use);

/// The warning shown before File, New removes every port, naming the ports
/// that programs have open, or empty when none do.
QString new_setup_warning(const std::vector<port_view>& in_use);

} // namespace vss::gui

