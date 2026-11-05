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

result<virtual_port_result>
create_driver_port(const transport::executor_type& executor,
                   const std::string& name)
{
    return detail::platform_create_driver_port(executor, name);
}

} // namespace vss
