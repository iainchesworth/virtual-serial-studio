//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_TOPOLOGY_NODE_ITEM_HPP
#define VSS_GUI_TOPOLOGY_NODE_ITEM_HPP

#include <vss/core/types.hpp>

#include <QFont>
#include <QGraphicsItem>
#include <QRectF>
#include <QSizeF>
#include <QString>

#include <functional>

namespace vss::gui::topology
{

/// What one node draws: a name, one detail line and optional badges.
struct node_data
{
    vss::endpoint_id id = 0;
    QString name;
    /// The mono path, or a short role sentence such as "Real device, COM3".
    QString detail;
    bool detail_mono = true;
    /// A real device uses the sunken fill.
    bool device = false;
    /// A real device or the target of a join uses the taller 64 px box.
    bool large = false;
    bool unlinked = false;
    bool owner = false;
    bool dropped = false;
    QString tooltip;
};

/// The node size the design specifies, grown when the text needs more room.
///
/// 168 by 56 is the comfortable size and 120 by 48 the compact one; a real
/// device or the target of a join is 168 by 64.  At a larger text size the box
/// grows instead of clipping, because the specification forbids clipping at
/// 200 percent text.
QSizeF node_size_for(const node_data& data, bool compact,
                     const QFont& name_font, const QFont& detail_font,
                     const QFont& badge_font);

struct node_item_callbacks
{
    std::function<void(vss::endpoint_id)> pressed;
    std::function<void(vss::endpoint_id)> double_clicked;
    std::function<void(vss::endpoint_id)> hover_entered;
    std::function<void(vss::endpoint_id)> hover_left;
};

/// One port drawn as a card on the topology scene.
class NodeItem final : public QGraphicsItem
{
public:
    NodeItem(node_data data, const QSizeF& size, node_item_callbacks callbacks);

    vss::endpoint_id id() const noexcept { return data_.id; }
    const node_data& data() const noexcept { return data_; }

    void set_state(bool selected, bool focused, bool hovered);
    bool is_selected() const noexcept { return selected_; }
    bool is_focused() const noexcept { return focused_; }
    bool is_hovered() const noexcept { return hovered_; }

    /// Part of a loop the user asked to see.
    void set_in_loop(bool value);
    bool in_loop() const noexcept { return in_loop_; }

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
               QWidget* widget) override;

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;

private:
    node_data data_;
    QSizeF size_;
    node_item_callbacks callbacks_;
    bool selected_ = false;
    bool focused_ = false;
    bool hovered_ = false;
    bool in_loop_ = false;
};

} // namespace vss::gui::topology

#endif // VSS_GUI_TOPOLOGY_NODE_ITEM_HPP
