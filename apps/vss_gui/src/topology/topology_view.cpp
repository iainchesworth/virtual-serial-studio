//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "topology/topology_view.hpp"

#include "theme/theme.hpp"
#include "topology/link_item.hpp"
#include "topology/node_item.hpp"
#include "topology/topology_text.hpp"
#include "widgets/notes.hpp"

#include <QAccessible>
#include <QContextMenuEvent>
#include <QFocusEvent>
#include <QFontMetricsF>
#include <QGraphicsScene>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QSet>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <utility>

namespace vss::gui::topology
{
namespace
{

/// The central column is about this wide at the 960 by 640 minimum window,
/// where the design asks for the compact 120 by 48 nodes.
constexpr int kCompactWidth = 500;
constexpr qreal kMinZoom = 0.25;
constexpr qreal kMaxZoom = 2.0;
constexpr qreal kFitMaxZoom = 1.25;
constexpr qreal kFitMargin = 12.0;
constexpr qreal kZoomStep = 1.25;
constexpr qreal kArrowStep = 8.0;
constexpr qreal kArrowStepLarge = 32.0;
constexpr qreal kNoteMargin = 12.0;

/// True when the two snapshots draw exactly the same graph.  Traffic and
/// state counters change many times a second; the topology does not show
/// them, so a refresh must not rebuild the scene for a counter.
bool same_graph(const std::vector<port_view>& left_ports,
                const std::vector<port_view>& right_ports,
                const std::vector<link_view>& left_links,
                const std::vector<link_view>& right_links)
{
    if (left_ports.size() != right_ports.size() ||
        left_links.size() != right_links.size())
    {
        return false;
    }
    for (std::size_t index = 0; index < left_ports.size(); ++index)
    {
        const port_view& left = left_ports[index];
        const port_view& right = right_ports[index];
        if (left.id != right.id || left.name != right.name ||
            left.path != right.path || left.kind != right.kind ||
            left.studio_only != right.studio_only ||
            (left.stats.dropped > 0) != (right.stats.dropped > 0))
        {
            return false;
        }
    }
    for (std::size_t index = 0; index < left_links.size(); ++index)
    {
        const link_view& left = left_links[index];
        const link_view& right = right_links[index];
        if (left.id != right.id || left.shape != right.shape ||
            left.mode != right.mode ||
            left.options.policy != right.options.policy ||
            left.owner != right.owner || left.sources != right.sources ||
            left.targets != right.targets)
        {
            return false;
        }
    }
    return true;
}

} // namespace

TopologyView::TopologyView(QWidget* parent) : QGraphicsView(parent)
{
    setObjectName(QStringLiteral("topologyView"));
    scene_ = new QGraphicsScene(this);
    setScene(scene_);
    setAccessibleName(topology_text::view_name());
    setAccessibleDescription(topology_text::empty_state());
    setFocusPolicy(Qt::StrongFocus);
    setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
    setFrameShape(QFrame::NoFrame);
    setDragMode(QGraphicsView::NoDrag);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setMinimumSize(240, 160);

    empty_ = new QLabel(topology_text::empty_state(), viewport());
    empty_->setObjectName(QStringLiteral("topologyEmptyState"));
    empty_->setAccessibleName(topology_text::empty_state());
    empty_->setAlignment(Qt::AlignCenter);
    empty_->setWordWrap(true);
    empty_->setFont(Theme::instance().font(text_style::body));
    empty_->setProperty("muted", true);
    empty_->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    hint_ = new QLabel(topology_text::hint(), viewport());
    hint_->setObjectName(QStringLiteral("topologyHint"));
    hint_->setAccessibleName(topology_text::hint());
    hint_->setFont(Theme::instance().font(text_style::caption));
    hint_->setProperty("muted", true);
    hint_->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    viewport()->installEventFilter(this);
    connect(&Theme::instance(), &Theme::themeChanged, this,
            [this]
            {
                empty_->setFont(Theme::instance().font(text_style::body));
                hint_->setFont(Theme::instance().font(text_style::caption));
                scene_->update();
                viewport()->update();
                update_overlays();
            });
    update_accessibility();
    update_overlays();
}

TopologyView::~TopologyView() = default;

void TopologyView::set_capabilities(const capabilities& caps)
{
    caps_ = caps;
}

void TopologyView::set_topology(const std::vector<port_view>& ports,
                                const std::vector<link_view>& links)
{
    if (same_graph(ports_, ports, links_, links))
        return;

    ports_ = ports;
    links_ = links;

    names_.clear();
    node_ids_.clear();
    link_inputs_.clear();

    for (const auto& port : ports_)
    {
        names_.insert(port.id, port.name);
        node_ids_.append(port.id);
    }
    std::sort(node_ids_.begin(), node_ids_.end());

    for (const auto& link : links_)
    {
        topo_link_input input;
        input.id = link.id;
        input.shape = link.shape;
        input.sources = link.sources;
        input.targets = link.targets;
        input.arrow = link.shape == link_shape::one_way ||
                      link.shape == link_shape::monitor;
        link_inputs_.append(input);
    }
    std::sort(link_inputs_.begin(), link_inputs_.end(),
              [](const topo_link_input& left, const topo_link_input& right)
              { return left.id < right.id; });

    const int available =
        viewport()->width() > 0 ? viewport()->width() : width();
    compact_ = available < kCompactWidth;

    rebuild_items();
    update_accessibility();
    update_overlays();

    if (ports_.empty())
    {
        resetTransform();
        user_zoomed_ = false;
    }
    else if (!user_zoomed_)
    {
        fit_content();
    }
}

void TopologyView::rebuild_items()
{
    scene_->clear();
    node_items_.clear();
    link_items_.clear();
    node_order_.clear();
    link_order_.clear();
    node_sizes_.clear();
    base_positions_.clear();

    QSet<vss::endpoint_id> linked;
    QSet<vss::endpoint_id> owners;
    QSet<vss::endpoint_id> join_targets;
    for (const auto& link : links_)
    {
        for (const vss::endpoint_id id : link.sources)
            linked.insert(id);
        for (const vss::endpoint_id id : link.targets)
            linked.insert(id);
        if (link.owner != 0)
            owners.insert(link.owner);
        if (link.shape == link_shape::join)
        {
            for (const vss::endpoint_id id : link.targets)
                join_targets.insert(id);
        }
    }

    const QFont name_font = Theme::instance().font(text_style::label);
    const QFont badge_font = Theme::instance().font(text_style::caption);

    QHash<vss::endpoint_id, node_data> data_by_id;
    QVector<topo_node_input> inputs;
    inputs.reserve(static_cast<qsizetype>(ports_.size()));
    for (const auto& port : ports_)
    {
        node_data node;
        node.id = port.id;
        node.name = port.name;
        node.detail = topology_text::node_detail(port);
        node.detail_mono = topology_text::node_detail_is_mono(port);
        node.device = port.kind == vss::endpoint_kind::physical;
        node.large = node.device || join_targets.contains(port.id);
        node.unlinked = !linked.contains(port.id);
        node.owner = owners.contains(port.id);
        node.dropped = port.stats.dropped > 0;
        node.tooltip = topology_text::node_tooltip(port, node.unlinked);

        const QFont detail_font =
            node.detail_mono ? Theme::instance().font(text_style::data)
                             : Theme::instance().font(text_style::caption);
        const QSizeF size =
            node_size_for(node, compact_, name_font, detail_font, badge_font);

        data_by_id.insert(port.id, node);
        node_sizes_.insert(port.id, size);
        inputs.append({port.id, size});
    }

    layout_ = compute_topology_layout(inputs, link_inputs_);

    for (auto it = overrides_.begin(); it != overrides_.end();)
    {
        if (!node_sizes_.contains(it.key()))
            it = overrides_.erase(it);
        else
            ++it;
    }

    for (const auto& placement : layout_.nodes)
    {
        const auto found = data_by_id.constFind(placement.id);
        if (found == data_by_id.constEnd())
            continue;

        node_item_callbacks callbacks;
        callbacks.pressed = [this](vss::endpoint_id id)
        {
            setFocus(Qt::MouseFocusReason);
            const qsizetype index = focus_index_of_port(id);
            if (index >= 0)
                focus_at(index, true);
        };
        callbacks.double_clicked = [this](vss::endpoint_id id)
        {
            setFocus(Qt::MouseFocusReason);
            const qsizetype index = focus_index_of_port(id);
            if (index >= 0)
                focus_at(index, true);
            Q_EMIT inspect_requested(id);
        };
        callbacks.hover_entered = [](vss::endpoint_id) {};
        callbacks.hover_left = [](vss::endpoint_id) {};

        auto* item = new NodeItem(found.value(), placement.rect.size(),
                                  std::move(callbacks));
        item->setPos(placement.rect.topLeft());
        scene_->addItem(item);
        node_items_.insert(placement.id, item);
        node_order_.append(placement.id);
        base_positions_.insert(placement.id, placement.rect.topLeft());
    }

    QHash<vss::link_id, const link_view*> views;
    for (const auto& link : links_)
        views.insert(link.id, &link);

    for (const auto& placement : layout_.links)
    {
        const auto view = views.constFind(placement.id);
        if (view == views.constEnd())
            continue;

        link_data link;
        link.id = view.value()->id;
        link.shape = view.value()->shape;
        link.primary = topology_text::link_primary_label(link.shape);
        link.secondary = topology_text::link_secondary_label(*view.value());
        link.accessible_name = topology_text::link_name(*view.value(), names_);
        link.dashed = link.shape == link_shape::monitor;
        link.arrow = link.shape == link_shape::one_way ||
                     link.shape == link_shape::monitor;

        link_item_callbacks callbacks;
        callbacks.pressed = [this](vss::link_id id)
        {
            setFocus(Qt::MouseFocusReason);
            const qsizetype index = focus_index_of_link(id);
            if (index >= 0)
                focus_at(index, true);
        };
        callbacks.hover_entered = [](vss::link_id) {};
        callbacks.hover_left = [](vss::link_id) {};

        auto* item = new LinkItem(std::move(link), std::move(callbacks));
        item->set_geometry(placement);
        scene_->addItem(item);
        link_items_.insert(placement.id, item);
        link_order_.append(placement.id);
    }

    if (selected_port_ != 0 && !node_items_.contains(selected_port_))
        selected_port_ = 0;
    if (selected_link_ != 0 && !link_items_.contains(selected_link_))
        selected_link_ = 0;
    if (focused_port_ != 0 && !node_items_.contains(focused_port_))
        focused_port_ = 0;
    if (focused_link_ != 0 && !link_items_.contains(focused_link_))
        focused_link_ = 0;
    if (focused_port_ != 0)
        focus_index_ = focus_index_of_port(focused_port_);
    else if (focused_link_ != 0)
        focus_index_ = focus_index_of_link(focused_link_);
    else
        focus_index_ = -1;

    apply_positions();
    refresh_focus_visuals();

    if (!loop_links_.isEmpty())
    {
        QSet<vss::endpoint_id> members;
        for (const vss::endpoint_id id : loop_nodes_)
            members.insert(id);
        for (const vss::endpoint_id id : node_order_)
            node_items_.value(id)->set_in_loop(members.contains(id));
        for (const vss::link_id id : link_order_)
            link_items_.value(id)->set_in_loop(loop_links_.contains(id));
    }
}

void TopologyView::apply_positions()
{
    QHash<vss::endpoint_id, QRectF> rects;
    for (const auto& placement : layout_.nodes)
    {
        auto* item = node_items_.value(placement.id);
        if (item == nullptr)
            continue;
        const QPointF base = base_positions_.value(placement.id);
        const QPointF delta = overrides_.value(placement.id);
        const QRectF rect(base + delta, node_sizes_.value(placement.id));
        item->setPos(rect.topLeft());
        rects.insert(placement.id, rect);
    }

    const QVector<topo_link_placement> geometry =
        compute_link_geometry(link_inputs_, rects);
    for (const auto& placement : geometry)
    {
        if (LinkItem* item = link_items_.value(placement.id))
            item->set_geometry(placement);
    }

    update_scene_rect();
}

void TopologyView::update_scene_rect()
{
    QRectF content;
    for (const auto& placement : layout_.nodes)
    {
        if (auto* item = node_items_.value(placement.id))
        {
            const QRectF rect(item->pos(), node_sizes_.value(placement.id));
            content = content.isNull() ? rect : content.united(rect);
        }
    }
    for (const vss::link_id id : link_order_)
    {
        auto* item = link_items_.value(id);
        if (item == nullptr)
            continue;
        const QRectF rect = item->boundingRect();
        if (rect.isValid())
            content = content.isNull() ? rect : content.united(rect);
    }

    if (content.isNull())
    {
        setSceneRect(QRectF(0.0, 0.0, std::max(1, viewport()->width()),
                            std::max(1, viewport()->height())));
        return;
    }
    setSceneRect(content.adjusted(-16.0, -16.0, 16.0, 16.0));
}

void TopologyView::update_overlays()
{
    const bool has_ports = !ports_.empty();
    const int view_width = std::max(1, viewport()->width());
    const int view_height = std::max(1, viewport()->height());

    empty_->setVisible(!has_ports);
    empty_->setFixedWidth(std::max(120, std::min(360, view_width - 48)));
    empty_->adjustSize();
    empty_->move((view_width - empty_->width()) / 2,
                 (view_height - empty_->height()) / 2);

    hint_->adjustSize();
    hint_->setVisible(view_width > hint_->width() + 24);
    hint_->move(static_cast<int>(kNoteMargin),
                view_height - hint_->height() - static_cast<int>(kNoteMargin));

    if (loop_note_ != nullptr && loop_note_->isVisible())
    {
        const int width =
            std::min(640, view_width - 2 * static_cast<int>(kNoteMargin));
        if (width > 160)
        {
            loop_note_->setFixedWidth(width);
            loop_note_->adjustSize();
            loop_note_->move((view_width - loop_note_->width()) / 2,
                             static_cast<int>(kNoteMargin));
        }
    }

    empty_->raise();
    hint_->raise();
    if (loop_note_ != nullptr)
        loop_note_->raise();
}

void TopologyView::update_accessibility()
{
    const QString next = topology_text::summary(ports_, links_);
    if (next == summary_)
        return;
    summary_ = next;
    setAccessibleDescription(summary_);
    QAccessibleEvent event(this, QAccessible::DescriptionChanged);
    QAccessible::updateAccessibility(&event);
}

void TopologyView::refresh_focus_visuals()
{
    for (const vss::endpoint_id id : node_order_)
    {
        auto* item = node_items_.value(id);
        item->set_state(id == selected_port_,
                        show_focus_ && id == focused_port_, item->is_hovered());
    }
    for (const vss::link_id id : link_order_)
    {
        auto* item = link_items_.value(id);
        item->set_state(id == selected_link_,
                        show_focus_ && id == focused_link_, item->is_hovered());
    }
}

bool TopologyView::select_port(vss::endpoint_id id)
{
    const bool port_changed = selected_port_ != id;
    const bool link_cleared = selected_link_ != 0;
    selected_port_ = id;
    selected_link_ = 0;
    if (!port_changed && !link_cleared)
        return false;

    refresh_focus_visuals();
    if (port_changed)
        Q_EMIT port_selected(id);
    if (link_cleared)
        Q_EMIT link_selected(0);
    return true;
}

bool TopologyView::select_link(vss::link_id id)
{
    const bool link_changed = selected_link_ != id;
    const bool port_cleared = selected_port_ != 0;
    selected_link_ = id;
    selected_port_ = 0;
    if (!link_changed && !port_cleared)
        return false;

    refresh_focus_visuals();
    if (link_changed)
        Q_EMIT link_selected(id);
    if (port_cleared)
        Q_EMIT port_selected(0);
    return true;
}

void TopologyView::set_selection(vss::endpoint_id port, vss::link_id link)
{
    selected_port_ = node_items_.contains(port) ? port : 0;
    selected_link_ = link_items_.contains(link) ? link : 0;
    refresh_focus_visuals();
}

qsizetype TopologyView::focus_index_of_port(vss::endpoint_id id) const
{
    return node_order_.indexOf(id);
}

qsizetype TopologyView::focus_index_of_link(vss::link_id id) const
{
    const qsizetype index = link_order_.indexOf(id);
    return index < 0 ? -1 : node_order_.size() + index;
}

void TopologyView::focus_at(qsizetype index, bool select)
{
    const qsizetype node_total = node_order_.size();
    if (index < 0 || index >= node_total + link_order_.size())
    {
        clear_focus();
        return;
    }

    focus_index_ = index;
    focused_port_ = index < node_total ? node_order_.at(index) : 0;
    focused_link_ = index < node_total ? 0 : link_order_.at(index - node_total);
    show_focus_ = hasFocus();

    if (select)
    {
        if (focused_port_ != 0)
            select_port(focused_port_);
        else
            select_link(focused_link_);
    }

    refresh_focus_visuals();

    const QRectF rect = focused_port_ != 0 ? node_rect(focused_port_)
                                           : link_bounds(focused_link_);
    if (rect.isValid())
        ensureVisible(rect.adjusted(-16.0, -16.0, 16.0, 16.0));
}

void TopologyView::clear_focus()
{
    focused_port_ = 0;
    focused_link_ = 0;
    focus_index_ = -1;
    refresh_focus_visuals();
}

void TopologyView::nudge_focused(qreal dx, qreal dy)
{
    if (focused_port_ == 0)
        return;
    overrides_[focused_port_] += QPointF(dx, dy);
    apply_positions();
    const QRectF rect = node_rect(focused_port_);
    if (rect.isValid())
        ensureVisible(rect.adjusted(-16.0, -16.0, 16.0, 16.0));
}

bool TopologyView::focusNextPrevChild(bool next)
{
    const qsizetype total = node_order_.size() + link_order_.size();
    if (total == 0)
        return QGraphicsView::focusNextPrevChild(next);

    qsizetype target = 0;
    if (focus_index_ < 0)
        target = next ? 0 : total - 1;
    else
        target = focus_index_ + (next ? 1 : -1);

    if (target < 0 || target >= total)
    {
        clear_focus();
        return QGraphicsView::focusNextPrevChild(next);
    }

    focus_at(target, true);
    return true;
}

void TopologyView::keyPressEvent(QKeyEvent* event)
{
    const bool control = event->modifiers().testFlag(Qt::ControlModifier);
    const bool shift = event->modifiers().testFlag(Qt::ShiftModifier);

    switch (event->key())
    {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (focused_port_ != 0)
        {
            Q_EMIT inspect_requested(focused_port_);
            event->accept();
            return;
        }
        if (focused_link_ != 0)
        {
            Q_EMIT status_message(
                topology_text::selected(link_description(focused_link_)));
            event->accept();
            return;
        }
        break;
    case Qt::Key_Delete:
        if (focused_port_ != 0)
            Q_EMIT remove_port_requested(focused_port_);
        else if (focused_link_ != 0)
            Q_EMIT remove_link_requested(focused_link_);
        else if (selected_port_ != 0)
            Q_EMIT remove_port_requested(selected_port_);
        else if (selected_link_ != 0)
            Q_EMIT remove_link_requested(selected_link_);
        else
            break;
        event->accept();
        return;
    case Qt::Key_F10:
        if (shift)
        {
            open_focused_menu();
            event->accept();
            return;
        }
        break;
    case Qt::Key_Menu:
        open_focused_menu();
        event->accept();
        return;
    case Qt::Key_Escape:
        if (loop_note_ != nullptr && loop_note_->isVisible())
        {
            dismiss_loop_note();
            event->accept();
            return;
        }
        if (selected_port_ != 0 || selected_link_ != 0)
        {
            select_port(0);
            select_link(0);
            event->accept();
            return;
        }
        break;
    case Qt::Key_0:
        if (control)
        {
            fit_content();
            event->accept();
            return;
        }
        break;
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        if (control)
        {
            zoom_in();
            event->accept();
            return;
        }
        break;
    case Qt::Key_Minus:
        if (control)
        {
            zoom_out();
            event->accept();
            return;
        }
        break;
    case Qt::Key_Left:
        if (!control && focused_port_ != 0)
        {
            nudge_focused(-(shift ? kArrowStepLarge : kArrowStep), 0.0);
            event->accept();
            return;
        }
        break;
    case Qt::Key_Right:
        if (!control && focused_port_ != 0)
        {
            nudge_focused(shift ? kArrowStepLarge : kArrowStep, 0.0);
            event->accept();
            return;
        }
        break;
    case Qt::Key_Up:
        if (!control && focused_port_ != 0)
        {
            nudge_focused(0.0, -(shift ? kArrowStepLarge : kArrowStep));
            event->accept();
            return;
        }
        break;
    case Qt::Key_Down:
        if (!control && focused_port_ != 0)
        {
            nudge_focused(0.0, shift ? kArrowStepLarge : kArrowStep);
            event->accept();
            return;
        }
        break;
    default:
        break;
    }

    QGraphicsView::keyPressEvent(event);
}

void TopologyView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton &&
        itemAt(event->position().toPoint()) == nullptr)
    {
        setFocus(Qt::MouseFocusReason);
        clear_focus();
        select_port(0);
        select_link(0);
    }
    QGraphicsView::mousePressEvent(event);
}

