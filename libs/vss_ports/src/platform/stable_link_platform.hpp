//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_PORTS_STABLE_LINK_PLATFORM_HPP
#define VSS_PORTS_STABLE_LINK_PLATFORM_HPP

#include <vss/core/error.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vss::detail
{

/// Implemented once for each operating system in `src/platform/<os>/`.
bool platform_supports_stable_links() noexcept;

/// Directories tried after the environment variables, best first.
std::vector<std::filesystem::path> platform_link_directories();

/// Creates `directory` if it is missing and checks that it is a real
/// directory (not a link) owned by the caller with no access for anyone else.
error_code
platform_ensure_private_directory(const std::filesystem::path& directory);

/// The value of an environment variable, if it is set.
std::optional<std::string> platform_getenv(std::string_view name);

/// True when `path` is a character device.
bool platform_is_character_device(const std::filesystem::path& path) noexcept;

} // namespace vss::detail

#endif // VSS_PORTS_STABLE_LINK_PLATFORM_HPP
