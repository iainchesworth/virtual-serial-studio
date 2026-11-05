//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "../native_lines_platform.hpp"

#include <cerrno>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

namespace vss::detail
{
namespace
{

error_code last_error()
{
    return error_code(errno, boost::system::system_category());
}

} // namespace

result<native::modem_status> platform_read_modem_status(std::intptr_t handle)
{
    native::modem_status out;

    int status = 0;
    if (::ioctl(static_cast<int>(handle), TIOCMGET, &status) != 0)
        return make_error_code(errc::unsupported);

    out.cts = (status & TIOCM_CTS) != 0;
    out.dsr = (status & TIOCM_DSR) != 0;
    out.dcd = (status & TIOCM_CAR) != 0;
    out.ri = (status & TIOCM_RNG) != 0;
    return out;
}

result<void> platform_set_break(std::intptr_t handle, bool active)
{
    if (::ioctl(static_cast<int>(handle), active ? TIOCSBRK : TIOCCBRK) != 0)
        return last_error();
    return {};
}

result<void> platform_write_line(std::intptr_t handle, control_line line,
                                 bool state)
{
    int bit = 0;
    switch (line)
    {
    case control_line::dtr:
        bit = TIOCM_DTR;
        break;
    case control_line::rts:
        bit = TIOCM_RTS;
        break;
    case control_line::dcd:
        bit = TIOCM_CAR;
        break;
    case control_line::ri:
        bit = TIOCM_RNG;
        break;
    case control_line::dsr:
        bit = TIOCM_DSR;
        break;
    case control_line::cts:
        bit = TIOCM_CTS;
        break;
    case control_line::break_signal:
        return platform_set_break(handle, state);
    default:
        return make_error_code(errc::unsupported);
    }

    const auto request =
        static_cast<unsigned long>(state ? TIOCMBIS : TIOCMBIC);
    if (::ioctl(static_cast<int>(handle), request, &bit) != 0)
        return last_error();
    return {};
}

} // namespace vss::detail
