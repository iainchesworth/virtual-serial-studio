//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_TOPOLOGY_VIEW_HPP
#define VSS_GUI_TOPOLOGY_VIEW_HPP

#include <vss/core/endpoint.hpp>
#include <vss/core/link.hpp>
#include <vss/core/types.hpp>

#include <QHash>
#include <QGraphicsView>

#include <vector>

class QGraphicsScene;

namespace vss::gui {

class EndpointItem;
class LinkItem;

/// Graphical representation of the hub topology.
///
/// Nodes are boxes carrying the endpoint name, kind, advertised path and a
/// state colour; links are labelled lines whose arrowheads show their mode.
/// The view is refreshed from a snapshot; set_topology updates existing items
/// when the shape has not changed and rebuilds only when ports or links come
/// and go.
class TopologyView final : public QGraphicsView
{
    Q_OBJECT

public:
    explicit TopologyView(QWidget* parent = nullptr);

    void set_topology(const std::vector<vss::endpoint_info>& endpoints,
                      const std::vector<vss::link_info>& links);

    /// Programmatic selection; does not emit selection_changed.
    void select_endpoint(vss::endpoint_id id);
    void clear_selection();

    vss::endpoint_id selected_endpoint() const noexcept
    {
        return selected_endpoint_;
    }
    vss::link_id selected_link() const noexcept { return selected_link_; }

    int node_count() const;
    int link_count() const;
    void fit_content();

Q_SIGNALS:
    /// Emitted when the user selects a node, a link or empty canvas.  At most
    /// one of the two identifiers is non-zero.
    void selection_changed(quint64 endpoint, quint64 link);

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void wheelEvent(QWheelEvent* event) override;

private Q_SLOTS:
    void on_scene_selection_changed();

private:
    void rebuild(const std::vector<vss::endpoint_info>& endpoints,
                 const std::vector<vss::link_info>& links);
    void update_items(const std::vector<vss::endpoint_info>& endpoints,
                      const std::vector<vss::link_info>& links);
    void apply_layout();
    bool resolve_positions(const vss::link_info& link,
                           std::vector<QPointF>& sources,
                           std::vector<QPointF>& targets) const;

    QGraphicsScene* scene_ = nullptr;
    QHash<quint64, EndpointItem*> nodes_;
    QHash<quint64, LinkItem*> links_;
    vss::endpoint_id selected_endpoint_ = 0;
    vss::link_id selected_link_ = 0;
    bool updating_ = false;
    bool fitted_ = false;
};

} // namespace vss::gui

#endif // VSS_GUI_TOPOLOGY_VIEW_HPP
