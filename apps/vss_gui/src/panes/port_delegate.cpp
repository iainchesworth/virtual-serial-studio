//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "panes/port_delegate.hpp"

#include "panes/port_model.hpp"
#include "theme/icon_provider.hpp"
#include "theme/theme.hpp"

#include <QApplication>
#include <QEvent>
#include <QFontMetrics>
#include <QIcon>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionViewItem>

#include <algorithm>

namespace vss::gui
{
namespace
{

QFont semibold(QFont font)
{
    font.setWeight(QFont::DemiBold);
    return font;
}

struct chip_style
{
    QColor fill;
    QColor edge;
    QColor text;
    QColor mark;
    bool hollow = false;
    bool cross = false;
};

chip_style style_for(port_state_kind kind)
{
    const Theme& theme = Theme::instance();
    switch (kind)
    {
    case port_state_kind::open:
    case port_state_kind::connected:
        return {theme.colour(colour_role::okSoft),
                theme.colour(colour_role::okLine),
                theme.colour(colour_role::noteOkText),
                theme.colour(colour_role::ok),
                false,
                false};
    case port_state_kind::waiting:
        return {theme.colour(colour_role::amberSoft),
                theme.colour(colour_role::amberLine),
                theme.colour(colour_role::noteWarnText),
                theme.colour(colour_role::amber),
                true,
                false};
    case port_state_kind::studio_only:
    case port_state_kind::closed:
        return {theme.colour(colour_role::sunken),
                theme.colour(colour_role::line),
                theme.colour(colour_role::ink2),
                theme.colour(colour_role::ink3),
                true,
                false};
    case port_state_kind::failed:
        return {theme.colour(colour_role::badSoft),
                theme.colour(colour_role::badLine),
                theme.colour(colour_role::noteDangerText),
                theme.colour(colour_role::bad),
                false,
                true};
    case port_state_kind::fixture:
        return {theme.colour(colour_role::accentSoft),
                theme.colour(colour_role::accentLine),
                theme.colour(colour_role::noteInfoText),
                theme.colour(colour_role::accentStrong),
                true,
                false};
    }
    return {};
}

int chip_width(const QString& text)
{
    const QFont font = Theme::instance().font(text_style::caption);
    QFont bold = font;
    bold.setWeight(QFont::DemiBold);
    const QFontMetrics metrics(bold);
    return metrics.horizontalAdvance(text) + 26;
}

void paint_chip(QPainter* painter, const QRect& rect, const QString& text,
                port_state_kind kind)
{
    const chip_style style = style_for(kind);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(QPen(style.edge, 1.0));
    painter->setBrush(style.fill);
    painter->drawRoundedRect(QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5),
                             rect.height() / 2.0, rect.height() / 2.0);

    QFont font = Theme::instance().font(text_style::caption);
    font.setWeight(QFont::DemiBold);
    painter->setFont(font);
    painter->setPen(style.text);
    const int dot_space = 14;
    const QRect text_rect = rect.adjusted(dot_space, 0, -6, 0);
    painter->drawText(text_rect, Qt::AlignVCenter | Qt::AlignLeft, text);

    const QPointF centre(rect.left() + 8.0, rect.center().y() + 0.5);
    painter->setPen(QPen(style.mark, 1.2));
    painter->setBrush(style.hollow ? Qt::NoBrush : style.mark);
    if (style.cross)
    {
        const qreal arm = 3.0;
        painter->drawLine(QPointF(centre.x() - arm, centre.y() - arm),
                          QPointF(centre.x() + arm, centre.y() + arm));
        painter->drawLine(QPointF(centre.x() - arm, centre.y() + arm),
                          QPointF(centre.x() + arm, centre.y() - arm));
    }
    else
    {
        painter->drawEllipse(centre, 3.0, 3.0);
    }
    painter->restore();
}

} // namespace

PortDelegate::PortDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

void PortDelegate::set_compact(bool compact)
{
    compact_ = compact;
}

QRect PortDelegate::copy_rect_for(const QStyleOptionViewItem& option,
                                  const QModelIndex& index) const
{
    if (index.column() != 0)
        return {};
    const QString path = index.data(PortModel::path_role).toString();
    const QFont mono = Theme::instance().font(text_style::data);
    const QFontMetrics metrics(mono);
    const int left = option.rect.left() + 12;
    const int top = table_ ? option.rect.top() + 20
                           : option.rect.bottom() - (compact_ ? 28 : 30);
    return QRect(left + metrics.horizontalAdvance(path) + 4, top, 24, 24);
}

void PortDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                         const QModelIndex& index) const
{
    const Theme& theme = Theme::instance();
    const bool selected = (option.state & QStyle::State_Selected) != 0;
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->fillRect(option.rect, selected
                                       ? theme.colour(colour_role::accentSoft)
                                       : theme.colour(colour_role::panel));

    if (table_ && index.column() > 0)
    {
        const QString text = index.data(Qt::DisplayRole).toString();
        if (index.column() == 1)
        {
            const auto kind = static_cast<port_state_kind>(
                index.data(PortModel::state_kind_role).toInt());
            const int width = chip_width(text);
            paint_chip(painter,
                       QRect(option.rect.left() + 4,
                             option.rect.center().y() - 10, width, 20),
                       text, kind);
        }
        else
        {
            painter->setFont(theme.font(text_style::data));
            painter->setPen(theme.colour(colour_role::ink2));
            painter->drawText(option.rect.adjusted(6, 0, -8, 0),
                              Qt::AlignRight | Qt::AlignVCenter, text);
        }
        painter->restore();
        return;
    }

    const QString name = index.data(PortModel::name_role).toString();
    const QString path = index.data(PortModel::path_role).toString();
    const QString role_text = index.data(PortModel::role_text_role).toString();
    const QString state = index.data(PortModel::state_text_role).toString();
    const auto kind = static_cast<port_state_kind>(
        index.data(PortModel::state_kind_role).toInt());

    const int left = option.rect.left() + 12;
    const int right = option.rect.right() - 12;
    const int top = option.rect.top() + (compact_ ? 6 : 8);
    const QFont name_font = semibold(
        theme.font(compact_ ? text_style::body : text_style::bodyLarge));
    painter->setFont(name_font);
    painter->setPen(theme.colour(colour_role::ink));
    const int name_width =
        table_ ? std::max(0, right - left)
               : std::max(0, right - left - chip_width(state) - 8);
    painter->drawText(
        QRect(left, top, name_width, 20), Qt::AlignLeft | Qt::AlignVCenter,
        QFontMetrics(name_font).elidedText(name, Qt::ElideRight, name_width));
    if (!table_)
    {
        paint_chip(
            painter,
            QRect(right - chip_width(state), top - 1, chip_width(state), 20),
            state, kind);
    }

    if (!compact_ && !table_)
    {
        painter->setFont(theme.font(text_style::caption));
        painter->setPen(theme.colour(colour_role::ink3));
        painter->drawText(
            QRect(left, top + 20, right - left, 18),
            Qt::AlignLeft | Qt::AlignVCenter,
            QFontMetrics(theme.font(text_style::caption))
                .elidedText(role_text, Qt::ElideRight, right - left));
    }

    const int path_y = table_ ? option.rect.top() + 22
                              : option.rect.bottom() - (compact_ ? 28 : 30);
    painter->setFont(theme.font(text_style::data));
    painter->setPen(theme.colour(colour_role::ink2));
    const QFontMetrics mono(theme.font(text_style::data));
    painter->drawText(QRect(left, path_y, right - left - 34, 24),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      mono.elidedText(path, Qt::ElideRight, right - left - 34));

    copy_rect_ = copy_rect_for(option, index);
    if (!path.isEmpty())
    {
        const QIcon icon = IconProvider::instance().icon(QStringLiteral("copy"),
                                                         colour_role::ink3, 14);
        icon.paint(painter, copy_rect_.adjusted(5, 5, -5, -5));
    }

    const QString serial = index.data(PortModel::serial_role).toString();
    if (!serial.isEmpty() && !compact_ && !table_)
    {
        painter->setFont(theme.font(text_style::caption));
        painter->setPen(theme.colour(colour_role::ink3));
        painter->drawText(
            QRect(left, option.rect.bottom() - 26, right - left, 18),
            Qt::AlignRight | Qt::AlignVCenter, serial);
    }

    painter->setPen(QPen(theme.colour(colour_role::line), 1));
    painter->drawLine(option.rect.bottomLeft(), option.rect.bottomRight());
    painter->restore();
}

QSize PortDelegate::sizeHint(const QStyleOptionViewItem& option,
                             const QModelIndex& index) const
{
    Q_UNUSED(option);
    Q_UNUSED(index);
    if (table_)
        return QSize(120, 44);
    return QSize(240, compact_ ? 58 : 78);
}

bool PortDelegate::editorEvent(QEvent* event, QAbstractItemModel* model,
                               const QStyleOptionViewItem& option,
                               const QModelIndex& index)
{
    if (event->type() == QEvent::MouseButtonRelease)
    {
        const QRect copy_rect = copy_rect_for(option, index);
        if (copy_rect.isValid() &&
            copy_rect.contains(
                static_cast<QMouseEvent*>(event)->position().toPoint()))
        {
            if (copy_handler_)
                copy_handler_(static_cast<vss::endpoint_id>(
                    index.data(PortModel::port_id_role).toULongLong()));
            return true;
        }
    }
    return QStyledItemDelegate::editorEvent(event, model, option, index);
}

} // namespace vss::gui
