//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

// macOS: no sign-in entry yet.  A login item needs SMAppService and a signed
// helper in the bundle, so the setting is shown disabled with "Not available
// in this build" rather than faked.

#include "support/sign_in_start.hpp"

namespace vss::gui::support
{

sign_in_location default_sign_in_location()
{
    return {};
}

sign_in_location test_sign_in_location(const QString& /*scratch*/)
{
    return {};
}

bool sign_in_start_enabled(const sign_in_location& /*location*/)
{
    return false;
}

bool set_sign_in_start(bool /*enabled*/, const sign_in_location& /*location*/)
{
    return false;
}

} // namespace vss::gui::support
