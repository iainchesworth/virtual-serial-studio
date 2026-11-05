//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "../environment_platform.hpp"

#include <cstdlib>

namespace vss::testkit::detail
{

std::string platform_environment(const char* name)
{
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
}

} // namespace vss::testkit::detail
