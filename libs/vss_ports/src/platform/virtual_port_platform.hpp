//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_PORTS_VIRTUAL_PORT_PLATFORM_HPP
#define VSS_PORTS_VIRTUAL_PORT_PLATFORM_HPP

#include <vss/ports/virtual_port.hpp>

#include <string>

namespace vss::detail
{

/// Implemented once for each operating system in `src/platform/<os>/`.
/// True when pseudo terminals can be created.
bool platform_supports_pty() noexcept;

/// Creates a pseudo terminal pair, or reports `errc::unsupported`.
result<virtual_port_result>
platform_create_pty(const transport::executor_type& executor,
                    const std::string& name);

} // namespace vss::detail

#endif // VSS_PORTS_VIRTUAL_PORT_PLATFORM_HPP
