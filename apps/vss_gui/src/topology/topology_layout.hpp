//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_TOPOLOGY_LAYOUT_HPP
#define VSS_GUI_TOPOLOGY_LAYOUT_HPP

#include "backend/view_types.hpp"

#include <vss/core/types.hpp>

#include <QHash>
#include <QLineF>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QSizeF>
#include <QVector>

namespace vss::gui::topology
{

/// One node fed to the layout: the port id and the size the text needs.
struct topo_node_input
{
    vss::endpoint_id id = 0;
    QSizeF size{168.0, 56.0};
};

/// One link fed to the layout.
struct topo_link_input
{
    vss::link_id id = 0;
    link_shape shape = link_shape::two_way;
    QVector<vss::endpoint_id> sources;
    QVector<vss::endpoint_id> targets;
    /// Draw a filled arrow head at each target end (one-way and monitor).
    bool arrow = false;
};

/// Gaps and margins in scene units.  The design fixes the minimum gap at
/// 48 px; the horizontal gap is wider so the link labels fit between the
/// columns (8 px clear of a line or a node).
struct topology_metrics
{
    qreal horizontal_gap = 96.0;
    qreal vertical_gap = 48.0;
    qreal margin = 16.0;
};

struct topo_node_placement
{
    vss::endpoint_id id = 0;
    QRectF rect;
};

/// The pre-computed geometry of one link.  The item adds the label rectangles
/// from these anchors, because their size depends on the font.
struct topo_link_placement
{
    vss::link_id id = 0;
    QVector<QLineF> segments;
    /// The vertical bar of a split or a join; a null line when there is none.
    QLineF bar;
    QVector<QPolygonF> arrows;
    QPointF primary_anchor;
    QPointF secondary_anchor;
};

struct topology_layout
{
    /// Nodes in reading order: left column first, then top to bottom.  This
    /// is also the Tab order of the view.
    QVector<topo_node_placement> nodes;
    QVector<topo_link_placement> links;
    QRectF content;

    QRectF node_rect(vss::endpoint_id id) const;
    const topo_node_placement* find_node(vss::endpoint_id id) const;
    const topo_link_placement* find_link(vss::link_id id) const;
};

/// Places the nodes in deterministic columns: sources on the left, targets on
/// the right, each column centred vertically, with at least
/// `metrics.vertical_gap` between two nodes and at least
/// `metrics.horizontal_gap` between two columns.  Split and join links get a
/// mid-gap bar; every other link is a straight line.  The result depends only
/// on the input order of the ports, so two runs on the same setup are equal.
topology_layout compute_topology_layout(const QVector<topo_node_input>& nodes,
                                        const QVector<topo_link_input>& links,
                                        const topology_metrics& metrics = {});

/// Geometry for every link from the current node rectangles.  Split and join
/// links get their mid-gap bar; every other shape is a straight line.  The
/// view calls this again after a nudge, when only the node positions changed.
QVector<topo_link_placement>
compute_link_geometry(const QVector<topo_link_input>& links,
                      const QHash<vss::endpoint_id, QRectF>& rects);

/// Finds a directed cycle in the link graph.  The returned path starts and
/// ends at the same port, for example `a, b, c, a`; an acyclic graph returns
/// an empty vector.  Used by the "Show the loop" feedback.
QVector<vss::endpoint_id> find_cycle(const QVector<topo_link_input>& links,
                                     const QVector<vss::endpoint_id>& node_ids);

/// The links that connect two ports of the cycle, so they can be highlighted.
QVector<vss::link_id> cycle_links(const QVector<topo_link_input>& links,
                                  const QVector<vss::endpoint_id>& cycle);

} // namespace vss::gui::topology

#endif // VSS_GUI_TOPOLOGY_LAYOUT_HPP
