//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <vss/ports/process_control.hpp>

namespace vss
{

// Only a driver port names the process that has it open, and the driver exists
// only on Windows.

std::optional<process_summary> describe_process(std::uint32_t)
{
    return std::nullopt;
}

result<void> terminate_process(std::uint32_t)
{
    return make_error_code(errc::unsupported);
}

} // namespace vss
