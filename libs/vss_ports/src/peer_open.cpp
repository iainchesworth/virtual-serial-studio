//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <vss/ports/peer_open.hpp>

#include "platform/peer_open_platform.hpp"

namespace vss
{

bool peer_open_supported() noexcept
{
    return detail::platform_peer_open_supported();
}

std::vector<peer_open_state> peer_open(const std::vector<std::string>& paths)
{
    if (paths.empty() || !detail::platform_peer_open_supported())
        return std::vector<peer_open_state>(paths.size(),
                                            peer_open_state::unknown);
    return detail::platform_peer_open(paths);
}

} // namespace vss
