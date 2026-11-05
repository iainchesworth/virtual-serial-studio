//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "../environment_platform.hpp"

#include <cstddef>
#include <cstdlib>

namespace vss::testkit::detail
{

std::string platform_environment(const char* name)
{
    char* buffer = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&buffer, &size, name) != 0 || buffer == nullptr)
        return {};
    std::string value(buffer);
    std::free(buffer);
    return value;
}

} // namespace vss::testkit::detail
