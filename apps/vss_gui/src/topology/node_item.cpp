//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "topology/node_item.hpp"

#include "theme/theme.hpp"
#include "topology/topology_text.hpp"

#include <QApplication>
#include <QFontMetricsF>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QStyleOptionGraphicsItem>

#include <algorithm>
#include <utility>

namespace vss::gui::topology
{
namespace
{

QStringList badge_texts(const node_data& data)
{
    QStringList badges;
    if (data.owner)
        badges.append(topology_text::owner_badge());
    if (data.dropped)
        badges.append(topology_text::dropped_badge());
    return badges;
}

qreal badge_area(const node_data& data, const QFont& font)
{
    const QFontMetricsF metrics(font);
    qreal total = 0.0;
    const QStringList badges = badge_texts(data);
    for (const QString& badge : badges)
        total += metrics.horizontalAdvance(badge) + 12.0 + 4.0;
    if (badges.isEmpty() && data.unlinked)
        total =
            metrics.horizontalAdvance(topology_text::unlinked_badge()) + 6.0;
    return total;
}

struct node_measure
{
    QSizeF size;
};

node_measure measure(const node_data& data, bool compact,
                     const QFont& name_font, const QFont& detail_font,
                     const QFont& badge_font)
{
    const Theme& theme = Theme::instance();
    const qreal padding_x = theme.space(2);
    const qreal padding_y = theme.space(1);

    QSizeF base(compact ? 120.0 : 168.0, compact ? 48.0 : 56.0);
    if (!compact && data.large)
        base.setHeight(64.0);

    const QFontMetricsF name_metrics(name_font);
    const QFontMetricsF detail_metrics(detail_font);

    const qreal name_width = name_metrics.horizontalAdvance(data.name) +
                             badge_area(data, badge_font);
    const qreal detail_width = detail_metrics.horizontalAdvance(data.detail);
    const qreal needed_width =
        std::max(name_width, detail_width) + padding_x * 2.0;

    const qreal line_gap = 2.0;
    const qreal needed_height = name_metrics.height() + line_gap +
                                detail_metrics.height() + padding_y * 2.0;

    return {QSizeF(std::max(base.width(), needed_width),
                   std::max(base.height(), needed_height))};
}

void draw_chip(QPainter* painter, const QRectF& rect, const QString& text,
               const QColor& fill, const QColor& edge,
               const QColor& text_colour, const QFont& font)
{
    painter->setPen(QPen(edge, 1.0));
    painter->setBrush(fill);
    painter->drawRoundedRect(rect, 4.0, 4.0);
    painter->setPen(text_colour);
    painter->setFont(font);
    painter->drawText(rect, Qt::AlignCenter, text);
}

} // namespace

QSizeF node_size_for(const node_data& data, bool compact,
                     const QFont& name_font, const QFont& detail_font,
                     const QFont& badge_font)
{
    return measure(data, compact, name_font, detail_font, badge_font).size;
}

NodeItem::NodeItem(node_data data, const QSizeF& size,
                   node_item_callbacks callbacks)
    : data_(std::move(data)), size_(size), callbacks_(std::move(callbacks))
{
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
    setCursor(Qt::OpenHandCursor);
    setToolTip(data_.tooltip);
    setZValue(1.0);
}

void NodeItem::set_state(bool selected, bool focused, bool hovered)
{
    if (selected_ == selected && focused_ == focused && hovered_ == hovered)
        return;
    selected_ = selected;
    focused_ = focused;
    hovered_ = hovered;
    update();
}

void NodeItem::set_in_loop(bool value)
{
    if (in_loop_ == value)
        return;
    in_loop_ = value;
    update();
}

QRectF NodeItem::boundingRect() const
{
    return QRectF(-6.0, -6.0, size_.width() + 12.0, size_.height() + 12.0);
}

void NodeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
                     QWidget* widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);

    const Theme& theme = Theme::instance();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const QRectF body(0.0, 0.0, size_.width(), size_.height());
    const qreal radius = theme.radius(QStringLiteral("inner"));

    if (selected_ || in_loop_)
    {
        const QColor halo = in_loop_ ? theme.colour(colour_role::badSoft)
                                     : theme.colour(colour_role::accentSoft);
        painter->setPen(QPen(halo, 2.0));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(body.adjusted(-2.0, -2.0, 2.0, 2.0),
                                 radius + 2.0, radius + 2.0);
    }

    QColor edge = theme.colour(colour_role::control);
    if (in_loop_)
        edge = theme.colour(colour_role::bad);
    else if (selected_)
        edge = theme.colour(colour_role::accent);
    else if (hovered_)
        edge = theme.colour(colour_role::accentLine);

    QPen body_pen(edge, in_loop_ ? 2.0 : 1.0);
    if (data_.unlinked && !selected_ && !in_loop_)
        body_pen.setStyle(Qt::DashLine);
    painter->setPen(body_pen);
    painter->setBrush(
        theme.colour(data_.device ? colour_role::sunken : colour_role::panel));
    painter->drawRoundedRect(body, radius, radius);

    if (focused_)
    {
        painter->setPen(QPen(theme.colour(colour_role::focus), 2.0));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(body.adjusted(-4.0, -4.0, 4.0, 4.0),
                                 radius + 4.0, radius + 4.0);
    }

    const qreal padding_x = theme.space(2);
    const qreal padding_y = theme.space(1);
    const QFont name_font = theme.font(text_style::label);
    const QFont detail_font = data_.detail_mono
                                  ? theme.font(text_style::data)
                                  : theme.font(text_style::caption);
    const QFont badge_font = theme.font(text_style::caption);

    const QFontMetricsF name_metrics(name_font);
    const QFontMetricsF detail_metrics(detail_font);
    const QFontMetricsF badge_metrics(badge_font);

    const qreal name_height = name_metrics.height();
    const qreal detail_height = detail_metrics.height();
    const qreal content_height = name_height + 2.0 + detail_height;
    const qreal content_top =
        std::max(padding_y, (body.height() - content_height) / 2.0);

    const qreal badges_width = badge_area(data_, badge_font);
    const qreal name_available =
        std::max(0.0, body.width() - padding_x * 2.0 - badges_width);

    painter->setFont(name_font);
    painter->setPen(theme.colour(colour_role::ink));
    const QString elided_name =
        name_metrics.elidedText(data_.name, Qt::ElideRight, name_available);
    painter->drawText(
        QRectF(padding_x, content_top, name_available, name_height),
        Qt::AlignLeft | Qt::AlignVCenter, elided_name);

    qreal x = padding_x + name_metrics.horizontalAdvance(elided_name) + 6.0;
    const QStringList badges = badge_texts(data_);
    const qreal chip_height = std::max(18.0, badge_metrics.height() + 4.0);
    for (const QString& badge : badges)
    {
        const qreal chip_width = badge_metrics.horizontalAdvance(badge) + 12.0;
        if (x + chip_width > body.width() - padding_x)
            break;
        const bool owner = badge == topology_text::owner_badge();
        draw_chip(
            painter,
            QRectF(x, content_top + (name_height - chip_height) / 2.0,
                   chip_width, chip_height),
            badge,
            theme.colour(owner ? colour_role::okSoft : colour_role::amberSoft),
            theme.colour(owner ? colour_role::okLine : colour_role::amberLine),
            theme.colour(owner ? colour_role::ok : colour_role::amber),
            badge_font);
        x += chip_width + 4.0;
    }
    if (badges.isEmpty() && data_.unlinked)
    {
        const QString note = topology_text::unlinked_badge();
        const qreal note_width = badge_metrics.horizontalAdvance(note);
        if (x + note_width <= body.width() - padding_x)
        {
            painter->setFont(badge_font);
            painter->setPen(theme.colour(colour_role::ink3));
            painter->drawText(QRectF(x, content_top, note_width, name_height),
                              Qt::AlignLeft | Qt::AlignVCenter, note);
        }
    }

    painter->setFont(detail_font);
    painter->setPen(theme.colour(colour_role::ink3));
    const qreal detail_width = body.width() - padding_x * 2.0;
    const QString elided_detail =
        detail_metrics.elidedText(data_.detail, Qt::ElideRight, detail_width);
    painter->drawText(QRectF(padding_x, content_top + name_height + 2.0,
                             detail_width, detail_height),
                      Qt::AlignLeft | Qt::AlignVCenter, elided_detail);
}

