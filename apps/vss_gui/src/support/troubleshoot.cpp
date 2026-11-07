//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "support/troubleshoot.hpp"

#include <vss/core/profile.hpp>

#include <QCoreApplication>
#include <QStringList>

#include <algorithm>
#include <chrono>

namespace vss::gui::support
{
namespace
{

constexpr auto kOneMinute = std::chrono::minutes(1);

const port_view* find_port(const QVector<port_view>& ports, vss::endpoint_id id)
{
    const auto it =
        std::find_if(ports.begin(), ports.end(),
                     [id](const port_view& port) { return port.id == id; });
    return it == ports.end() ? nullptr : &*it;
}

QStringList names_for(const QVector<port_view>& ports,
                      const QVector<vss::endpoint_id>& ids)
{
    QStringList names;
    for (const vss::endpoint_id id : ids)
    {
        if (const port_view* port = find_port(ports, id))
            names.push_back(port->name);
        else
            names.push_back(
                QCoreApplication::translate("Troubleshoot", "port %1").arg(id));
    }
    return names;
}

QString joined(const QStringList& names)
{
    if (names.isEmpty())
        return QCoreApplication::translate("Troubleshoot", "another port");
    if (names.size() == 1)
        return names.first();
    QStringList head = names.mid(0, names.size() - 1);
    return QCoreApplication::translate("Troubleshoot", "%1 and %2")
        .arg(head.join(QStringLiteral(", ")), names.last());
}

/// The endpoint on the other side of a link, preferring a real or mock device
/// over another virtual port.
const port_view* linked_device(const QVector<port_view>& ports,
                               const link_view& link, vss::endpoint_id self)
{
    QVector<vss::endpoint_id> candidates;
    for (const vss::endpoint_id id : link.sources)
    {
        if (id != self)
            candidates.push_back(id);
    }
    for (const vss::endpoint_id id : link.targets)
    {
        if (id != self)
            candidates.push_back(id);
    }
    const port_view* fallback = nullptr;
    for (const vss::endpoint_id id : candidates)
    {
        const port_view* port = find_port(ports, id);
        if (port == nullptr)
            continue;
        if (port->kind == vss::endpoint_kind::physical ||
            port->kind == vss::endpoint_kind::mock)
        {
            return port;
        }
        if (fallback == nullptr)
            fallback = port;
    }
    return fallback;
}

check_row passed(const QString& title, const QString& sentence)
{
    return {check_state::passed,
            QStringLiteral("done"),
            title,
            sentence,
            QCoreApplication::translate("Troubleshoot", "Passed"),
            false};
}

check_row attention(const QString& title, const QString& sentence,
                    bool highlight)
{
    return {check_state::attention,
            QStringLiteral("warning"),
            title,
            sentence,
            QCoreApplication::translate("Troubleshoot", "Needs attention"),
            highlight};
}

check_row not_checked(const QString& title, const QString& sentence)
{
    return {check_state::not_checked,
            QStringLiteral("info"),
            title,
            sentence,
            QCoreApplication::translate("Troubleshoot", "Not checked yet"),
            false};
}

QString link_sentence(const port_view& port, const link_view& link,
                      const QVector<port_view>& ports)
{
    const QStringList sources = names_for(ports, link.sources);
    const QStringList targets = names_for(ports, link.targets);
    switch (link.shape)
    {
    case link_shape::split:
        if (link.targets.contains(port.id))
        {
            return QCoreApplication::translate("Troubleshoot",
                                               "%1 is in a split link from %2.")
                .arg(port.name, joined(sources));
        }
        return QCoreApplication::translate("Troubleshoot",
                                           "%1 is in a split link to %2.")
            .arg(port.name, joined(targets));
    case link_shape::two_way:
        return QCoreApplication::translate("Troubleshoot",
                                           "%1 is in a two-way link with %2.")
            .arg(port.name, joined(sources + targets));
    case link_shape::join:
        if (link.targets.contains(port.id))
        {
            return QCoreApplication::translate("Troubleshoot",
                                               "%1 is joined from %2.")
                .arg(port.name, joined(sources));
        }
        return QCoreApplication::translate("Troubleshoot",
                                           "%1 is in a join link into %2.")
            .arg(port.name, joined(targets));
    case link_shape::monitor:
        return QCoreApplication::translate("Troubleshoot",
                                           "%1 is in a monitor link with %2.")
            .arg(port.name, joined(sources + targets));
    case link_shape::one_way:
        if (link.sources.contains(port.id))
        {
            return QCoreApplication::translate("Troubleshoot",
                                               "%1 is in a one-way link to %2.")
                .arg(port.name, joined(targets));
        }
        return QCoreApplication::translate("Troubleshoot",
                                           "%1 is in a one-way link from %2.")
            .arg(port.name, joined(sources));
    case link_shape::bus:
    {
        QStringList others = sources;
        others.removeAll(port.name);
        return QCoreApplication::translate("Troubleshoot",
                                           "%1 is on a shared bus with %2.")
            .arg(port.name, joined(others));
    }
    }
    return {};
}

} // namespace

QString port_display_path(const port_view& port)
{
    return port.path.isEmpty() ? port.name : port.path;
}

troubleshoot_report build_troubleshoot_report(const troubleshoot_facts& facts)
{
    VSS_PROFILE_ZONE("gui.support.troubleshoot.report");
    troubleshoot_report report;
    const port_view* port = find_port(facts.ports, facts.port);
    const QString path = port != nullptr ? port_display_path(*port) : QString();
    const QString port_title =
        path.isEmpty()
            ? QCoreApplication::translate("Troubleshoot", "this port")
            : path;

    // 1. The port exists and is open.
    if (port == nullptr)
    {
        report.rows.push_back(not_checked(
            QCoreApplication::translate("Troubleshoot",
                                        "The port exists and is open"),
            QCoreApplication::translate("Troubleshoot",
                                        "Studio has no port to check.")));
    }
    else if (port->open && port->state == vss::endpoint_state::open)
    {
        report.rows.push_back(passed(
            QCoreApplication::translate("Troubleshoot",
                                        "The port exists and is open"),
            QCoreApplication::translate("Troubleshoot", "%1 is open on %2.")
                .arg(port->name, path)));
    }
    else
    {
        report.rows.push_back(attention(
            QCoreApplication::translate("Troubleshoot",
                                        "The port exists and is open"),
            QCoreApplication::translate("Troubleshoot", "%1 is not open.")
                .arg(port->name),
            false));
    }

    // 2. The port is linked to the device.
    const link_view* link = nullptr;
    if (port != nullptr)
    {
        for (const link_view& candidate : facts.links)
        {
            if (candidate.sources.contains(port->id) ||
                candidate.targets.contains(port->id))
            {
                link = &candidate;
                break;
            }
        }
    }
    if (port == nullptr)
    {
        report.rows.push_back(not_checked(
            QCoreApplication::translate("Troubleshoot",
                                        "The port is linked to the device"),
            QCoreApplication::translate("Troubleshoot",
                                        "Studio has no port to check.")));
    }
    else if (link == nullptr)
    {
        report.rows.push_back(
            attention(QCoreApplication::translate(
                          "Troubleshoot", "The port is linked to the device"),
                      QCoreApplication::translate(
                          "Troubleshoot",
                          "This port has no link. Connect it to a device."),
                      false));
    }
    else
    {
        report.rows.push_back(
            passed(QCoreApplication::translate(
                       "Troubleshoot", "The port is linked to the device"),
                   link_sentence(*port, *link, facts.ports)));
    }

    // 3. The device is sending.
    const port_view* device = (port != nullptr && link != nullptr)
                                  ? linked_device(facts.ports, *link, port->id)
                                  : nullptr;
    const QString device_title =
        QCoreApplication::translate("Troubleshoot", "The device is sending");
    if (device == nullptr)
    {
        report.rows.push_back(not_checked(
            device_title,
            QCoreApplication::translate(
                "Troubleshoot", "Connect the port to a device first.")));
    }
    else if (device->stats.last_activity == vss::time_point{})
    {
        report.rows.push_back(not_checked(
            device_title,
            QCoreApplication::translate(
                "Troubleshoot", "Studio cannot tell when %1 last sent data.")
                .arg(device->name)));
    }
    else if (facts.now - device->stats.last_activity <= kOneMinute &&
             device->stats.bytes_rx > 0)
    {
        report.rows.push_back(
            passed(device_title,
                   QCoreApplication::translate(
                       "Troubleshoot", "%1 received data in the last minute.")
                       .arg(device->name)));
    }
    else if (device->stats.started != vss::time_point{} &&
             device->stats.bytes_rx == 0 &&
             facts.now - device->stats.started < kOneMinute)
    {
        report.rows.push_back(
            not_checked(device_title,
                        QCoreApplication::translate(
                            "Troubleshoot",
                            "Studio is still listening for the first bytes.")));
    }
    else
    {
        report.rows.push_back(attention(
            device_title,
            QCoreApplication::translate(
                "Troubleshoot",
                "%1 has not sent anything in the last minute. Check the cable "
                "and the device's power.")
                .arg(device->name),
            false));
    }

    // 4. A program has opened the advertised path.
    const QString program_title =
        QCoreApplication::translate("Troubleshoot", "A program has opened %1")
            .arg(port_title);
    if (port == nullptr)
    {
        report.rows.push_back(not_checked(
            QCoreApplication::translate("Troubleshoot",
                                        "A program has opened the port"),
            QCoreApplication::translate("Troubleshoot",
                                        "Studio has no port to check.")));
    }
    else if (!facts.peer_open_known || port->peer == peer_state::unknown)
    {
        report.rows.push_back(not_checked(
            program_title,
            QCoreApplication::translate(
                "Troubleshoot",
                "Studio cannot tell whether a program has opened %1 in this "
                "build.")
                .arg(port_title)));
    }
    else if (port->peer == peer_state::open)
    {
        report.rows.push_back(passed(
            program_title, QCoreApplication::translate(
                               "Troubleshoot", "A program has opened %1.")
                               .arg(port_title)));
    }
    else
    {
        report.rows.push_back(attention(
            program_title,
            QCoreApplication::translate(
                "Troubleshoot",
                "No program has opened %1 yet, so nothing can arrive. The "
                "program has to open exactly %1; a different COM port will "
                "not receive anything.")
                .arg(port_title),
            true));
    }

    // 5. Data reaches the program.  The engine has no visibility inside the
    // program, so this stays "Not checked yet" and says so.
    if (port != nullptr && facts.peer_open_known &&
        port->peer == peer_state::open)
    {
        report.rows.push_back(not_checked(
            QCoreApplication::translate("Troubleshoot",
                                        "Data reaches the program"),
            QCoreApplication::translate(
                "Troubleshoot",
                "Studio cannot see whether the program read the data.")));
    }
    else
    {
        report.rows.push_back(not_checked(
            QCoreApplication::translate("Troubleshoot",
                                        "Data reaches the program"),
            QCoreApplication::translate("Troubleshoot",
                                        "Checked once a program connects.")));
    }

    return report;
}

} // namespace vss::gui::support
