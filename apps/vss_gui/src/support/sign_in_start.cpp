//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "support/sign_in_start.hpp"

namespace vss::gui::support
{

// default_sign_in_location, sign_in_start_enabled, set_sign_in_start and
// test_sign_in_location are in platform/<platform>/sign_in_start_*.cpp.

bool sign_in_start_available()
{
    return !default_sign_in_location().where.isEmpty();
}

QString sign_in_command(const QString& program)
{
    return QStringLiteral("\"%1\" --background").arg(program);
}

} // namespace vss::gui::support
