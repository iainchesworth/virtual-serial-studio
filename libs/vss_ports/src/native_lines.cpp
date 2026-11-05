//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <vss/ports/native_lines.hpp>

#include "platform/native_lines_platform.hpp"

namespace vss::native
{

bool supported() noexcept
{
    return true;
}

result<modem_status> read_modem_status(std::intptr_t handle)
{
    if (handle == 0)
        return make_error_code(errc::not_open);

    auto out = detail::platform_read_modem_status(handle);
    if (!out)
        return out;

    out->lines.set(control_line::cts, out->cts);
    out->lines.set(control_line::dsr, out->dsr);
    out->lines.set(control_line::dcd, out->dcd);
    out->lines.set(control_line::ri, out->ri);
    return out;
}

result<void> write_line(std::intptr_t handle, control_line line, bool state)
{
    if (handle == 0)
        return make_error_code(errc::not_open);

    return detail::platform_write_line(handle, line, state);
}

result<void> set_break(std::intptr_t handle, bool active)
{
    if (handle == 0)
        return make_error_code(errc::not_open);

    return detail::platform_set_break(handle, active);
}

} // namespace vss::native
