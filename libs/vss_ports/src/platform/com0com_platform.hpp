//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_PORTS_COM0COM_PLATFORM_HPP
#define VSS_PORTS_COM0COM_PLATFORM_HPP

#include <vss/ports/com0com.hpp>

#include <string_view>

namespace vss::detail
{

/// Implemented once for each operating system in `src/platform/<os>/`.
/// The public functions in com0com.hpp forward to these.
com0com_installation platform_detect_com0com();

result<void>
platform_create_com0com_pair(const com0com_installation& installation,
                             std::string_view port_a, std::string_view port_b,
                             bool request_elevation);

result<void>
platform_remove_com0com_pair(const com0com_installation& installation,
                             int index, bool request_elevation);

} // namespace vss::detail

#endif // VSS_PORTS_COM0COM_PLATFORM_HPP
