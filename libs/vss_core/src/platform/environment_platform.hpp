//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Process environment access for vss::profiling. One implementation per
// platform family in src/platform/<windows|posix>; CMake picks the file.
//

#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace vss::profiling::platform
{

/// The value of an environment variable, or nothing when it is unset.
[[nodiscard]] std::optional<std::string> get_environment(const char* name);

/// Sets an environment variable of this process, where code that reads the
/// environment later (Tracy's listener) will see it.
void set_environment(const char* name, const std::string& value);

/// Whether a probe for a free listening port should set SO_REUSEADDR the way
/// Tracy does. On Windows that option would let two listeners share a port,
/// and Tracy does not set it there.
[[nodiscard]] bool probe_reuses_address() noexcept;

} // namespace vss::profiling::platform
