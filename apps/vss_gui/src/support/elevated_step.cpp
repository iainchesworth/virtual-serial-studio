//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "support/elevated_step.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include <string>
#include <vector>

namespace vss::gui::support
{
namespace
{

namespace perm = vss::permissions;

QString report_text(perm::step_status status, bool remove)
{
    switch (status)
    {
    case perm::step_status::done:
        return remove ? QCoreApplication::translate(
                            "SetupCheck", "The firewall rule was removed.")
                      : QCoreApplication::translate(
                            "SetupCheck", "The firewall rule was added.");
    case perm::step_status::declined:
        return QCoreApplication::translate(
            "SetupCheck",
            "You declined the Windows permission prompt, so nothing was "
            "changed.");
    case perm::step_status::unsupported:
        return QCoreApplication::translate(
            "SetupCheck", "Not available in this build.");
    case perm::step_status::not_elevated:
        return QCoreApplication::translate(
            "SetupCheck",
            "Windows did not give the helper administrator rights, so nothing "
            "was changed.");
    case perm::step_status::invalid:
        return QCoreApplication::translate(
            "SetupCheck",
            "The helper refused the rule, so nothing was changed. Use the "
            "command shown instead.");
    case perm::step_status::failed:
        break;
    }
    return QCoreApplication::translate(
        "SetupCheck",
        "The firewall rule could not be changed. Use the command shown "
        "instead, in a terminal opened as administrator.");
}

step_report run(const perm::firewall_rule_spec& spec, bool remove)
{
    step_report report;
    if (!firewall_step_available())
    {
        report.status = perm::step_status::unsupported;
        report.message = report_text(report.status, remove);
        return report;
    }
    const perm::step_result result = perm::run_elevated(
        helper_path().toStdString(), perm::elevated_arguments(spec, remove));
    report.status = result.status;
    report.message = report_text(result.status, remove);
    return report;
}

} // namespace

QString helper_path()
{
    const QDir directory(QCoreApplication::applicationDirPath());
    for (const char* name : {"vss_cli.exe", "vss_cli"})
    {
        const QFileInfo candidate(directory.filePath(QString::fromLatin1(name)));
        if (candidate.isFile())
            return QDir::toNativeSeparators(candidate.absoluteFilePath());
    }
    return {};
}

bool firewall_step_available()
{
    return perm::can_manage_firewall_rules() && perm::can_run_elevated() &&
           !helper_path().isEmpty();
}

step_report allow_through_firewall(const perm::firewall_rule_spec& spec)
{
    return run(spec, false);
}

step_report remove_firewall_rule(const perm::firewall_rule_spec& spec)
{
    return run(spec, true);
}

} // namespace vss::gui::support