void TopologyView::resizeEvent(QResizeEvent* event)
{
    QGraphicsView::resizeEvent(event);

    const bool compact_now = viewport()->width() < kCompactWidth;
    if (compact_now != compact_ && !ports_.empty())
    {
        compact_ = compact_now;
        rebuild_items();
    }

    update_overlays();
    if (!fitting_ && !user_zoomed_ && !ports_.empty())
        fit_content();
}

void TopologyView::focusInEvent(QFocusEvent* event)
{
    QGraphicsView::focusInEvent(event);
    show_focus_ = true;

    const bool keyboard_entry = event->reason() == Qt::TabFocusReason ||
                                event->reason() == Qt::BacktabFocusReason ||
                                event->reason() == Qt::ShortcutFocusReason;
    if (keyboard_entry && focus_index_ < 0 &&
        (!node_order_.isEmpty() || !link_order_.isEmpty()))
    {
        const qsizetype total = node_order_.size() + link_order_.size();
        focus_at(event->reason() == Qt::BacktabFocusReason ? total - 1 : 0,
                 true);
    }
    refresh_focus_visuals();
}

void TopologyView::focusOutEvent(QFocusEvent* event)
{
    show_focus_ = false;
    refresh_focus_visuals();
    QGraphicsView::focusOutEvent(event);
}

