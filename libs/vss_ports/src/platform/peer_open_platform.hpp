//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_PORTS_PEER_OPEN_PLATFORM_HPP
#define VSS_PORTS_PEER_OPEN_PLATFORM_HPP

#include <vss/ports/peer_open.hpp>

#include <string>
#include <vector>

namespace vss::detail
{

/// Implemented once for each operating system in `src/platform/<os>/`.
bool platform_peer_open_supported() noexcept;

/// One state per path, in order.  Only called when the platform is supported.
std::vector<peer_open_state>
platform_peer_open(const std::vector<std::string>& paths);

} // namespace vss::detail

#endif // VSS_PORTS_PEER_OPEN_PLATFORM_HPP
