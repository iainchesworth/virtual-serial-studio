//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <vss/ports/stable_link.hpp>

#include "platform/stable_link_platform.hpp"

#include <cstdlib>
#include <system_error>

namespace vss
{
namespace
{

constexpr std::size_t max_name_length = 64;

bool valid_name(std::string_view name) noexcept
{
    if (name.empty() || name.size() > max_name_length || name == "." ||
        name == "..")
        return false;
    for (const char c : name)
    {
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        const bool digit = c >= '0' && c <= '9';
        if (!letter && !digit && c != '.' && c != '_' && c != '-')
            return false;
    }
    return true;
}

error_code from_std(const std::error_code& ec)
{
    return error_code(ec.value(), boost::system::system_category());
}

std::optional<std::string> non_empty(const env_lookup& lookup,
                                     std::string_view name)
{
    auto value = lookup(name);
    if (value && value->empty())
        return std::nullopt;
    return value;
}

} // namespace

env_lookup process_environment()
{
    return [](std::string_view name) -> std::optional<std::string>
    {
        // std::getenv wants a terminated string.
        const std::string key(name);
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        if (const char* value = std::getenv(key.c_str()))
            return std::string(value);
        return std::nullopt;
    };
}

bool stable_links_supported() noexcept
{
    return detail::platform_supports_stable_links();
}

result<std::filesystem::path> resolve_link_directory(const env_lookup& lookup)
{
    if (!stable_links_supported())
        return make_error_code(errc::unsupported);

    if (const auto chosen = non_empty(lookup, "VSS_VPORT_DIR"))
    {
        const std::filesystem::path directory(*chosen);
        if (const auto ec =
                detail::platform_ensure_private_directory(directory))
            return ec;
        return directory;
    }

    if (const auto runtime = non_empty(lookup, "XDG_RUNTIME_DIR"))
    {
        const std::filesystem::path directory =
            std::filesystem::path(*runtime) / "vss";
        if (const auto ec =
                detail::platform_ensure_private_directory(directory))
            return ec;
        return directory;
    }

    error_code last = make_error_code(errc::not_found);
    for (const auto& directory : detail::platform_link_directories())
    {
        const auto ec = detail::platform_ensure_private_directory(directory);
        if (!ec)
            return directory;
        last = ec;
    }
    return last;
}

result<stable_link> make_stable_link(const std::filesystem::path& directory,
                                     std::string_view name,
                                     std::string_view target)
{
    if (!stable_links_supported())
        return make_error_code(errc::unsupported);
    if (!valid_name(name) || target.empty())
        return make_error_code(errc::invalid_argument);

    if (const auto ec = detail::platform_ensure_private_directory(directory))
        return ec;

    const std::filesystem::path path = directory / std::string(name);
    const std::filesystem::path wanted{std::string(target)};
    stable_link link{path.string(), std::string(target)};

    std::error_code ec;
    const auto status = std::filesystem::symlink_status(path, ec);
    if (status.type() == std::filesystem::file_type::not_found)
    {
        // Free: fall through to the creation below.
    }
    else if (ec)
    {
        return from_std(ec);
    }
    else if (status.type() == std::filesystem::file_type::symlink)
    {
        const auto current = std::filesystem::read_symlink(path, ec);
        if (ec)
            return from_std(ec);
        if (current == wanted)
            return link;

        // A link to a device that is still there belongs to someone running.
        // A link to nothing, or to something that is not a device, is the
        // remains of a process that did not clean up.
        if (detail::platform_is_character_device(path))
            return make_error_code(errc::already_exists);
        std::filesystem::remove(path, ec);
        if (ec)
            return from_std(ec);
    }
    else
    {
        return make_error_code(errc::already_exists);
    }

    std::filesystem::create_symlink(wanted, path, ec);
    if (ec == std::errc::file_exists)
        return make_error_code(errc::already_exists);
    if (ec)
        return from_std(ec);
    return link;
}

void remove_stable_link(const stable_link& link) noexcept
{
    std::error_code ec;
    const std::filesystem::path path(link.path);
    const auto status = std::filesystem::symlink_status(path, ec);
    if (ec || status.type() != std::filesystem::file_type::symlink)
        return;
    const auto current = std::filesystem::read_symlink(path, ec);
    if (ec || current != std::filesystem::path(link.target))
        return;
    std::filesystem::remove(path, ec);
}

} // namespace vss
