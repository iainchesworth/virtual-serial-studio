//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#pragma once

#include <vss/ports/permissions.hpp>

#include <QString>

namespace vss::gui::support
{

/// What came of a step that needed administrator rights.
struct step_report
{
    vss::permissions::step_status status =
        vss::permissions::step_status::failed;
    /// A sentence for the user, translated.
    QString message;

    bool done() const noexcept
    {
        return status == vss::permissions::step_status::done;
    }
};

/// The command-line helper, which sits beside this program and is the only
/// thing started elevated.  Empty when it is not there.
QString helper_path();

/// True when this build can make the firewall rule: the platform has the step
/// and the helper is in place.  The Network endpoint dialog and the Setup check
/// show "Not available in this build" otherwise.
bool firewall_step_available();

/// Starts the helper through the operating system's own elevation prompt to add
/// the one rule of `spec`, and waits.  Call it only after the user has seen the
/// rule and agreed; nothing here asks again.
step_report
allow_through_firewall(const vss::permissions::firewall_rule_spec& spec);

/// The same, to remove the rule again.
step_report
remove_firewall_rule(const vss::permissions::firewall_rule_spec& spec);

} // namespace vss::gui::support
