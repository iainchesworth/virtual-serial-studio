//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "../stable_link_platform.hpp"

namespace vss::detail
{

// Windows names ports COMn.  The driver owns that namespace, so a link layer
// has no meaning here.

bool platform_supports_stable_links() noexcept
{
    return false;
}

std::vector<std::filesystem::path> platform_link_directories()
{
    return {};
}

error_code platform_ensure_private_directory(const std::filesystem::path&)
{
    return make_error_code(errc::unsupported);
}

bool platform_is_character_device(const std::filesystem::path&) noexcept
{
    return false;
}

} // namespace vss::detail
