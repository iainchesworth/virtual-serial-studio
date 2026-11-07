//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#pragma once

namespace vss::gui::support
{

/// Gives a program that was started from a terminal its output back.
///
/// On Windows `vss_gui` is a program of the Windows subsystem, so that
/// starting it from the Start menu, Explorer or the sign-in entry never opens
/// a console window. A program of that kind has no standard output unless the
/// caller redirected it. This attaches to the console of the parent process,
/// when there is one, and points `stdout` and `stderr` at it, so `--version`,
/// `--help` and `--selftest` still print to the terminal that ran them. A
/// stream the caller redirected to a file or a pipe is left alone. It never
/// creates a console. Call it first thing in `main`.
///
/// Elsewhere the program already has the standard streams of its parent, and
/// this does nothing.
void attach_parent_console();

} // namespace vss::gui::support
