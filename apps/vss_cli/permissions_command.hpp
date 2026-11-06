//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_CLI_PERMISSIONS_COMMAND_HPP
#define VSS_CLI_PERMISSIONS_COMMAND_HPP

#include <string>
#include <vector>

namespace vss_cli
{

/// `vss_cli permissions ...`. `arguments` is everything after the program name,
/// starting with "permissions". Returns the process exit code.
int command_permissions(const std::vector<std::string>& arguments);

/// The text of `permissions` in `--help`.
const char* permissions_usage() noexcept;

} // namespace vss_cli

#endif // VSS_CLI_PERMISSIONS_COMMAND_HPP
