//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_PORTS_VCOM_DRIVER_PLATFORM_HPP
#define VSS_PORTS_VCOM_DRIVER_PLATFORM_HPP

#include <vss/core/error.hpp>
#include <vss/ports/virtual_port.hpp>

#include <string>

namespace vss::detail
{

/// Implemented once for each operating system in `src/platform/<os>/`.
/// True when the driver is installed and has at least one port.
bool platform_driver_available();

/// The installed driver version, or an empty string.
std::string platform_driver_version();

/// Opens the hub end of a free driver port. `errc::unsupported` when there is
/// no driver, `errc::device_busy` when every port is taken.
result<virtual_port_result>
platform_create_driver_port(const transport::executor_type& executor,
                            const std::string& name);

} // namespace vss::detail

#endif // VSS_PORTS_VCOM_DRIVER_PLATFORM_HPP
