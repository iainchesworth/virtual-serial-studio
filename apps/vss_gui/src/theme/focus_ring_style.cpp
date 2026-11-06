//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "theme/focus_ring_style.hpp"

#include "theme/icon_provider.hpp"
#include "theme/theme.hpp"

#include <QApplication>
#include <QFocusEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyleFactory>
#include <QStyleOptionFocusRect>
#include <QWidget>

#include <algorithm>

namespace vss::gui
{

Qt::FocusReason FocusRingStyle::last_reason_ = Qt::OtherFocusReason;

FocusRingStyle::FocusRingStyle(QStyle* base)
    : QProxyStyle(base != nullptr
                      ? base
                      : QStyleFactory::create(QStringLiteral("Fusion")))
{
}

void FocusRingStyle::install()
{
    if (installed_ || qApp == nullptr)
        return;
    qApp->installEventFilter(this);
    installed_ = true;
}

bool FocusRingStyle::keyboard_focus()
{
    return last_reason_ == Qt::TabFocusReason ||
           last_reason_ == Qt::BacktabFocusReason ||
           last_reason_ == Qt::ShortcutFocusReason;
}

bool FocusRingStyle::eventFilter(QObject* watched, QEvent* event)
{
    if (event != nullptr && event->type() == QEvent::FocusIn)
    {
        if (auto* focus = dynamic_cast<QFocusEvent*>(event))
            last_reason_ = focus->reason();
    }
    return QProxyStyle::eventFilter(watched, event);
}

bool FocusRingStyle::draw_chevron(PrimitiveElement element,
                                  const QStyleOption* option, QPainter* painter)
{
    // Fusion's own arrows are filled triangles a few pixels wide; the icon
    // set uses a 1.5 px stroked chevron, so draw the same shape here for the
    // combo box and spin box arrows.
    const bool up = element == PE_IndicatorArrowUp ||
                    element == PE_IndicatorSpinUp ||
                    element == PE_IndicatorSpinPlus;
    const bool enabled = (option->state & State_Enabled) != 0;
    const QColor colour =
        enabled ? Theme::instance().colour(colour_role::ink2)
                : Theme::instance().colour(colour_role::disabledText);

    const QPointF centre = QRectF(option->rect).center();
    const qreal half = std::min<qreal>(4.0, option->rect.width() / 2.0);
    const qreal rise = half * 0.55;
    QPainterPath path;
    if (up)
    {
        path.moveTo(centre.x() - half, centre.y() + rise);
        path.lineTo(centre.x(), centre.y() - rise);
        path.lineTo(centre.x() + half, centre.y() + rise);
    }
    else
    {
        path.moveTo(centre.x() - half, centre.y() - rise);
        path.lineTo(centre.x(), centre.y() + rise);
        path.lineTo(centre.x() + half, centre.y() - rise);
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    QPen pen(colour, 1.5);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(path);
    painter->restore();
    return true;
}

QIcon FocusRingStyle::standardIcon(StandardPixmap standard,
                                   const QStyleOption* option,
                                   const QWidget* widget) const
{
    // The dock title buttons are drawn by the style from standard pixmaps.
    // The icon set has the close mark, so use it rather than the platform's.
    if (standard == SP_DockWidgetCloseButton ||
        standard == SP_TitleBarCloseButton)
    {
        return IconProvider::instance().icon(QStringLiteral("remove"),
                                             colour_role::ink2, 16);
    }
    return QProxyStyle::standardIcon(standard, option, widget);
}

void FocusRingStyle::drawPrimitive(PrimitiveElement element,
                                   const QStyleOption* option,
                                   QPainter* painter,
                                   const QWidget* widget) const
{
    if (element == PE_IndicatorArrowDown || element == PE_IndicatorArrowUp ||
        element == PE_IndicatorSpinDown || element == PE_IndicatorSpinUp ||
        element == PE_IndicatorSpinMinus || element == PE_IndicatorSpinPlus)
    {
        if (option != nullptr && painter != nullptr &&
            draw_chevron(element, option, painter))
            return;
    }

    if (element != PE_FrameFocusRect)
    {
        QProxyStyle::drawPrimitive(element, option, painter, widget);
        return;
    }

    if (!keyboard_focus() || option == nullptr || painter == nullptr)
        return;
    if ((option->state & State_HasFocus) == 0)
        return;

    QPainter::RenderHints hints = painter->renderHints();
    painter->setRenderHint(QPainter::Antialiasing);
    const QColor colour = Theme::instance().colour(colour_role::focus);
    QPen pen(colour, 2.0);
    pen.setJoinStyle(Qt::RoundJoin);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);

    const QRect ring = option->rect.adjusted(1, 1, -1, -1);
    const int radius = Theme::instance().radius(QStringLiteral("control"));
    painter->drawRoundedRect(ring, radius, radius);
    painter->setRenderHints(hints);
}

} // namespace vss::gui