void NodeItem::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        pressed_ = true;
        dragging_ = false;
        press_scene_ = event->scenePos();
        press_position_ = pos();
        if (callbacks_.pressed)
            callbacks_.pressed(data_.id);
        event->accept();
        return;
    }
    QGraphicsItem::mousePressEvent(event);
}

void NodeItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    if (!pressed_ || !(event->buttons() & Qt::LeftButton))
    {
        QGraphicsItem::mouseMoveEvent(event);
        return;
    }

    const QPointF delta = event->scenePos() - press_scene_;
    if (!dragging_)
    {
        if (QPointF::dotProduct(delta, delta) <
            static_cast<qreal>(QApplication::startDragDistance()) *
                QApplication::startDragDistance())
        {
            event->accept();
            return;
        }
        dragging_ = true;
        setCursor(Qt::ClosedHandCursor);
    }
    if (callbacks_.dragged)
        callbacks_.dragged(data_.id, press_position_ + delta);
    event->accept();
}

void NodeItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && pressed_)
    {
        const bool was_dragging = dragging_;
        pressed_ = false;
        dragging_ = false;
        if (was_dragging)
        {
            setCursor(Qt::OpenHandCursor);
            if (callbacks_.drag_finished)
                callbacks_.drag_finished(data_.id);
        }
        event->accept();
        return;
    }
    QGraphicsItem::mouseReleaseEvent(event);
}

void NodeItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        if (callbacks_.double_clicked)
            callbacks_.double_clicked(data_.id);
        event->accept();
        return;
    }
    QGraphicsItem::mouseDoubleClickEvent(event);
}

void NodeItem::hoverEnterEvent(QGraphicsSceneHoverEvent* event)
{
    hovered_ = true;
    update();
    if (callbacks_.hover_entered)
        callbacks_.hover_entered(data_.id);
    QGraphicsItem::hoverEnterEvent(event);
}

void NodeItem::hoverLeaveEvent(QGraphicsSceneHoverEvent* event)
{
    hovered_ = false;
    update();
    if (callbacks_.hover_left)
        callbacks_.hover_left(data_.id);
    QGraphicsItem::hoverLeaveEvent(event);
}

} // namespace vss::gui::topology
