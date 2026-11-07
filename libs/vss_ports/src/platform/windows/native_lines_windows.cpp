//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "../native_lines_platform.hpp"

#include <windows.h>

namespace vss::detail
{
namespace
{

error_code last_error()
{
    return error_code(static_cast<int>(::GetLastError()),
                      boost::system::system_category());
}

/// The portable interface carries an operating-system handle as an integer.
HANDLE as_handle(std::intptr_t handle) noexcept
{
    // The integer is a HANDLE that the caller got from the operating system.
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    return reinterpret_cast<HANDLE>(handle);
}

} // namespace

result<native::modem_status> platform_read_modem_status(std::intptr_t handle)
{
    native::modem_status out;

    DWORD status = 0;
    if (!::GetCommModemStatus(as_handle(handle), &status))
        return last_error();

    out.cts = (status & MS_CTS_ON) != 0;
    out.dsr = (status & MS_DSR_ON) != 0;
    out.dcd = (status & MS_RLSD_ON) != 0;
    out.ri = (status & MS_RING_ON) != 0;
    return out;
}

result<void> platform_write_line(std::intptr_t handle, control_line line,
                                 bool state)
{
    DWORD function = 0;
    switch (line)
    {
    case control_line::dtr:
        function = state ? SETDTR : CLRDTR;
        break;
    case control_line::rts:
        function = state ? SETRTS : CLRRTS;
        break;
    case control_line::break_signal:
        function = state ? SETBREAK : CLRBREAK;
        break;
    default:
        // CTS/DSR/DCD/RI are driven by the device, never by us.
        return make_error_code(errc::unsupported);
    }
    if (!::EscapeCommFunction(as_handle(handle), function))
        return last_error();
    return {};
}

result<void> platform_set_break(std::intptr_t handle, bool active)
{
    if (!::EscapeCommFunction(as_handle(handle),
                              active ? SETBREAK : CLRBREAK))
        return last_error();
    return {};
}

} // namespace vss::detail
