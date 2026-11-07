//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// vss::profiling in a build without Tracy: nothing to start or stop. Compiled
// when VSS_TRACY is off.
//

#include <vss/core/profiling.hpp>

namespace vss::profiling
{

status current() noexcept
{
    status out;
    out.current = state::not_compiled;
    return out;
}

result<status> enable(const options&)
{
    return make_error_code(errc::unsupported);
}

void disable() noexcept {}

void shutdown() noexcept {}

} // namespace vss::profiling
