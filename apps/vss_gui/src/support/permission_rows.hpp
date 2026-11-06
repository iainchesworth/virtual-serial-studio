//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#pragma once

#include "support/setup_facts.hpp"

#include <vss/ports/permissions.hpp>

#include <QString>
#include <QVector>

#include <optional>
#include <vector>

namespace vss::gui::support
{

/// What the operating system is asked about: the listeners the studio has now.
/// A listener is a port whose path is a `tcp-listen://` or `udp-listen://`
/// address.  The program is this one.
vss::permissions::context permission_context(const StudioBackend& backend);

/// The same, for one listener the user is about to open.
vss::permissions::context permission_context_for(const QString& uri);

/// Adds the rows the permission facts call for to the Setup check.  Facts for
/// another system are not in the list, so they add nothing.  The row of a fact
/// the platform could not answer says so ("Not checked yet"); nothing is
/// guessed.
void append_permission_rows(QVector<setup_row>& rows,
                            const std::vector<vss::permissions::fact>& facts);

/// A sentence for the administrator row when the account is not elevated:
/// whether Windows will ask for consent or for an administrator's password.
/// Empty when the facts say nothing.
QString elevation_sentence(const std::vector<vss::permissions::fact>& facts);

/// The Windows Firewall fact, if there is one.
std::optional<vss::permissions::fact>
firewall_fact(const std::vector<vss::permissions::fact>& facts);

} // namespace vss::gui::support
