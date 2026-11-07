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
    char* buffer = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&buffer, &size, name) != 0 || buffer == nullptr)
    {
        return std::nullopt;
    }
    std::string value(buffer);
    std::free(buffer);
    return value;
}

void set_environment(const char* name, const std::string& value)
{
    // Updates the C runtime's copy and the process environment block, which is
    // what Tracy reads on Windows.
    _putenv_s(name, value.c_str());
}

bool probe_reuses_address() noexcept
{
    return false;
}

} // namespace vss::profiling::platform
