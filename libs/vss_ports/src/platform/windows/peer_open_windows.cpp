//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "../peer_open_platform.hpp"

namespace vss::detail
{

// There is no portable way here to ask which process has a device open, so
// the answer is always "unknown" and the GUI says nothing about the program.
bool platform_peer_open_supported() noexcept
{
    return false;
}

std::vector<peer_open_state>
platform_peer_open(const std::vector<std::string>& paths)
{
    return std::vector<peer_open_state>(paths.size(), peer_open_state::unknown);
}

} // namespace vss::detail
