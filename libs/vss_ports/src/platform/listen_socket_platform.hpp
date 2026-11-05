//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#pragma once

#include <vss/core/error.hpp>

#include <cstdint>

namespace vss::detail
{

/// Applies the operating system's rule for reusing an address to a listening
/// TCP socket that has been created and not yet bound. On POSIX that is
/// SO_REUSEADDR, so a listener can start again at once after one that had
/// clients. On Windows SO_REUSEADDR would let another program take the same
/// port, so the socket is made exclusive instead. Implemented once for each
/// operating system in `src/platform/<os>/`.
error_code platform_prepare_listening_socket(std::uintptr_t native_handle);

} // namespace vss::detail
