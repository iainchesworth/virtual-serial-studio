//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "../com0com_platform.hpp"

namespace vss::gui::detail
{

bool platform_supports_com0com_pair_creation() noexcept
{
    return false;
}

vss::result<void> platform_create_com0com_pair(vss::studio&)
{
    return vss::make_error_code(vss::errc::unsupported);
}

} // namespace vss::gui::detail
