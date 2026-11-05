//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_PORTS_LISTEN_SOCKET_POSIX_HPP
#define VSS_PORTS_LISTEN_SOCKET_POSIX_HPP

#include <vss/core/error.hpp>

#include <cerrno>
#include <cstdint>
#include <sys/socket.h>

namespace vss::detail
{

/// Shared by the Linux and macOS files.
inline error_code posix_prepare_listening_socket(std::uintptr_t native_handle)
{
    const int on = 1;
    if (::setsockopt(static_cast<int>(native_handle), SOL_SOCKET, SO_REUSEADDR,
                     &on, sizeof(on)) != 0)
    {
        return error_code(errno, boost::system::system_category());
    }
    return {};
}

} // namespace vss::detail

#endif // VSS_PORTS_LISTEN_SOCKET_POSIX_HPP
