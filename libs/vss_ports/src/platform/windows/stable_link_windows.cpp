//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "../stable_link_platform.hpp"

#include <stdlib.h>

#include <memory>

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

error_code platform_check_private_directory(const std::filesystem::path&)
{
    return make_error_code(errc::unsupported);
}

std::optional<std::string> platform_getenv(std::string_view name)
{
    const std::string key(name);
    char* raw = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&raw, &length, key.c_str()) != 0 || raw == nullptr)
        return std::nullopt;
    const std::unique_ptr<char, decltype(&std::free)> owner(raw, &std::free);
    return std::string(raw);
}

bool platform_is_character_device(const std::filesystem::path&) noexcept
{
    return false;
}

} // namespace vss::detail
