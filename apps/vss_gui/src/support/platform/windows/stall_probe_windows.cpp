//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

// Windows: the calling thread's kernel and user time.

#include "support/stall_probe.hpp"

#include <QtCore/qt_windows.h>

namespace vss::gui::support
{

namespace
{

qint64 hundred_nanoseconds(const FILETIME& time)
{
    ULARGE_INTEGER value;
    value.LowPart = time.dwLowDateTime;
    value.HighPart = time.dwHighDateTime;
    return static_cast<qint64>(value.QuadPart);
}

} // namespace

qint64 StallProbe::thread_cpu_ns()
{
    FILETIME created{};
    FILETIME exited{};
    FILETIME kernel{};
    FILETIME user{};
    if (GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user) ==
        0)
        return 0;
    // Units of 100 ns. The system counts them in scheduler ticks, so the
    // answer is good to about 16 ms.
    return (hundred_nanoseconds(kernel) + hundred_nanoseconds(user)) * 100;
}

} // namespace vss::gui::support
