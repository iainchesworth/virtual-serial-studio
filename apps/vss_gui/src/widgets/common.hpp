//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_WIDGETS_COMMON_HPP
#define VSS_GUI_WIDGETS_COMMON_HPP

#include "theme/theme.hpp"

#include <QAccessible>
#include <QLabel>
#include <QStyle>
#include <QWidget>

namespace vss::gui
{

/// Repaints a custom-painted widget when the theme or density changes.
inline void repaint_on_theme_change(QWidget* widget)
{
    QObject::connect(&Theme::instance(), &Theme::themeChanged, widget,
                     [widget] { widget->update(); });
}

inline void polish(QWidget* widget)
{
    if (widget == nullptr || widget->style() == nullptr)
        return;
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

inline void set_role(QWidget* widget, const char* role)
{
    widget->setProperty("role", QString::fromLatin1(role));
    polish(widget);
}

inline QLabel* make_label(const QString& text, text_style style,
                          QWidget* parent = nullptr)
{
    auto* label = new QLabel(text, parent);
    label->setFont(Theme::instance().font(style));
    label->setWordWrap(true);
    return label;
}

inline void announce_name_change(QWidget* widget)
{
    if (widget == nullptr)
        return;
    auto* event = new QAccessibleEvent(widget, QAccessible::NameChanged);
    QAccessible::updateAccessibility(event);
}

} // namespace vss::gui

#endif // VSS_GUI_WIDGETS_COMMON_HPP
