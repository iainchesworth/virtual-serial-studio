//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_BACKEND_COM0COM_PLATFORM_HPP
#define VSS_GUI_BACKEND_COM0COM_PLATFORM_HPP

#include <vss/core/error.hpp>
#include <vss/studio/studio.hpp>

namespace vss::gui::detail
{

/// Implemented once for each operating system in `platform/<os>/`.
/// True where the operating system can create a com0com pair.
bool platform_supports_com0com_pair_creation() noexcept;

/// Creates another com0com pair on the first two COM numbers above those in
/// use.  Fails with `errc::unsupported` where com0com cannot exist or is not
/// installed.
vss::result<void> platform_create_com0com_pair(vss::studio& studio);

} // namespace vss::gui::detail

#endif // VSS_GUI_BACKEND_COM0COM_PLATFORM_HPP
