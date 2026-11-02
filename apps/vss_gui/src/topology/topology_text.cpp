//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "topology/topology_text.hpp"

#include <QCoreApplication>
#include <QStringList>

#include <algorithm>

namespace vss::gui::topology_text
{
namespace
{

QString t(const char* text)
{
    return QCoreApplication::translate("Topology", text);
}

QString port_name(const QVector<vss::endpoint_id>& ids,
                  const QHash<vss::endpoint_id, QString>& names)
{
    QStringList list;
    for (const vss::endpoint_id id : ids)
    {
        const QString name = names.value(id);
        if (!name.isEmpty())
            list.append(name);
    }
    return join_names(list);
}

QString display_path(const port_view& port)
{
    const QString scheme = QStringLiteral("serial://");
    if (port.path.startsWith(scheme, Qt::CaseInsensitive))
        return port.path.mid(scheme.size());
    return port.path;
}

QString with_states(const QString& base, const link_view& link)
{
    QStringList states;
    if (link.mode == vss::link_mode::duplex &&
        (link.shape == link_shape::split || link.shape == link_shape::join))
    {
        states.append(t("two-way"));
    }
    if (link.options.policy == vss::merge_policy::owner)
        states.append(t("owner"));

    if (states.size() == 1)
        return t("%1, %2").arg(base, states.first());
    if (states.size() == 2)
        return t("%1, %2, %3").arg(base, states.at(0), states.at(1));
    return base;
}

} // namespace

QString view_name()
{
    return t("Topology");
}

QString links_list_name()
{
    return t("Links");
}

QString empty_state()
{
    return t("Add a port to see it here.");
}

QString hint()
{
    return t("Click a box to inspect it. Click a line to select the link.");
}

QString loop_note()
{
    return t("That link would create a loop. Data would travel from device to "
             "terminal and back to device forever. Nothing was changed. "
             "Remove a link first, or choose different ports.");
}

QString show_loop()
{
    return t("Show the loop");
}

QString owner_badge()
{
    return t("Owner");
}

QString dropped_badge()
{
    return t("Dropped");
}

QString unlinked_badge()
{
    return t("not linked");
}

QString no_links()
{
    return t("No links yet.");
}

QString inspect_action()
{
    return t("Inspect");
}

QString copy_name_action()
{
    return t("Copy name");
}

QString rename_action()
{
    return t("Rename");
}

QString connect_from_here_action()
{
    return t("Connect from here");
}

QString connect_needs_two()
{
    return t("Connect ports needs at least two ports.");
}

QString remove_action()
{
    return t("Remove");
}

QString remove_link_action()
{
    return t("Remove link");
}

QString port_count(int count)
{
    return count == 1 ? t("1 port") : t("%1 ports").arg(count);
}

QString link_count(int count)
{
    return count == 1 ? t("1 link") : t("%1 links").arg(count);
}

QString join_names(const QStringList& names)
{
    if (names.isEmpty())
        return {};
    if (names.size() == 1)
        return names.first();
    if (names.size() == 2)
        return t("%1 and %2").arg(names.at(0), names.at(1));
    return t("%1 and %2")
        .arg(names.mid(0, names.size() - 1).join(QStringLiteral(", ")),
             names.last());
}

QString node_detail(const port_view& port)
{
    switch (port.kind)
    {
    case vss::endpoint_kind::physical:
        return t("Real device, %1").arg(display_path(port));
    case vss::endpoint_kind::mock:
        return t("Fixture (mock)");
    case vss::endpoint_kind::virtual_port:
    case vss::endpoint_kind::tcp:
    case vss::endpoint_kind::udp:
    case vss::endpoint_kind::rfc2217:
        break;
    }
    if (port.studio_only)
        return t("Studio only");
    return port.path;
}

bool node_detail_is_mono(const port_view& port)
{
    return port.kind != vss::endpoint_kind::physical &&
           port.kind != vss::endpoint_kind::mock && !port.studio_only;
}

QString node_tooltip(const port_view& port, bool unlinked)
{
    QString text = port.name;
    const QString detail = node_detail(port);
    if (!detail.isEmpty())
        text += QLatin1Char('\n') + detail;
    if (unlinked)
        text += QLatin1Char('\n') + t("Not linked to another port.");
    return text;
}

QString link_primary_label(link_shape shape)
{
    switch (shape)
    {
    case link_shape::split:
        return t("Split");
    case link_shape::join:
        return t("Join");
    case link_shape::two_way:
        return t("two-way");
    case link_shape::monitor:
        return t("monitor");
    case link_shape::one_way:
        break;
    }
    return {};
}

QString link_secondary_label(const link_view& link)
{
    if (link.options.policy == vss::merge_policy::owner)
        return t("owner");
    if (link.mode == vss::link_mode::duplex &&
        (link.shape == link_shape::split || link.shape == link_shape::join))
    {
        return t("two-way");
    }
    return {};
}

QString summary(const std::vector<port_view>& ports,
                const std::vector<link_view>& links)
{
    if (ports.empty())
        return t("No ports yet. Add a port to see it here.");

    QHash<vss::endpoint_id, QString> names;
    for (const auto& port : ports)
        names.insert(port.id, port.name);

    const QString ports_text = port_count(static_cast<int>(ports.size()));
    if (links.empty())
        return t("%1, no links").arg(ports_text);

    QStringList phrases;
    for (const auto& link : links)
        phrases.append(link_phrase(link, names));
    return t("%1, %2: %3")
        .arg(ports_text, link_count(static_cast<int>(links.size())),
             phrases.join(QStringLiteral("; ")));
}

QString link_name(const link_view& link,
                  const QHash<vss::endpoint_id, QString>& names)
{
    const QString source = port_name(link.sources, names);
    const QString target = port_name(link.targets, names);

    QString base;
    switch (link.shape)
    {
    case link_shape::split:
        if (link.targets.size() > 1)
        {
            base = t("Split link, %1 to %2 ports")
                       .arg(source)
                       .arg(link.targets.size());
        }
        else
        {
            base = t("Split link, %1 to %2").arg(source, target);
        }
        break;
    case link_shape::join:
        if (link.sources.size() > 1)
        {
            base = t("Join link, %1 ports into %2")
                       .arg(link.sources.size())
                       .arg(target);
        }
        else
        {
            base = t("Join link, %1 into %2").arg(source, target);
        }
        break;
    case link_shape::two_way:
        base = t("Two-way link, %1 to %2").arg(source, target);
        break;
    case link_shape::one_way:
        base = t("One-way link, %1 to %2").arg(source, target);
        break;
    case link_shape::monitor:
        base = t("Monitor link, %1 to %2").arg(source, target);
        break;
    }
    return with_states(base, link);
}

QString link_phrase(const link_view& link,
                    const QHash<vss::endpoint_id, QString>& names)
{
    const QString source = port_name(link.sources, names);
    const QString target = port_name(link.targets, names);

    switch (link.shape)
    {
    case link_shape::split:
        return t("split from %1 to %2").arg(source, target);
    case link_shape::join:
        return t("join from %1 into %2").arg(source, target);
    case link_shape::two_way:
        return t("two-way between %1 and %2").arg(source, target);
    case link_shape::one_way:
        return t("one-way from %1 to %2").arg(source, target);
    case link_shape::monitor:
        return t("monitor from %1 to %2").arg(source, target);
    }
    return {};
}

QString copied(const QString& name)
{
    return t("Copied %1.").arg(name);
}

QString selected(const QString& name)
{
    return t("Selected %1.").arg(name);
}

} // namespace vss::gui::topology_text
