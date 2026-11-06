//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#pragma once

// Mouse input and layout checks for the Qt widget tests.  Events go through
// `QApplication::sendEvent` to the widget under the point, the way the window
// system delivers them, so an ignored press travels up to the parent.

#include <QApplication>
#include <QDialog>
#include <QFont>
#include <QFrame>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPushButton>
#include <QRect>
#include <QScreen>
#include <QWidget>

namespace vss_test
{

/// The widget a click at `position` (in `root` coordinates) reaches first.
inline QWidget* widget_at(QWidget* root, const QPoint& position)
{
    QWidget* child = root->childAt(position);
    return child != nullptr ? child : root;
}

inline void send_mouse(QWidget* root, QEvent::Type type, const QPoint& position)
{
    QWidget* target = widget_at(root, position);
    const QPointF local(target->mapFrom(root, position));
    const QPointF global(root->mapToGlobal(position));
    const bool down = type != QEvent::MouseButtonRelease;
    QMouseEvent event(type, local, global, Qt::LeftButton,
                      down ? Qt::LeftButton : Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(target, &event);
}

/// Presses and releases the left button at `position` in `root`.
inline void click_at(QWidget* root, const QPoint& position)
{
    send_mouse(root, QEvent::MouseButtonPress, position);
    send_mouse(root, QEvent::MouseButtonRelease, position);
    QApplication::processEvents();
}

/// The two clicks of a double-click, the second as the double-click event.
inline void double_click_at(QWidget* root, const QPoint& position)
{
    click_at(root, position);
    send_mouse(root, QEvent::MouseButtonDblClick, position);
    send_mouse(root, QEvent::MouseButtonRelease, position);
    QApplication::processEvents();
}

/// Makes the text of `root` and everything under it `factor` times as large.
inline void scale_text(QWidget* root, double factor)
{
    const auto scale = [factor](QWidget* widget)
    {
        QFont font = widget->font();
        if (font.pointSizeF() > 0)
            font.setPointSizeF(font.pointSizeF() * factor);
        else if (font.pixelSize() > 0)
            font.setPixelSize(static_cast<int>(font.pixelSize() * factor));
        widget->setFont(font);
    };
    scale(root);
    for (auto* child : root->findChildren<QWidget*>())
        scale(child);
}

/// Resizes `widget` as small as its minimum size allows, the way a person
/// drags a window corner in.
inline void shrink_to_minimum(QWidget& widget)
{
    widget.resize(1, 1);
    QApplication::processEvents();
}

/// True when the dialog fits on its screen and every visible button in its
/// footer lies fully inside the dialog.  A footer that the layout pushed
/// below the window edge, or a window taller than the screen, fails.
inline bool footer_fully_visible(QDialog& dialog)
{
    const QScreen* screen = dialog.screen() != nullptr
                                ? dialog.screen()
                                : QGuiApplication::primaryScreen();
    if (screen != nullptr &&
        dialog.height() > screen->availableGeometry().height())
        return false;

    QFrame* footer = dialog.findChild<QFrame*>(QStringLiteral("dialogFooter"));
    if (footer == nullptr || !footer->isVisibleTo(&dialog))
        return false;
    bool any = false;
    for (auto* button : footer->findChildren<QPushButton*>())
    {
        if (!button->isVisibleTo(&dialog))
            continue;
        any = true;
        const QRect rect(button->mapTo(&dialog, QPoint(0, 0)), button->size());
        if (!dialog.rect().contains(rect))
            return false;
    }
    return any;
}

} // namespace vss_test
