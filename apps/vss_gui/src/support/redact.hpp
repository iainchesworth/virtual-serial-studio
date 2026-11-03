//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_SUPPORT_REDACT_HPP
#define VSS_GUI_SUPPORT_REDACT_HPP

#include <QString>

namespace vss::gui::support
{

/// Removes credentials from text that leaves the process (a diagnostic
/// bundle).  It covers URLs with a user name and password, the common
/// `password=`, `token:`, `secret=` and `api_key=` shapes, bearer tokens, and
/// the current user's home directory.
QString redact_secrets(QString text);

} // namespace vss::gui::support

#endif // VSS_GUI_SUPPORT_REDACT_HPP
