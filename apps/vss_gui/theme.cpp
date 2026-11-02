//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "theme.hpp"

#include <QApplication>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>

namespace vss::gui {
namespace {

QPalette dark_palette()
{
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(0x1e, 0x1f, 0x22));
    palette.setColor(QPalette::WindowText, QColor(0xe6, 0xe6, 0xe6));
    palette.setColor(QPalette::Base, QColor(0x16, 0x17, 0x19));
    palette.setColor(QPalette::AlternateBase, QColor(0x24, 0x25, 0x28));
    palette.setColor(QPalette::ToolTipBase, QColor(0x2a, 0x2b, 0x2e));
    palette.setColor(QPalette::ToolTipText, QColor(0xe6, 0xe6, 0xe6));
    palette.setColor(QPalette::Text, QColor(0xe6, 0xe6, 0xe6));
    palette.setColor(QPalette::Button, QColor(0x2a, 0x2b, 0x2e));
    palette.setColor(QPalette::ButtonText, QColor(0xe6, 0xe6, 0xe6));
    palette.setColor(QPalette::BrightText, QColor(0xff, 0x6b, 0x6b));
    palette.setColor(QPalette::Link, QColor(0x74, 0xa7, 0xff));
    palette.setColor(QPalette::Highlight, QColor(0x2f, 0x6f, 0xd0));
    palette.setColor(QPalette::HighlightedText, QColor(0xff, 0xff, 0xff));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(0x8a, 0x8a,
                                                                0x8a));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText,
                     QColor(0x8a, 0x8a, 0x8a));
    return palette;
}

} // namespace

void apply_theme(const QString& name)
{
    QApplication* app = qApp;
    if (app == nullptr)
        return;

    if (name == QLatin1String("dark"))
    {
        if (app->style()->objectName().compare(QStringLiteral("fusion"),
                                               Qt::CaseInsensitive) != 0)
            app->setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
        app->setPalette(dark_palette());
    }
    else if (name == QLatin1String("light"))
    {
        if (app->style()->objectName().compare(QStringLiteral("fusion"),
                                               Qt::CaseInsensitive) != 0)
            app->setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
        app->setPalette(app->style()->standardPalette());
    }
    else
    {
        // Follow the desktop: the style's own standard palette.
        app->setPalette(app->style()->standardPalette());
    }
}

} // namespace vss::gui
