//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "backend/share_edit.hpp"

#include <QSet>

#include <algorithm>

namespace vss::gui
{
namespace
{

const port_view* find_port(const std::vector<port_view>& ports,
                           vss::endpoint_id id)
{
    for (const port_view& port : ports)
    {
        if (port.id == id)
            return &port;
    }
    return nullptr;
}

std::optional<share_ref> as_share(const std::vector<port_view>& ports,
                                  const link_view& link)
{
    if (link.mode != vss::link_mode::duplex ||
        (link.shape != link_shape::two_way && link.shape != link_shape::split))
        return std::nullopt;

    QVector<vss::endpoint_id> ends = link.sources;
    ends += link.targets;
    share_ref share;
    share.link = link.id;
    for (const vss::endpoint_id id : ends)
    {
        const port_view* port = find_port(ports, id);
        if (port == nullptr)
            return std::nullopt;
        if (port->kind == vss::endpoint_kind::virtual_port)
        {
            share.programs.push_back(id);
        }
        else if (share.device == 0)
        {
            share.device = id;
        }
        else
        {
            return std::nullopt;
        }
    }
    if (share.device == 0 || share.programs.isEmpty())
        return std::nullopt;
    return share;
}

} // namespace

std::optional<share_ref> find_share(const std::vector<port_view>& ports,
                                    const std::vector<link_view>& links,
                                    vss::endpoint_id port)
{
    for (const link_view& link : links)
    {
        const auto share = as_share(ports, link);
        if (!share)
            continue;
        if (share->device == port || share->programs.contains(port))
            return share;
    }
    return std::nullopt;
}

std::optional<share_ref> find_share_of_link(const std::vector<port_view>& ports,
                                            const std::vector<link_view>& links,
                                            vss::link_id link)
{
    for (const link_view& candidate : links)
    {
        if (candidate.id == link)
            return as_share(ports, candidate);
    }
    return std::nullopt;
}

QString next_program_name(const std::vector<port_view>& ports)
{
    QSet<QString> used;
    for (const port_view& port : ports)
        used.insert(port.name);
    for (int number = 1;; ++number)
    {
        const QString name = QStringLiteral("program-%1").arg(number);
        if (!used.contains(name))
            return name;
    }
}

vss::result<share_added> add_program_port(StudioBackend& backend,
                                          const share_ref& share,
                                          const QString& name, bool capture)
{
    const auto link = backend.link(share.link);
    if (!link)
        return vss::make_error_code(vss::errc::not_found);

    const auto created = backend.create_auto_port(name, capture);
    if (!created)
        return created.error();

    std::vector<vss::endpoint_id> old_targets;
    for (const vss::endpoint_id id : share.programs)
        old_targets.push_back(id);
    std::vector<vss::endpoint_id> new_targets = old_targets;
    new_targets.push_back(*created);

    const auto disconnected = backend.disconnect(share.link);
    if (!disconnected)
    {
        static_cast<void>(backend.remove_port(*created));
        return disconnected.error();
    }

    const auto replaced =
        backend.split(share.device, new_targets, link->options);
    if (!replaced)
    {
        // Put the old link back.  If that fails too the ports are still
        // there and unlinked, which the topology shows.
        static_cast<void>(
            backend.split(share.device, old_targets, link->options));
        static_cast<void>(backend.remove_port(*created));
        return replaced.error();
    }
    return share_added{*replaced, *created};
}

vss::result<vss::link_id> remove_program_port(StudioBackend& backend,
                                              const share_ref& share,
                                              vss::endpoint_id program)
{
    if (!share.programs.contains(program))
        return vss::make_error_code(vss::errc::invalid_argument);
    const auto link = backend.link(share.link);
    if (!link)
        return vss::make_error_code(vss::errc::not_found);

    std::vector<vss::endpoint_id> remaining;
    for (const vss::endpoint_id id : share.programs)
    {
        if (id != program)
            remaining.push_back(id);
    }

    const auto removed = backend.remove_port(program);
    if (!removed)
        return removed.error();
    if (remaining.empty())
        return vss::link_id{0};

    // The engine dropped the link with the port.  Share again with what is
    // left; the remaining ports keep their names and paths.
    const auto replaced = backend.split(share.device, remaining, link->options);
    if (!replaced)
        return replaced.error();
    return *replaced;
}

} // namespace vss::gui
