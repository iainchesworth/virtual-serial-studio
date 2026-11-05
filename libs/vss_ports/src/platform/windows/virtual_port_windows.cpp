//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "../virtual_port_platform.hpp"

namespace vss::detail
{

// Windows has no pseudo terminals; the driver and com0com cover real ports.

bool platform_supports_pty() noexcept
{
    return false;
}

result<virtual_port_result> platform_create_pty(const transport::executor_type&,
                                                const std::string&)
{
    return make_error_code(errc::unsupported);
}

} // namespace vss::detail
