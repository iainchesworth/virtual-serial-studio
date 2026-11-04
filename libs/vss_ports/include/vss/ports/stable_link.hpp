//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_PORTS_STABLE_LINK_HPP
#define VSS_PORTS_STABLE_LINK_HPP

#include <vss/core/error.hpp>

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace vss
{

/// A name an operator can put in a script or a configuration file.
///
/// A pseudo terminal is `/dev/pts/N` and N changes on every run.  A stable
/// link is a symbolic link in a private directory that points at the current
/// slave path, so the name stays the same while the target moves.
struct stable_link
{
    /// Directory plus name: what applications open.
    std::string path;
    /// The pseudo terminal slave path the link points at.
    std::string target;

    friend bool operator==(const stable_link&, const stable_link&) = default;
};

/// Looks up an environment variable.  Tests pass their own.
using env_lookup = std::function<std::optional<std::string>(std::string_view)>;

/// The lookup that reads the process environment.
env_lookup process_environment();

/// True when this platform can make stable links (Linux and macOS).
bool stable_links_supported() noexcept;

/// The directory links are made in.
///
/// In order: `$VSS_VPORT_DIR`; `$XDG_RUNTIME_DIR/vss`; then the platform's own
/// fallbacks (`/run/vss` on Linux, then `/tmp/vss`; `/tmp/vss` on macOS).  The
/// directory is created with mode 0700 and must belong to the caller and be
/// closed to everyone else; a directory that is not is refused rather than
/// used.  A directory named by an environment variable has no fallback.
result<std::filesystem::path> resolve_link_directory(const env_lookup& lookup);

/// Makes `name` in `directory` point at `target`.
///
/// `name` is one path component of `[A-Za-z0-9._-]`, not `.` or `..`, at most
/// 64 characters.  An existing link to the same target is left as it is.  A
/// link whose target no longer exists, or is no longer a character device, is
/// stale and is replaced; a link to a live device and anything that is not a
/// link give `errc::already_exists`.
result<stable_link> make_stable_link(const std::filesystem::path& directory,
                                     std::string_view name,
                                     std::string_view target);

/// Removes the link, but only when it still points at `link.target`, so a
/// link an operator has replaced is left alone.
void remove_stable_link(const stable_link& link) noexcept;

} // namespace vss

#endif // VSS_PORTS_STABLE_LINK_HPP
