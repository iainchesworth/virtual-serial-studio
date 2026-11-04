//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_PORTS_POSIX_PRIVATE_DIRECTORY_HPP
#define VSS_PORTS_POSIX_PRIVATE_DIRECTORY_HPP

// Shared by the Linux and macOS files in `src/platform/`.  Both systems answer
// these questions the same way, so the code is written once and included.

#include <vss/core/error.hpp>

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace vss::detail::posix
{

inline error_code
check_private_directory(const std::filesystem::path& directory)
{
    const std::string text = directory.string();

    // lstat, not stat: a link in place of the directory is refused.
    struct stat info{};
    if (::lstat(text.c_str(), &info) != 0)
        return error_code(errno, boost::system::system_category());
    if (!S_ISDIR(info.st_mode))
        return make_error_code(errc::permission_denied);
    if (info.st_uid != ::geteuid())
        return make_error_code(errc::permission_denied);
    if ((info.st_mode & (S_IRWXG | S_IRWXO)) != 0)
        return make_error_code(errc::permission_denied);
    return {};
}

inline error_code
ensure_private_directory(const std::filesystem::path& directory)
{
    const std::string text = directory.string();

    // Make each missing parent with the default mode, and the last component
    // closed to everyone but the owner.
    std::error_code ignored;
    if (directory.has_parent_path())
        std::filesystem::create_directories(directory.parent_path(), ignored);

    if (::mkdir(text.c_str(), S_IRWXU) != 0 && errno != EEXIST)
        return error_code(errno, boost::system::system_category());

    return check_private_directory(directory);
}

inline std::optional<std::string> getenv_value(std::string_view name)
{
    const std::string key(name);
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    if (const char* value = std::getenv(key.c_str()))
        return std::string(value);
    return std::nullopt;
}

inline bool is_character_device(const std::filesystem::path& path) noexcept
{
    struct stat info{};
    return ::stat(path.c_str(), &info) == 0 && S_ISCHR(info.st_mode);
}

} // namespace vss::detail::posix

#endif // VSS_PORTS_POSIX_PRIVATE_DIRECTORY_HPP
