//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Linux rows of the Setup check and the dialog behind them. The facts are
// injected, so every state is checked on every system, and nothing here ever
// starts polkit.
//

#include "test_framework.hpp"

#include "support/permission_dialog.hpp"
#include "support/permission_rows.hpp"
#include "support/setup_facts.hpp"
#include "widgets/notes.hpp"

#include <QPushButton>

#include <string>
#include <string_view>
#include <vector>

namespace
{

namespace support = vss::gui::support;
namespace perm = vss::permissions;

perm::fact serial(std::string_view why, perm::state status,
                  bool with_step = false)
{
    perm::fact fact;
    fact.what = perm::topic::serial_access;
    fact.status = status;
    fact.why = std::string(why);
    fact.subject = "/dev/ttyUSB0";
    fact.remedy = "sudo usermod -aG dialout alice";
    if (with_step)
    {
        perm::group_membership_spec spec;
        spec.user = "alice";
        spec.group = "dialout";
        fact.group_step = spec;
        fact.can_act = true;
    }
    return fact;
}

perm::fact confinement(std::string_view why, perm::state status)
{
    perm::fact fact;
    fact.what = perm::topic::confinement;
    fact.status = status;
    fact.why = std::string(why);
    fact.subject = "/usr/bin/vss_gui";
    fact.remedy = "# profile text";
    return fact;
}

perm::fact linux_firewall(std::string_view why, perm::state status)
{
    perm::fact fact;
    fact.what = perm::topic::linux_firewall;
    fact.status = status;
    fact.why = std::string(why);
    fact.port = 4001;
    fact.remedy = "sudo ufw allow 4001/tcp";
    return fact;
}

support::setup_report report_with(std::vector<perm::fact> facts)
{
    vss::gui::setup_facts engine;
    engine.platform = QStringLiteral("linux");
    engine.serial_devices = 1;
    support::platform_facts platform;
    platform.pty_known = true;
    platform.pty_available = true;
    platform.permissions = std::move(facts);
    return support::build_setup_report(engine, platform,
                                       {QStringLiteral("/dev/ttyUSB0")});
}

const support::setup_row* find_row(const support::setup_report& report,
                                   const QString& title)
{
    for (const support::setup_row& row : report.rows)
    {
        if (row.title == title)
            return &row;
    }
    return nullptr;
}

} // namespace

TEST_CASE("linux rows: serial device access says what is wrong and offers the "
          "smallest step")
{
    const QString title = QStringLiteral("Serial device access");

    auto report =
        report_with({serial(perm::reason::devices_open, perm::state::ok)});
    REQUIRE(find_row(report, title)->state == support::setup_state::ok);

    // No devices listed: no row, because there is nothing to say.
    report = report_with({serial(perm::reason::no_devices, perm::state::info)});
    REQUIRE(find_row(report, title) == nullptr);

    report = report_with(
        {serial(perm::reason::not_in_group, perm::state::attention, true)});
    const auto* row = find_row(report, title);
    REQUIRE(row->state == support::setup_state::attention);
    REQUIRE(row->action == support::setup_action::serial_access_help);
    REQUIRE(row->sentence.contains(QStringLiteral("/dev/ttyUSB0")));
    REQUIRE_EQ(row->action_text, QStringLiteral("Add me to the group"));

    // Without the step in this build the row only explains.
    report = report_with(
        {serial(perm::reason::not_in_group, perm::state::attention)});
    REQUIRE_EQ(find_row(report, title)->action_text,
               QStringLiteral("How to add me"));

    report = report_with(
        {serial(perm::reason::relogin_needed, perm::state::attention)});
    REQUIRE(find_row(report, title)
                ->sentence.contains(QStringLiteral("Sign out and back in")));

    report = report_with(
        {serial(perm::reason::denied_by_policy, perm::state::attention)});
    REQUIRE_EQ(find_row(report, title)->action_text,
               QStringLiteral("Show the fix"));

    report =
        report_with({serial(perm::reason::not_asked, perm::state::unknown)});
    REQUIRE(find_row(report, title)->state ==
            support::setup_state::not_checked);
}

TEST_CASE("linux rows: confinement is information and only shown when it "
          "applies")
{
    const QString title = QStringLiteral("Confinement");
    auto report =
        report_with({confinement(perm::reason::unconfined, perm::state::ok)});
    REQUIRE(find_row(report, title) == nullptr);

    for (const std::string_view why :
         {perm::reason::flatpak, perm::reason::snap,
          perm::reason::apparmor_profile, perm::reason::selinux_enforcing})
    {
        report = report_with({confinement(why, perm::state::info)});
        const auto* row = find_row(report, title);
        REQUIRE(row != nullptr);
        REQUIRE(row->state == support::setup_state::info);
        REQUIRE(row->action == support::setup_action::confinement_help);
        // Information is not a problem: the header does not count it.
        REQUIRE(!report.header_chip.contains(QStringLiteral("fix")));
    }
}

