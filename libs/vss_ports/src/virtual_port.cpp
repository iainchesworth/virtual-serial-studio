//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <vss/ports/loopback_transport.hpp>
#include <vss/ports/vcom_driver.hpp>
#include <vss/ports/virtual_port.hpp>

#include <boost/asio/post.hpp>

#include "platform/virtual_port_platform.hpp"

#include <utility>

namespace vss
{

bool supports(virtual_port_kind kind) noexcept
{
    switch (kind)
    {
    case virtual_port_kind::pty:
        return detail::platform_supports_pty();
    case virtual_port_kind::windows_driver:
        return driver_available();
    case virtual_port_kind::named_pipe:
        // Named pipe virtual ports are not implemented yet; com0com covers
        // the real COM port case on Windows.
        return false;
    case virtual_port_kind::loopback:
        return true;
    }
    return false;
}

std::vector<virtual_port_kind> available_kinds()
{
    std::vector<virtual_port_kind> kinds;
    if (supports(virtual_port_kind::windows_driver))
        kinds.push_back(virtual_port_kind::windows_driver);
    if (supports(virtual_port_kind::pty))
        kinds.push_back(virtual_port_kind::pty);
    if (supports(virtual_port_kind::named_pipe))
        kinds.push_back(virtual_port_kind::named_pipe);
    kinds.push_back(virtual_port_kind::loopback);
    return kinds;
}

const char* to_string(virtual_port_kind kind) noexcept
{
    switch (kind)
    {
    case virtual_port_kind::pty:
        return "pty";
    case virtual_port_kind::windows_driver:
        return "windows-driver";
    case virtual_port_kind::named_pipe:
        return "named-pipe";
    case virtual_port_kind::loopback:
        return "loopback";
    }
    return "unknown";
}

result<virtual_port_result>
create_virtual_port(const transport::executor_type& executor,
                    virtual_port_kind kind, const std::string& name,
                    unsigned com_number)
{
    switch (kind)
    {
    case virtual_port_kind::loopback:
    {
        auto port = loopback_transport::create(executor, name);
        virtual_port_result out;
        out.path = "loopback:" + name;
        out.kind = virtual_port_kind::loopback;
        out.port = std::move(port);
        return out;
    }
    case virtual_port_kind::pty:
        return detail::platform_create_pty(executor, name);
    case virtual_port_kind::windows_driver:
        return create_driver_port(executor, name, com_number);
    case virtual_port_kind::named_pipe:
        return make_error_code(errc::unsupported);
    }
    return make_error_code(errc::invalid_argument);
}

result<virtual_port_result>
create_best_virtual_port(const transport::executor_type& executor,
                         const std::string& name, unsigned com_number)
{
    if (supports(virtual_port_kind::windows_driver))
        return create_virtual_port(executor, virtual_port_kind::windows_driver,
                                   name, com_number);
    if (supports(virtual_port_kind::pty))
        return create_virtual_port(executor, virtual_port_kind::pty, name);
    return create_virtual_port(executor, virtual_port_kind::loopback, name);
}

} // namespace vss
