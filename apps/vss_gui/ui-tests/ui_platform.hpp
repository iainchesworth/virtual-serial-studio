//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the UI suite needs from the operating system. One implementation per
// platform lives in platform/ and CMake picks it.
//

#pragma once

#include <cstdint>

namespace vss::gui::ui::platform
{

/// CPU time the calling thread has used, in nanoseconds.
std::int64_t thread_cpu_ns();

/// Environment the offscreen platform needs, set before the application is
/// made (a font directory on Windows, where the plugin finds none).
void prepare_environment();

/// The application font the tests' text measures depend on, set once the
/// application exists (Segoe UI on Windows, the default elsewhere).
void prepare_application();

/// Catches the platform's own ways of ending a program (an unhandled
/// structured exception, a failed assertion of the debug runtime, which opens
/// a message box and waits) and reports them through `crash`, which does not
/// return.
void install_crash_hooks(void (*crash)(const char* what));

} // namespace vss::gui::ui::platform
