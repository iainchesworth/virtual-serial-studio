//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "../posix/pty_slave_path.hpp"

#include <stdlib.h>

#include <mutex>

namespace vss::detail
{

bool platform_pty_slave_path(int master, std::string& path)
{
    // ptsname returns a pointer to static storage.
    static std::mutex ptsname_mutex;
    const std::lock_guard lock(ptsname_mutex);
    const char* resolved = ::ptsname(master);
    if (!resolved)
        return false;
    path = resolved;
    return true;
}

} // namespace vss::detail