void TopologyView::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers().testFlag(Qt::ControlModifier))
    {
        const int delta = event->angleDelta().y();
        if (delta != 0)
        {
            scale_by(std::pow(1.0015, static_cast<qreal>(delta)),
                     QGraphicsView::AnchorUnderMouse, true);
        }
        event->accept();
        return;
    }
    QGraphicsView::wheelEvent(event);
}

void TopologyView::drawBackground(QPainter* painter, const QRectF& rect)
{
    painter->fillRect(rect, Theme::instance().colour(colour_role::panel));
}

bool TopologyView::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == viewport() && event->type() == QEvent::ContextMenu)
    {
        auto* context = static_cast<QContextMenuEvent*>(event);
        open_context_menu(context->pos());
        return true;
    }
    return QGraphicsView::eventFilter(watched, event);
}

void TopologyView::open_focused_menu()
{
    if (focused_port_ != 0)
    {
        const QPoint position = mapFromScene(node_rect(focused_port_).center());
        open_context_menu(position);
        return;
    }
    if (focused_link_ != 0)
    {
        const QPoint position = mapFromScene(link_anchor(focused_link_));
        open_context_menu(position);
    }
}

void TopologyView::open_context_menu(const QPoint& viewport_position)
{
    QGraphicsItem* item = itemAt(viewport_position);
    if (item == nullptr)
        return;

    const QPoint global = viewport()->mapToGlobal(viewport_position);
    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->setAccessibleName(topology_text::view_name());

    if (auto* node = dynamic_cast<NodeItem*>(item))
    {
        const vss::endpoint_id id = node->id();
        const qsizetype index = focus_index_of_port(id);
        if (index >= 0)
            focus_at(index, true);

        menu->addAction(topology_text::inspect_action(), this,
                        [this, id] { Q_EMIT inspect_requested(id); });
        menu->addAction(topology_text::copy_name_action(), this,
                        [this, id] { Q_EMIT copy_name_requested(id); });
        if (caps_.rename_port)
        {
            menu->addAction(topology_text::rename_action(), this,
                            [this, id] { Q_EMIT rename_requested(id); });
        }
        QAction* connect_action =
            menu->addAction(topology_text::connect_from_here_action(), this,
                            [this, id] { Q_EMIT connect_requested(id); });
        connect_action->setEnabled(node_order_.size() >= 2);
        if (!connect_action->isEnabled())
            connect_action->setToolTip(topology_text::connect_needs_two());
        menu->addSeparator();
        menu->addAction(topology_text::remove_action(), this,
                        [this, id] { Q_EMIT remove_port_requested(id); });
    }
    else if (auto* link = dynamic_cast<LinkItem*>(item))
    {
        const vss::link_id id = link->id();
        const qsizetype index = focus_index_of_link(id);
        if (index >= 0)
            focus_at(index, true);
        menu->addAction(topology_text::remove_link_action(), this,
                        [this, id] { Q_EMIT remove_link_requested(id); });
    }
    else
    {
        delete menu;
        return;
    }

    menu->popup(global);
}

