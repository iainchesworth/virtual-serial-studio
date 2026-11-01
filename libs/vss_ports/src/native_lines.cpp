//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <vss/ports/native_lines.hpp>

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <cerrno>
#  include <termios.h>
#  include <sys/ioctl.h>
#  include <unistd.h>
#endif

namespace vss::native {
namespace {

error_code last_error()
{
#if defined(_WIN32)
    return error_code(static_cast<int>(::GetLastError()),
                      boost::system::system_category());
#else
    return error_code(errno, boost::system::system_category());
#endif
}

} // namespace

bool supported() noexcept
{
    return true;
}

result<modem_status> read_modem_status(std::intptr_t handle)
{
    if (handle == 0)
        return make_error_code(errc::not_open);

    modem_status out;

#if defined(_WIN32)
    DWORD status = 0;
    if (!::GetCommModemStatus(reinterpret_cast<HANDLE>(handle), &status))
        return last_error();

    out.cts = (status & MS_CTS_ON) != 0;
    out.dsr = (status & MS_DSR_ON) != 0;
    out.dcd = (status & MS_RLSD_ON) != 0;
    out.ri = (status & MS_RING_ON) != 0;
#else
    int status = 0;
    if (::ioctl(static_cast<int>(handle), TIOCMGET, &status) != 0)
        return make_error_code(errc::unsupported);

    out.cts = (status & TIOCM_CTS) != 0;
    out.dsr = (status & TIOCM_DSR) != 0;
    out.dcd = (status & TIOCM_CAR) != 0;
    out.ri = (status & TIOCM_RNG) != 0;
#endif

    out.lines.set(control_line::cts, out.cts);
    out.lines.set(control_line::dsr, out.dsr);
    out.lines.set(control_line::dcd, out.dcd);
    out.lines.set(control_line::ri, out.ri);
    return out;
}

result<void> write_line(std::intptr_t handle, control_line line, bool state)
{
    if (handle == 0)
        return make_error_code(errc::not_open);

#if defined(_WIN32)
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
    if (!::EscapeCommFunction(reinterpret_cast<HANDLE>(handle), function))
        return last_error();
    return {};
#else
    int bit = 0;
    switch (line)
    {
    case control_line::dtr: bit = TIOCM_DTR; break;
    case control_line::rts: bit = TIOCM_RTS; break;
    case control_line::dcd: bit = TIOCM_CAR; break;
    case control_line::ri: bit = TIOCM_RNG; break;
    case control_line::dsr: bit = TIOCM_DSR; break;
    case control_line::cts: bit = TIOCM_CTS; break;
    case control_line::break_signal:
        return set_break(handle, state);
    default:
        return make_error_code(errc::unsupported);
    }

    const int request = state ? TIOCMBIS : TIOCMBIC;
    if (::ioctl(static_cast<int>(handle), request, &bit) != 0)
        return last_error();
    return {};
#endif
}

result<void> set_break(std::intptr_t handle, bool active)
{
    if (handle == 0)
        return make_error_code(errc::not_open);

#if defined(_WIN32)
    if (!::EscapeCommFunction(reinterpret_cast<HANDLE>(handle),
                              active ? SETBREAK : CLRBREAK))
        return last_error();
    return {};
#else
    if (::ioctl(static_cast<int>(handle), active ? TIOCSBRK : TIOCCBRK) != 0)
        return last_error();
    return {};
#endif
}

} // namespace vss::native
