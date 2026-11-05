//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_PORTS_VCOM_DRIVER_PLATFORM_HPP
#define VSS_PORTS_VCOM_DRIVER_PLATFORM_HPP

#include <string>

namespace vss::detail
{

/// Implemented once for each operating system in `src/platform/<os>/`.
/// True when the filter driver is installed.
bool platform_driver_available();

/// The installed driver version, or an empty string.
std::string platform_driver_version();

} // namespace vss::detail

#endif // VSS_PORTS_VCOM_DRIVER_PLATFORM_HPP