QStringList TopologyView::node_menu_labels(vss::endpoint_id id) const
{
    if (!node_items_.contains(id))
        return {};
    QStringList labels;
    labels << topology_text::inspect_action()
           << topology_text::copy_name_action();
    if (caps_.rename_port)
        labels << topology_text::rename_action();
    labels << topology_text::connect_from_here_action()
           << topology_text::remove_action();
    return labels;
}

QStringList TopologyView::link_menu_labels(vss::link_id id) const
{
    if (!link_items_.contains(id))
        return {};
    return {topology_text::remove_link_action()};
}

void TopologyView::fit_content()
{
    if (ports_.empty())
    {
        resetTransform();
        return;
    }

    const QRectF content = scene_->itemsBoundingRect();
    if (content.isNull())
        return;

    const QRectF available =
        QRectF(viewport()->rect())
            .adjusted(kFitMargin, kFitMargin, -kFitMargin, -kFitMargin);
    if (available.width() <= 0.0 || available.height() <= 0.0)
        return;

    qreal scale_factor = std::min(available.width() / content.width(),
                                  available.height() / content.height());
    scale_factor = std::clamp(scale_factor, kMinZoom, kFitMaxZoom);

    fitting_ = true;
    resetTransform();
    scale(scale_factor, scale_factor);
    centerOn(content.center());
    fitting_ = false;
    user_zoomed_ = false;
}

