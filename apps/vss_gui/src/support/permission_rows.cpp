//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "support/permission_rows.hpp"

#include <QCoreApplication>

#include <algorithm>
#include <string>
#include <string_view>

namespace vss::gui::support
{
namespace
{

namespace perm = vss::permissions;


QString port_text(const perm::fact& fact)
{
    const QString protocol = fact.protocol == perm::transport_protocol::tcp
                                 ? QStringLiteral("TCP")
                                 : QStringLiteral("UDP");
    return fact.port == 0
               ? QCoreApplication::translate("SetupCheck", "the port you choose")
               : QStringLiteral("%1 %2").arg(protocol).arg(fact.port);
}

setup_row firewall_row(const perm::fact& fact)
{
    setup_row row;
    row.icon = QStringLiteral("info");
    row.title = QCoreApplication::translate("SetupCheck", "Windows Firewall");

    if (fact.status == perm::state::unknown)
    {
        row.state = setup_state::not_checked;
        row.chip = QCoreApplication::translate("SetupCheck", "Not checked yet");
        row.sentence = QCoreApplication::translate("SetupCheck", 
            "Studio could not read the Windows Firewall rules, so it cannot "
            "say whether other computers can connect.");
        return row;
    }

    if (fact.why == perm::reason::loopback_only)
    {
        row.state = setup_state::ok;
        row.chip = QCoreApplication::translate("SetupCheck", "OK");
        row.sentence = QCoreApplication::translate("SetupCheck", 
            "Windows Firewall is not involved. Studio is not waiting for other "
            "computers to connect.");
        return row;
    }
    if (fact.why == perm::reason::firewall_off)
    {
        row.state = setup_state::ok;
        row.chip = QCoreApplication::translate("SetupCheck", "OK");
        row.sentence = QCoreApplication::translate("SetupCheck", 
            "Windows Firewall is off for the network you are on, so it does not "
            "stop other computers. Studio does not change that setting.");
        return row;
    }
    if (fact.why == perm::reason::allow_rule)
    {
        row.state = setup_state::ok;
        row.chip = QCoreApplication::translate("SetupCheck", "OK");
        row.sentence = QCoreApplication::translate("SetupCheck", "Windows Firewall has a rule that lets other "
                                "computers reach %1.")
                           .arg(port_text(fact));
        return row;
    }

    row.state = setup_state::attention;
    row.chip = QCoreApplication::translate("SetupCheck", "Needs attention");
    row.highlight = true;
    row.icon = QStringLiteral("warning");
    row.action = setup_action::firewall_help;
    if (fact.why == perm::reason::no_allow_rule)
    {
        row.sentence = QCoreApplication::translate("SetupCheck", 
                           "Windows Firewall may stop other computers "
                           "connecting to %1. Studio can add one rule for this "
                           "program and this port, after you agree.")
                           .arg(port_text(fact));
        row.action_text = fact.can_act ? QCoreApplication::translate("SetupCheck", "Allow through the firewall")
                                       : QCoreApplication::translate("SetupCheck", "How to allow it");
    }
    else if (fact.why == perm::reason::blocked_by_rule)
    {
        row.sentence = QCoreApplication::translate("SetupCheck", 
                           "A Windows Firewall rule blocks this program on %1. "
                           "Studio never removes a block rule; change it in "
                           "Windows Defender Firewall.")
                           .arg(port_text(fact));
        row.action_text = QCoreApplication::translate("SetupCheck", "How to change it");
    }
    else
    {
        row.sentence = QCoreApplication::translate("SetupCheck", 
            "Windows Firewall is set to block all incoming connections, which "
            "overrides every rule. Change that setting in Windows Security if "
            "you want other computers to connect.");
        row.action_text = QCoreApplication::translate("SetupCheck", "How to change it");
    }
    return row;
}

} // namespace

perm::context permission_context(const StudioBackend& backend)
{
    perm::context ask;
    for (const port_view& port : backend.ports())
    {
        perm::listener each;
        if (perm::parse_listen_uri(port.path.toStdString(), each))
            ask.listeners.push_back(each);
    }
    return ask;
}

perm::context permission_context_for(const QString& uri)
{
    perm::context ask;
    perm::listener each;
    if (perm::parse_listen_uri(uri.toStdString(), each))
        ask.listeners.push_back(each);
    return ask;
}

void append_permission_rows(QVector<setup_row>& rows,
                            const std::vector<perm::fact>& facts)
{
    const auto fact = firewall_fact(facts);
    if (!fact)
        return;

    // Before "Carry on without a driver", which stays the last row.
    const auto carry_on =
        std::find_if(rows.begin(), rows.end(), [](const setup_row& row)
                     { return row.action == setup_action::continue_test; });
    rows.insert(carry_on, firewall_row(*fact));
}

QString elevation_sentence(const std::vector<perm::fact>& facts)
{
    for (const perm::fact& fact : facts)
    {
        if (fact.what != perm::topic::windows_elevation)
            continue;
        if (fact.why == perm::reason::admin_not_elevated)
            return QCoreApplication::translate("SetupCheck", "Your account is an administrator, so Windows will "
                            "ask you to confirm.");
        if (fact.why == perm::reason::standard_user)
            return QCoreApplication::translate("SetupCheck", "Your account is not an administrator, so Windows "
                            "will ask for an administrator's password.");
    }
    return {};
}

std::optional<perm::fact> firewall_fact(const std::vector<perm::fact>& facts)
{
    for (const perm::fact& fact : facts)
    {
        if (fact.what == perm::topic::windows_firewall)
            return fact;
    }
    return std::nullopt;
}

} // namespace vss::gui::support
