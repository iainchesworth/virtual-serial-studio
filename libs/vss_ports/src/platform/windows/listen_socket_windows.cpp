//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "../listen_socket_platform.hpp"

#include <winsock2.h>

namespace vss::detail
{

error_code platform_prepare_listening_socket(std::uintptr_t native_handle)
{
    const int on = 1;
    if (::setsockopt(static_cast<SOCKET>(native_handle), SOL_SOCKET,
                     SO_EXCLUSIVEADDRUSE,
                     reinterpret_cast<const char*>(&on), sizeof(on)) != 0)
    {
        return error_code(::WSAGetLastError(),
                          boost::system::system_category());
    }
    return {};
}

} // namespace vss::detail
