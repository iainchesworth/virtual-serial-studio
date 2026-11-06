//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <vss/ports/vcom_driver.hpp>

#include "platform/vcom_driver_platform.hpp"

namespace vss
{

namespace
{

bool valid_com_number(unsigned number) noexcept
{
    return number >= min_driver_com_number && number <= max_driver_com_number;
}

} // namespace

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
                   const std::string& name, unsigned com_number)
{
    // The same answer on every system and with or without a driver, so that a
    // wrong number in a saved topology is caught where it is read.
    if (com_number != 0 && !valid_com_number(com_number))
        return make_error_code(errc::invalid_argument);
    return detail::platform_create_driver_port(executor, name, com_number);
}

result<std::string> set_driver_com_number(transport& port, unsigned com_number)
{
    if (!valid_com_number(com_number))
        return make_error_code(errc::invalid_argument);
    return detail::platform_set_driver_com_number(port, com_number);
}

std::optional<driver_peer_info> driver_peer(const std::string& com_name)
{
    return detail::platform_driver_peer(com_name);
}

} // namespace vss