void TopologyView::scale_by(qreal factor, QGraphicsView::ViewportAnchor anchor,
                            bool user_action)
{
    const qreal current = transform().m11();
    if (current <= 0.0)
        return;
    const qreal target = std::clamp(current * factor, kMinZoom, kMaxZoom);
    if (qFuzzyCompare(current, target))
        return;

    const QGraphicsView::ViewportAnchor previous = transformationAnchor();
    setTransformationAnchor(anchor);
    scale(target / current, target / current);
    setTransformationAnchor(previous);
    if (user_action)
        user_zoomed_ = true;
}

void TopologyView::zoom_in()
{
    scale_by(kZoomStep, QGraphicsView::AnchorViewCenter, true);
}

void TopologyView::zoom_out()
{
    scale_by(1.0 / kZoomStep, QGraphicsView::AnchorViewCenter, true);
}

qreal TopologyView::zoom() const
{
    return transform().m11();
}

void TopologyView::centre_on_links(const QVector<vss::link_id>& ids)
{
    QRectF bounds;
    for (const vss::link_id id : ids)
    {
        const QRectF rect = link_bounds(id);
        if (!rect.isValid())
            continue;
        bounds = bounds.isNull() ? rect : bounds.united(rect);
    }
    if (bounds.isNull())
        return;

    const QRectF available =
        QRectF(viewport()->rect()).adjusted(24.0, 24.0, -24.0, -24.0);
    qreal scale_factor = std::min(available.width() / bounds.width(),
                                  available.height() / bounds.height());
    scale_factor = std::clamp(scale_factor, 0.3, kFitMaxZoom);

    fitting_ = true;
    resetTransform();
    scale(scale_factor, scale_factor);
    centerOn(bounds.center());
    fitting_ = false;
    user_zoomed_ = true;
}

