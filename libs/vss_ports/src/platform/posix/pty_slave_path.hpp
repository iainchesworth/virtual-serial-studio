//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_PORTS_PTY_SLAVE_PATH_HPP
#define VSS_PORTS_PTY_SLAVE_PATH_HPP

#include <string>

namespace vss::detail
{

/// The device path of the slave side of the pseudo terminal `master`.
/// Implemented in `src/platform/linux/` and `src/platform/macos/`, because
/// ptsname_r exists only on Linux. Returns false when the name is unknown.
bool platform_pty_slave_path(int master, std::string& path);

} // namespace vss::detail

#endif // VSS_PORTS_PTY_SLAVE_PATH_HPP
