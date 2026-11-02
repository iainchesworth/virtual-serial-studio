//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "topology/topology_layout.hpp"

#include <QMap>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <utility>

namespace vss::gui::topology
{
namespace
{

constexpr qreal kArrowLength = 8.0;
constexpr qreal kArrowHalfWidth = 4.5;

QPointF right_centre(const QRectF& rect)
{
    return QPointF(rect.right(), rect.center().y());
}

QPointF left_centre(const QRectF& rect)
{
    return QPointF(rect.left(), rect.center().y());
}

QPolygonF arrow_head(const QPointF& tip, const QPointF& from)
{
    const qreal length = QLineF(from, tip).length();
    if (length < 1e-6)
        return {};

    const QPointF unit((tip.x() - from.x()) / length,
                       (tip.y() - from.y()) / length);
    const QPointF base = tip - unit * kArrowLength;
    const QPointF perpendicular(-unit.y(), unit.x());

    QPolygonF arrow;
    arrow << tip << base + perpendicular * kArrowHalfWidth
          << base - perpendicular * kArrowHalfWidth;
    return arrow;
}

void note_point(QRectF& bounds, const QPointF& point)
{
    const QRectF dot(point, QSizeF(0.0, 0.0));
    bounds = bounds.isNull() ? dot : bounds.united(dot);
}

void note_rect(QRectF& bounds, const QRectF& rect)
{
    bounds = bounds.isNull() ? rect : bounds.united(rect);
}

} // namespace

QRectF topology_layout::node_rect(vss::endpoint_id id) const
{
    const topo_node_placement* placement = find_node(id);
    return placement == nullptr ? QRectF() : placement->rect;
}

const topo_node_placement* topology_layout::find_node(vss::endpoint_id id) const
{
    for (const auto& node : nodes)
    {
        if (node.id == id)
            return &node;
    }
    return nullptr;
}

const topo_link_placement* topology_layout::find_link(vss::link_id id) const
{
    for (const auto& link : links)
    {
        if (link.id == id)
            return &link;
    }
    return nullptr;
}

topology_layout compute_topology_layout(const QVector<topo_node_input>& nodes,
                                        const QVector<topo_link_input>& links,
                                        const topology_metrics& metrics)
{
    topology_layout result;
    if (nodes.isEmpty())
        return result;

    QVector<topo_node_input> ordered_nodes = nodes;
    std::sort(ordered_nodes.begin(), ordered_nodes.end(),
              [](const topo_node_input& left, const topo_node_input& right)
              { return left.id < right.id; });

    QVector<topo_link_input> ordered_links = links;
    std::sort(ordered_links.begin(), ordered_links.end(),
              [](const topo_link_input& left, const topo_link_input& right)
              { return left.id < right.id; });

    QHash<vss::endpoint_id, QSizeF> sizes;
    QHash<vss::endpoint_id, int> rank;
    QHash<vss::endpoint_id, QVector<vss::endpoint_id>> neighbours;
    for (const auto& node : ordered_nodes)
    {
        sizes.insert(node.id, node.size);
        rank.insert(node.id, 0);
    }

    for (const auto& link : ordered_links)
    {
        for (const vss::endpoint_id source : link.sources)
        {
            if (!rank.contains(source))
                continue;
            for (const vss::endpoint_id target : link.targets)
            {
                if (!rank.contains(target))
                    continue;
                neighbours[source].push_back(target);
                neighbours[target].push_back(source);
            }
        }
    }
    for (auto it = neighbours.begin(); it != neighbours.end(); ++it)
    {
        std::sort(it->begin(), it->end());
        it->erase(std::unique(it->begin(), it->end()), it->end());
    }

    // Longest-path ranks.  The engine refuses cycles, so a handful of passes
    // is enough; the clamp keeps a malformed document deterministic instead
    // of unbounded.
    const int node_count = static_cast<int>(ordered_nodes.size());
    for (int pass = 0; pass < node_count; ++pass)
    {
        bool changed = false;
        for (const auto& link : ordered_links)
        {
            for (const vss::endpoint_id source : link.sources)
            {
                if (!rank.contains(source))
                    continue;
                for (const vss::endpoint_id target : link.targets)
                {
                    if (!rank.contains(target))
                        continue;
                    const int candidate =
                        std::min(node_count - 1, rank.value(source) + 1);
                    if (candidate > rank.value(target))
                    {
                        rank[target] = candidate;
                        changed = true;
                    }
                }
            }
        }
        if (!changed)
            break;
    }

    // A port with no link is not a source or a target: give it its own column
    // after the graph so it cannot push a real source off the centre line.
    if (!ordered_links.isEmpty())
    {
        QSet<vss::endpoint_id> linked;
        for (const auto& link : ordered_links)
        {
            for (const vss::endpoint_id id : link.sources)
                linked.insert(id);
            for (const vss::endpoint_id id : link.targets)
                linked.insert(id);
        }
        int highest = 0;
        for (auto it = rank.constBegin(); it != rank.constEnd(); ++it)
            highest = std::max(highest, it.value());
        for (const auto& node : ordered_nodes)
        {
            if (!linked.contains(node.id))
                rank[node.id] = highest + 1;
        }
    }

    QMap<int, QVector<vss::endpoint_id>> by_rank;
    for (const auto& node : ordered_nodes)
        by_rank[rank.value(node.id)].push_back(node.id);

    QVector<QVector<vss::endpoint_id>> columns;
    columns.reserve(by_rank.size());
    for (auto it = by_rank.begin(); it != by_rank.end(); ++it)
        columns.push_back(*it);

    // One barycentre pass in each direction: peers in the neighbouring column
    // decide the vertical order.  Ties fall back to the port id, so the result
    // is stable for a given setup.
    const auto order_column = [&columns, &neighbours](int column, int reference)
    {
        QHash<vss::endpoint_id, double> position;
        const auto& reference_ids = columns.at(reference);
        for (int index = 0; index < reference_ids.size(); ++index)
            position.insert(reference_ids.at(index),
                            static_cast<double>(index));

        auto& ids = columns[column];
        QHash<vss::endpoint_id, double> fallback;
        for (int index = 0; index < ids.size(); ++index)
            fallback.insert(ids.at(index), static_cast<double>(index));

        QHash<vss::endpoint_id, double> key;
        for (const vss::endpoint_id id : ids)
        {
            double sum = 0.0;
            int count = 0;
            for (const vss::endpoint_id peer : neighbours.value(id))
            {
                const auto found = position.constFind(peer);
                if (found == position.constEnd())
                    continue;
                sum += found.value();
                ++count;
            }
            key.insert(id, count == 0 ? fallback.value(id) : sum / count);
        }

        std::stable_sort(ids.begin(), ids.end(),
                         [&key](vss::endpoint_id left, vss::endpoint_id right)
                         {
                             if (key.value(left) != key.value(right))
                                 return key.value(left) < key.value(right);
                             return left < right;
                         });
    };

    for (int column = 1; column < columns.size(); ++column)
        order_column(column, column - 1);
    for (int column = columns.size() - 2; column >= 0; --column)
        order_column(column, column + 1);

    QVector<qreal> column_width(columns.size(), 0.0);
    QVector<qreal> column_height(columns.size(), 0.0);
    for (int column = 0; column < columns.size(); ++column)
    {
        qreal width = 0.0;
        qreal height =
            metrics.vertical_gap * static_cast<qreal>(std::max<qsizetype>(
                                       0, columns.at(column).size() - 1));
        for (const vss::endpoint_id id : columns.at(column))
        {
            const QSizeF size = sizes.value(id);
            width = std::max(width, size.width());
            height += size.height();
        }
        column_width[column] = width;
        column_height[column] = height;
    }

    const qreal total_height =
        *std::max_element(column_height.begin(), column_height.end());

    QHash<vss::endpoint_id, QRectF> rects;
    qreal x = metrics.margin;
    for (int column = 0; column < columns.size(); ++column)
    {
        qreal y =
            metrics.margin + (total_height - column_height.at(column)) / 2.0;
        for (const vss::endpoint_id id : columns.at(column))
        {
            const QSizeF size = sizes.value(id);
            const QRectF rect(x + (column_width.at(column) - size.width()) /
                                      2.0,
                              y, size.width(), size.height());
            result.nodes.append({id, rect});
            rects.insert(id, rect);
            y += size.height() + metrics.vertical_gap;
        }
        x += column_width.at(column) + metrics.horizontal_gap;
    }

    result.links = compute_link_geometry(ordered_links, rects);

    QRectF bounds;
    for (const auto& node : result.nodes)
        note_rect(bounds, node.rect);
    for (const auto& link : result.links)
    {
        for (const QLineF& line : link.segments)
        {
            note_point(bounds, line.p1());
            note_point(bounds, line.p2());
        }
        if (!link.bar.isNull())
        {
            note_point(bounds, link.bar.p1());
            note_point(bounds, link.bar.p2());
        }
        for (const QPolygonF& arrow : link.arrows)
        {
            for (const QPointF& point : arrow)
                note_point(bounds, point);
        }
    }

    result.content = bounds.isNull()
                         ? QRectF()
                         : bounds.adjusted(-metrics.margin, -metrics.margin,
                                           metrics.margin, metrics.margin);
    return result;
}

QVector<topo_link_placement>
compute_link_geometry(const QVector<topo_link_input>& links,
                      const QHash<vss::endpoint_id, QRectF>& rects)
{
    QVector<topo_link_placement> placements;
    for (const auto& link : links)
    {
        QVector<QRectF> sources;
        for (const vss::endpoint_id id : link.sources)
        {
            if (rects.contains(id))
                sources.push_back(rects.value(id));
        }
        QVector<QRectF> targets;
        for (const vss::endpoint_id id : link.targets)
        {
            if (rects.contains(id))
                targets.push_back(rects.value(id));
        }
        if (sources.isEmpty() || targets.isEmpty())
            continue;

        topo_link_placement placement;
        placement.id = link.id;

        const auto add_segment = [&placement](const QLineF& line)
        { placement.segments.append(line); };

        if (link.shape == link_shape::split && sources.size() == 1 &&
            targets.size() > 1)
        {
            std::sort(targets.begin(), targets.end(),
                      [](const QRectF& left, const QRectF& right)
                      { return left.center().y() < right.center().y(); });

            const QRectF source = sources.first();
            qreal target_left = targets.first().left();
            for (const auto& target : targets)
                target_left = std::min(target_left, target.left());

            const qreal bar_x = (source.right() + target_left) / 2.0;
            add_segment(QLineF(right_centre(source),
                               QPointF(bar_x, source.center().y())));
            placement.bar = QLineF(bar_x, targets.first().center().y(), bar_x,
                                   targets.last().center().y());

            for (const auto& target : targets)
            {
                add_segment(QLineF(bar_x, target.center().y(), target.left(),
                                   target.center().y()));
                if (link.arrow)
                {
                    placement.arrows.append(
                        arrow_head(QPointF(target.left(), target.center().y()),
                                   QPointF(bar_x, target.center().y())));
                }
            }
            placement.primary_anchor =
                QPointF((source.right() + bar_x) / 2.0, source.center().y());
            placement.secondary_anchor = placement.primary_anchor;
        }
        else if (link.shape == link_shape::join && sources.size() > 1 &&
                 targets.size() == 1)
        {
            std::sort(sources.begin(), sources.end(),
                      [](const QRectF& left, const QRectF& right)
                      { return left.center().y() < right.center().y(); });

            const QRectF target = targets.first();
            qreal source_right = sources.first().right();
            for (const auto& source : sources)
                source_right = std::max(source_right, source.right());

            const qreal bar_x = (source_right + target.left()) / 2.0;
            for (const auto& source : sources)
            {
                add_segment(QLineF(right_centre(source),
                                   QPointF(bar_x, source.center().y())));
            }
            placement.bar = QLineF(bar_x, sources.first().center().y(), bar_x,
                                   sources.last().center().y());

            add_segment(QLineF(bar_x, target.center().y(), target.left(),
                               target.center().y()));
            if (link.arrow)
            {
                placement.arrows.append(
                    arrow_head(QPointF(target.left(), target.center().y()),
                               QPointF(bar_x, target.center().y())));
            }
            placement.primary_anchor =
                QPointF((bar_x + target.left()) / 2.0, target.center().y());
            placement.secondary_anchor = placement.primary_anchor;
        }
        else
        {
            bool first = true;
            for (const auto& source : sources)
            {
                for (const auto& target : targets)
                {
                    const QLineF line =
                        QLineF(right_centre(source), left_centre(target));
                    add_segment(line);
                    if (link.arrow)
                    {
                        placement.arrows.append(arrow_head(
                            left_centre(target), right_centre(source)));
                    }
                    if (first)
                    {
                        placement.primary_anchor =
                            (line.p1() + line.p2()) / 2.0;
                        placement.secondary_anchor = placement.primary_anchor;
                        first = false;
                    }
                }
            }
        }

        placements.append(placement);
    }
    return placements;
}

QVector<vss::endpoint_id> find_cycle(const QVector<topo_link_input>& links,
                                     const QVector<vss::endpoint_id>& node_ids)
{
    QHash<vss::endpoint_id, QVector<vss::endpoint_id>> adjacency;
    for (const auto& link : links)
    {
        for (const vss::endpoint_id source : link.sources)
        {
            for (const vss::endpoint_id target : link.targets)
                adjacency[source].push_back(target);
        }
    }
    for (auto it = adjacency.begin(); it != adjacency.end(); ++it)
    {
        std::sort(it->begin(), it->end());
        it->erase(std::unique(it->begin(), it->end()), it->end());
    }

    QVector<vss::endpoint_id> starts = node_ids;
    std::sort(starts.begin(), starts.end());
    starts.erase(std::unique(starts.begin(), starts.end()), starts.end());

    QHash<vss::endpoint_id, int> state;
    QVector<vss::endpoint_id> stack;
    QVector<vss::endpoint_id> loop;

    std::function<bool(vss::endpoint_id)> visit =
        [&](vss::endpoint_id id) -> bool
    {
        state[id] = 1;
        stack.append(id);
        for (const vss::endpoint_id next : adjacency.value(id))
        {
            const int colour = state.value(next, 0);
            if (colour == 0)
            {
                if (visit(next))
                    return true;
            }
            else if (colour == 1)
            {
                const qsizetype start = stack.indexOf(next);
                if (start < 0)
                    continue;
                loop = stack.mid(start);
                loop.append(next);
                return true;
            }
        }
        state[id] = 2;
        stack.removeLast();
        return false;
    };

    for (const vss::endpoint_id id : starts)
    {
        if (state.value(id, 0) == 0 && visit(id))
            break;
    }
    return loop;
}

QVector<vss::link_id> cycle_links(const QVector<topo_link_input>& links,
                                  const QVector<vss::endpoint_id>& cycle)
{
    QSet<vss::endpoint_id> members;
    for (const vss::endpoint_id id : cycle)
        members.insert(id);

    QVector<vss::link_id> result;
    for (const auto& link : links)
    {
        if (members.isEmpty())
            break;
        bool source_in = false;
        for (const vss::endpoint_id id : link.sources)
        {
            if (members.contains(id))
            {
                source_in = true;
                break;
            }
        }
        if (!source_in)
            continue;
        for (const vss::endpoint_id id : link.targets)
        {
            if (members.contains(id))
            {
                if (!result.contains(link.id))
                    result.append(link.id);
                break;
            }
        }
    }
    return result;
}

} // namespace vss::gui::topology
