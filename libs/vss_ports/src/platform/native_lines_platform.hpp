//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_PORTS_NATIVE_LINES_PLATFORM_HPP
#define VSS_PORTS_NATIVE_LINES_PLATFORM_HPP

#include <vss/ports/native_lines.hpp>

#include <cstdint>

namespace vss::detail
{

/// Reads CTS, DSR, DCD and RI from the device. `handle` is not zero. Only
/// the four bits are set; the caller fills in the line set.
result<native::modem_status> platform_read_modem_status(std::intptr_t handle);

/// Drives one line. `handle` is not zero.
result<void> platform_write_line(std::intptr_t handle, control_line line,
                                 bool state);

/// Starts or ends a break condition. `handle` is not zero.
result<void> platform_set_break(std::intptr_t handle, bool active);

} // namespace vss::detail

#endif // VSS_PORTS_NATIVE_LINES_PLATFORM_HPP
