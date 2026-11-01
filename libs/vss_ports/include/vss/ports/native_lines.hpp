//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_PORTS_NATIVE_LINES_HPP
#define VSS_PORTS_NATIVE_LINES_HPP

#include <vss/core/control_lines.hpp>
#include <vss/core/error.hpp>

#include <cstdint>

/// Reading and driving RS-232 modem lines on a real device handle.
///
/// Windows uses `GetCommModemStatus`/`EscapeCommFunction`, POSIX uses
/// `ioctl(TIOCMGET)`/`TIOCMBIS`.  Every function returns
/// `errc::unsupported` when the platform or the device cannot do it, so
/// callers never need `#ifdef`s.
namespace vss::native {

struct modem_status
{
    control_lines lines;
    bool cts = false;
    bool dsr = false;
    bool dcd = false;
    bool ri = false;
};

/// True when this build implements the platform calls.
bool supported() noexcept;

result<modem_status> read_modem_status(std::intptr_t handle);
result<void> write_line(std::intptr_t handle, control_line line, bool state);
result<void> set_break(std::intptr_t handle, bool active);

} // namespace vss::native

#endif // VSS_PORTS_NATIVE_LINES_HPP
