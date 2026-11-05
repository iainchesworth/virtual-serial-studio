//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "../posix/pty_slave_path.hpp"

#include <stdlib.h>

namespace vss::detail
{

bool platform_pty_slave_path(int master, std::string& path)
{
    char buffer[256]{};
    if (::ptsname_r(master, buffer, sizeof(buffer)) != 0)
        return false;
    path = buffer;
    return true;
}

} // namespace vss::detail
