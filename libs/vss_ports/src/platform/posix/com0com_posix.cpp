//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "../com0com_platform.hpp"

namespace vss::detail
{

// com0com is a Windows driver; elsewhere every operation is unsupported.

com0com_installation platform_detect_com0com()
{
    return {};
}

result<void> platform_create_com0com_pair(const com0com_installation&,
                                          std::string_view, std::string_view,
                                          bool)
{
    return make_error_code(errc::unsupported);
}

result<void> platform_remove_com0com_pair(const com0com_installation&, int,
                                          bool)
{
    return make_error_code(errc::unsupported);
}

} // namespace vss::detail
