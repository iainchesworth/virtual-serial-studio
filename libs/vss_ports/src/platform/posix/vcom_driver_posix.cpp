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

} // namespace vss::detail
