//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#pragma once

#include <string>
#include <vector>

namespace vss::gui
{

/// The lowest COM number the window asks the Windows driver for.  COM1 and
/// COM2 are the legacy on-board ports: Windows, firmware and many programs
/// treat them as always there, and a program saved with one of them may
/// already have been configured for a real UART.  A port made here starts at
/// COM3 or higher.
constexpr unsigned first_gui_com_number = 3;

/// The highest COM number the driver can give (see
/// `vss::max_driver_com_number`).
constexpr unsigned last_gui_com_number = 255;

/// The COM numbers named by the paths of ports this computer already reports
/// (`COM7`, `com12`).  Any other path, such as `/dev/ttyUSB0`, is ignored.
std::vector<unsigned> com_numbers_in_use(const std::vector<std::string>& paths);

/// The number to try first for a new port: the one above every number in use,
/// so that a new port never lands between ports another program has saved,
/// and never lower than `first_gui_com_number`.  When nothing is left above
/// the highest, the lowest free number from `first_gui_com_number` up.
/// 0 when every number is taken.
unsigned first_com_candidate(const std::vector<unsigned>& in_use);

/// The number to try after `candidate` was refused because another device
/// holds it (Windows keeps names in its COM name database that no list of
/// ports shows).  It moves up one, skips the numbers in use, and wraps to
/// `first_gui_com_number` after `last_gui_com_number`.  0 when it has gone
/// all the way round to `start`, where `start` is the first candidate.
unsigned next_com_candidate(unsigned candidate, unsigned start,
                            const std::vector<unsigned>& in_use);

} // namespace vss::gui