bool TopologyView::show_loop_note()
{
    const QVector<vss::endpoint_id> cycle = find_cycle(link_inputs_, node_ids_);
    if (cycle.isEmpty())
    {
        dismiss_loop_note();
        return false;
    }

    loop_nodes_ = cycle;
    loop_links_ = cycle_links(link_inputs_, cycle);

    QSet<vss::endpoint_id> members;
    for (const vss::endpoint_id id : cycle)
        members.insert(id);
    for (const vss::endpoint_id id : node_order_)
        node_items_.value(id)->set_in_loop(members.contains(id));
    for (const vss::link_id id : link_order_)
        link_items_.value(id)->set_in_loop(loop_links_.contains(id));

    if (loop_note_ == nullptr)
    {
        loop_note_ = new NoteBanner(NoteBanner::kind::danger,
                                    topology_text::loop_note(), this);
        loop_note_->setObjectName(QStringLiteral("topologyLoopNote"));
        loop_note_->set_action(topology_text::show_loop(),
                               [this] { centre_on_links(loop_links_); });
    }
    loop_note_->set_text(topology_text::loop_note());
    loop_note_->show();
    loop_note_->raise();
    update_overlays();
    return true;
}

void TopologyView::dismiss_loop_note()
{
    loop_links_.clear();
    loop_nodes_.clear();
    if (loop_note_ != nullptr)
        loop_note_->hide();
    for (const vss::endpoint_id id : node_order_)
        node_items_.value(id)->set_in_loop(false);
    for (const vss::link_id id : link_order_)
        link_items_.value(id)->set_in_loop(false);
}

