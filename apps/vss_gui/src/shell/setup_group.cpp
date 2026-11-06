//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "shell/setup_group.hpp"

#include "dialogs/common.hpp"

#include <QSet>
#include <QStringList>

namespace vss::gui
{
namespace
{

bool involves(const link_view& link, const QSet<vss::endpoint_id>& members)
{
    for (const vss::endpoint_id id : link.sources)
    {
        if (members.contains(id))
            return true;
    }
    for (const vss::endpoint_id id : link.targets)
    {
        if (members.contains(id))
            return true;
    }
    return false;
}

setup_group grow(const std::vector<port_view>& ports,
                 const std::vector<link_view>& links,
                 QSet<vss::endpoint_id> members)
{
    // Keep adding the ends of any link that touches the group until nothing
    // new joins.  Setups are small, so the repeated scan is cheap.
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (const link_view& link : links)
        {
            if (!involves(link, members))
                continue;
            for (const auto& ends : {link.sources, link.targets})
            {
                for (const vss::endpoint_id id : ends)
                {
                    if (!members.contains(id))
                    {
                        members.insert(id);
                        changed = true;
                    }
                }
            }
        }
    }

    setup_group group;
    for (const port_view& port : ports)
    {
        if (members.contains(port.id))
            group.ports.push_back(port.id);
    }
    for (const link_view& link : links)
    {
        if (involves(link, members))
            group.links.push_back(link.id);
    }
    return group;
}

} // namespace

setup_group find_setup(const std::vector<port_view>& ports,
                       const std::vector<link_view>& links,
                       vss::endpoint_id port)
{
    bool known = false;
    for (const port_view& candidate : ports)
        known = known || candidate.id == port;
    if (!known)
        return {};
    return grow(ports, links, QSet<vss::endpoint_id>{port});
}

setup_group find_setup_of_link(const std::vector<port_view>& ports,
                               const std::vector<link_view>& links,
                               vss::link_id link)
{
    for (const link_view& candidate : links)
    {
        if (candidate.id != link)
            continue;
        QSet<vss::endpoint_id> seed;
        for (const vss::endpoint_id id : candidate.sources)
            seed.insert(id);
        for (const vss::endpoint_id id : candidate.targets)
            seed.insert(id);
        return grow(ports, links, seed);
    }
    return {};
}

QString setup_port_names(const setup_group& group,
                         const std::vector<port_view>& ports)
{
    QStringList names;
    for (const vss::endpoint_id id : group.ports)
    {
        for (const port_view& port : ports)
        {
            if (port.id != id)
                continue;
            const bool outside = !port.studio_only && !port.path.isEmpty() &&
                                 port.path != port.name;
            names << (outside ? QObject::tr("%1 (%2)").arg(port.name, port.path)
                              : port.name);
            break;
        }
    }
    return join_dialog_names(names);
}

} // namespace vss::gui
