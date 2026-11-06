//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "../vcom_driver_platform.hpp"

namespace vss::detail
{

// The filter driver exists only on Windows.

bool platform_driver_available()
{
    return false;
}

std::string platform_driver_version()
{
    return {};
}

result<virtual_port_result>
platform_create_driver_port(const transport::executor_type&,
                            const std::string&, unsigned)
{
    return make_error_code(errc::unsupported);
}

result<std::string> platform_set_driver_com_number(transport&, unsigned)
{
    return make_error_code(errc::unsupported);
}

std::optional<driver_peer_info> platform_driver_peer(const std::string&)
{
    return std::nullopt;
}

} // namespace vss::detail
