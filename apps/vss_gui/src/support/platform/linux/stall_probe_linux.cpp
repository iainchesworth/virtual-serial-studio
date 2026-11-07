//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

// Linux: the calling thread's CPU clock.

#include "support/stall_probe.hpp"

#include <ctime>

namespace vss::gui::support
{

qint64 StallProbe::thread_cpu_ns()
{
    timespec now{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now) != 0)
        return 0;
    return static_cast<qint64>(now.tv_sec) * 1'000'000'000 +
           static_cast<qint64>(now.tv_nsec);
}

} // namespace vss::gui::support
