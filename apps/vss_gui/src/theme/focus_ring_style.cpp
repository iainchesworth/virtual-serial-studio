//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "theme/focus_ring_style.hpp"

#include "theme/theme.hpp"

#include <QApplication>
#include <QFocusEvent>
#include <QPainter>
#include <QStyleFactory>
#include <QStyleOptionFocusRect>
#include <QWidget>

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

void FocusRingStyle::drawPrimitive(PrimitiveElement element,
                                   const QStyleOption* option,
                                   QPainter* painter,
                                   const QWidget* widget) const
{
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