TEST_CASE("linux rows: the firewall row appears only for a listener other "
          "computers can reach")
{
    const QString title = QStringLiteral("Firewall");
    auto report = report_with(
        {linux_firewall(perm::reason::loopback_only, perm::state::ok)});
    REQUIRE(find_row(report, title) == nullptr);

    report = report_with(
        {linux_firewall(perm::reason::ufw_active, perm::state::info)});
    const auto* row = find_row(report, title);
    REQUIRE(row->state == support::setup_state::info);
    REQUIRE(row->sentence.contains(QStringLiteral("TCP 4001")));

    report = report_with(
        {linux_firewall(perm::reason::rules_need_root, perm::state::unknown)});
    row = find_row(report, title);
    REQUIRE(row->state == support::setup_state::not_checked);
    REQUIRE_EQ(row->chip, QStringLiteral("Not checked yet"));
}

TEST_CASE("permission dialog: the group step is explained and never runs "
          "unasked")
{
    const auto fact =
        serial(perm::reason::not_in_group, perm::state::attention, true);

    support::PermissionDialog able(fact, true);
    REQUIRE(able.step_button() != nullptr);
    REQUIRE(able.step_button()->isEnabled());
    REQUIRE(!able.step_button()->isDefault());
    REQUIRE_EQ(able.step_button()->text(),
               QStringLiteral("Add me to the dialout group"));
    REQUIRE(able.step_button()->toolTip().contains(
        QStringLiteral("ask for your password")));
    REQUIRE(!able.step_done());
    REQUIRE(!able.result_note()->isVisibleTo(&able));

    // Not available here: the button is off and says so, and a call does
    // nothing.
    support::PermissionDialog explain(fact, false);
    REQUIRE(!explain.step_button()->isEnabled());
    REQUIRE_EQ(explain.step_button()->toolTip(),
               QStringLiteral("Not available in this build"));
    explain.run_step();
    REQUIRE(!explain.step_done());

    // No step in the fact: no button at all.
    support::PermissionDialog plain(
        confinement(perm::reason::apparmor_profile, perm::state::info), true);
    REQUIRE(plain.step_button() == nullptr);
}

namespace
{

perm::fact mac_fact(perm::topic what, std::string_view why, perm::state status)
{
    perm::fact fact;
    fact.what = what;
    fact.status = status;
    fact.why = std::string(why);
    fact.port = 4001;
    fact.subject = "/Applications/vss_gui.app/Contents/MacOS/vss_gui";
    fact.remedy =
        "sudo /usr/libexec/ApplicationFirewall/socketfilterfw --add x";
    return fact;
}

support::setup_report mac_report_with(std::vector<perm::fact> facts)
{
    vss::gui::setup_facts engine;
    engine.platform = QStringLiteral("macos");
    support::platform_facts platform;
    platform.pty_known = true;
    platform.pty_available = true;
    platform.permissions = std::move(facts);
    return support::build_setup_report(engine, platform, {});
}

} // namespace

TEST_CASE("macos rows: the firewall row says what macOS will do and never "
          "offers to change it")
{
    const QString title = QStringLiteral("macOS firewall");

    auto report = mac_report_with(
        {mac_fact(perm::topic::macos_firewall, perm::reason::loopback_only,
                  perm::state::ok)});
    REQUIRE(find_row(report, title) == nullptr);

    report = mac_report_with(
        {mac_fact(perm::topic::macos_firewall, perm::reason::firewall_off,
                  perm::state::ok)});
    REQUIRE(find_row(report, title)->state == support::setup_state::ok);

    report = mac_report_with(
        {mac_fact(perm::topic::macos_firewall, perm::reason::alf_prompt,
                  perm::state::info)});
    const auto* row = find_row(report, title);
    REQUIRE(row->state == support::setup_state::info);
    REQUIRE(row->sentence.contains(QStringLiteral("Choose Allow")));
    REQUIRE(row->action == support::setup_action::firewall_help);

    report = mac_report_with(
        {mac_fact(perm::topic::macos_firewall, perm::reason::block_all_inbound,
                  perm::state::attention)});
    REQUIRE(find_row(report, title)->state == support::setup_state::attention);

    report = mac_report_with(
        {mac_fact(perm::topic::macos_firewall, perm::reason::not_asked,
                  perm::state::unknown)});
    REQUIRE(find_row(report, title)->state ==
            support::setup_state::not_checked);
}

TEST_CASE("macos rows: Local Network access is information that cannot be "
          "checked, and the sandbox row names the entitlements")
{
    auto report = mac_report_with(
        {mac_fact(perm::topic::macos_local_network,
                  perm::reason::local_network_prompt, perm::state::info)});
    const auto* row = find_row(report, QStringLiteral("Local Network access"));
    REQUIRE(row->state == support::setup_state::info);
    REQUIRE(row->sentence.contains(QStringLiteral("cannot be checked")));
    REQUIRE(row->action == support::setup_action::network_privacy_help);

    report = mac_report_with(
        {mac_fact(perm::topic::confinement, perm::reason::macos_sandbox,
                  perm::state::info)});
    row = find_row(report, QStringLiteral("Confinement"));
    REQUIRE(row->sentence.contains(QStringLiteral("App Sandbox")));

    // The explanation dialog has no step to run on macOS.
    support::PermissionDialog dialog(mac_fact(perm::topic::macos_firewall,
                                              perm::reason::alf_prompt,
                                              perm::state::info),
                                     true);
    REQUIRE(dialog.step_button() == nullptr);
}
