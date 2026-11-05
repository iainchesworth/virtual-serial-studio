//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <vss/ports/vcom_driver.hpp>

#include "platform/vcom_driver_platform.hpp"

namespace vss
{

bool driver_available()
{
    return detail::platform_driver_available();
}

std::string driver_version()
{
    return detail::platform_driver_version();
}

result<virtual_port_result> create_driver_port(const transport::executor_type&,
                                               const std::string&)
{
    // The driver client (control device IOCTL + shared ring transport) is the
    // next milestone.  Reporting unsupported rather than failing later keeps
    // the auto-selection path honest.
    return make_error_code(errc::unsupported);
}

} // namespace vss
