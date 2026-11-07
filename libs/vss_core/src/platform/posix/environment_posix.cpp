//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "platform/environment_platform.hpp"

#include <cstdlib>

namespace vss::profiling::platform
{

std::optional<std::string> get_environment(const char* name)
{
    const char* value = std::getenv(name);
    if (value == nullptr)
    {
        return std::nullopt;
    }
    return std::string(value);
}

void set_environment(const char* name, const std::string& value)
{
    setenv(name, value.c_str(), 1);
}

bool probe_reuses_address() noexcept
{
    return true;
}

} // namespace vss::profiling::platform
