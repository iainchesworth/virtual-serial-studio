//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "ui_platform.hpp"

#include <time.h>

namespace vss::gui::ui::platform
{

std::int64_t thread_cpu_ns()
{
    timespec time{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &time);
    return static_cast<std::int64_t>(time.tv_sec) * 1'000'000'000 +
           time.tv_nsec;
}

void prepare_environment() {}

void prepare_application() {}

void install_crash_hooks(void (*)(const char*))
{
    // The harness installs its own handler for SIGABRT; a fatal signal such
    // as SIGSEGV ends the process with the system's usual report.
}

} // namespace vss::gui::ui::platform
