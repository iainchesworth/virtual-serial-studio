//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_TESTKIT_ENVIRONMENT_PLATFORM_HPP
#define VSS_TESTKIT_ENVIRONMENT_PLATFORM_HPP

#include <string>

namespace vss::testkit::detail
{

/// Implemented once for each operating system in `src/platform/`.
/// The value of an environment variable, or an empty string when it is unset.
std::string platform_environment(const char* name);

} // namespace vss::testkit::detail

#endif // VSS_TESTKIT_ENVIRONMENT_PLATFORM_HPP
