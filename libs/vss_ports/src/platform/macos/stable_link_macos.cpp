//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "../posix/posix_private_directory.hpp"
#include "../stable_link_platform.hpp"

namespace vss::detail
{

bool platform_supports_stable_links() noexcept
{
    return true;
}

std::vector<std::filesystem::path> platform_link_directories()
{
    return {"/tmp/vss"};
}

error_code
platform_ensure_private_directory(const std::filesystem::path& directory)
{
    return posix::ensure_private_directory(directory);
}

bool platform_is_character_device(const std::filesystem::path& path) noexcept
{
    return posix::is_character_device(path);
}

} // namespace vss::detail
