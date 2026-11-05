//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "../listen_socket_platform.hpp"
#include "../posix/listen_socket_posix.hpp"

namespace vss::detail
{

error_code platform_prepare_listening_socket(std::uintptr_t native_handle)
{
    return posix_prepare_listening_socket(native_handle);
}

} // namespace vss::detail