QString TopologyView::loop_note_text() const
{
    return loop_note_ == nullptr ? QString() : loop_note_->text();
}

int TopologyView::loop_link_count() const
{
    return static_cast<int>(loop_links_.size());
}

int TopologyView::node_count() const
{
    return static_cast<int>(node_items_.size());
}

int TopologyView::link_count() const
{
    return static_cast<int>(link_items_.size());
}

QRectF TopologyView::node_rect(vss::endpoint_id id) const
{
    NodeItem* item = node_items_.value(id);
    if (item == nullptr)
        return {};
    return QRectF(item->pos(), node_sizes_.value(id));
}

QSizeF TopologyView::node_size(vss::endpoint_id id) const
{
    return node_sizes_.value(id);
}

QPointF TopologyView::link_anchor(vss::link_id id) const
{
    LinkItem* item = link_items_.value(id);
    return item == nullptr ? QPointF() : item->geometry().primary_anchor;
}

QRectF TopologyView::link_bounds(vss::link_id id) const
{
    LinkItem* item = link_items_.value(id);
    return item == nullptr ? QRectF() : item->boundingRect();
}

QString TopologyView::link_description(vss::link_id id) const
{
    for (const auto& link : links_)
    {
        if (link.id == id)
            return topology_text::link_name(link, names_);
    }
    return {};
}

QStringList TopologyView::link_descriptions() const
{
    QStringList result;
    for (const vss::link_id id : link_order_)
        result.append(link_description(id));
    return result;
}

bool TopologyView::node_is_unlinked(vss::endpoint_id id) const
{
    NodeItem* item = node_items_.value(id);
    return item != nullptr && item->data().unlinked;
}

bool TopologyView::node_is_device(vss::endpoint_id id) const
{
    NodeItem* item = node_items_.value(id);
    return item != nullptr && item->data().device;
}

QStringList TopologyView::node_badges(vss::endpoint_id id) const
{
    QStringList result;
    NodeItem* item = node_items_.value(id);
    if (item == nullptr)
        return result;
    if (item->data().owner)
        result.append(topology_text::owner_badge());
    if (item->data().dropped)
        result.append(topology_text::dropped_badge());
    if (item->data().unlinked)
        result.append(topology_text::unlinked_badge());
    return result;
}

} // namespace vss::gui::topology
