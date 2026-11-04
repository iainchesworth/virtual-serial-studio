//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "theme/icon_provider.hpp"

#include <QFile>
#include <QGuiApplication>
#include <QPainter>
#include <QPixmap>
#include <QScreen>
#include <QSvgRenderer>

#include <cmath>

namespace vss::gui
{

IconProvider& IconProvider::instance()
{
    static IconProvider provider;
    return provider;
}

IconProvider::IconProvider(QObject* parent) : QObject(parent)
{
    connect(&Theme::instance(), &Theme::theme_changed, this,
            [this] { flush(); });
}

QIcon IconProvider::icon(const QString& name, colour_role role,
                         int logical_size)
{
    return icon(name, Theme::instance().colour(role), logical_size);
}

QIcon IconProvider::icon(const QString& name, const QColor& colour,
                         int logical_size)
{
    if (logical_size <= 0)
        logical_size = 16;

    qreal ratio = 1.0;
    if (QGuiApplication::primaryScreen() != nullptr)
        ratio = QGuiApplication::primaryScreen()->devicePixelRatio();
    if (ratio <= 0.0)
        ratio = 1.0;

    const QString key =
        QStringLiteral("%1|%2|%3|%4")
            .arg(name, colour.name(), QString::number(logical_size),
                 QString::number(ratio));
    const auto cached = cache_.constFind(key);
    if (cached != cache_.constEnd())
        return cached.value();

    const QIcon icon = render(name, colour, logical_size);
    cache_.insert(key, icon);
    return icon;
}

void IconProvider::flush()
{
    cache_.clear();
}

QIcon IconProvider::render(const QString& name, const QColor& colour,
                           int logical_size)
{
    QFile file(QStringLiteral(":/icons/%1.svg").arg(name));
    if (!file.open(QIODevice::ReadOnly))
    {
        qWarning("IconProvider: missing icon \"%s\"",
                 name.toUtf8().constData());
        return {};
    }

    QByteArray svg = file.readAll();
    svg.replace("currentColor", colour.name(QColor::HexRgb).toUtf8());

    QSvgRenderer renderer(svg);
    if (!renderer.isValid())
    {
        qWarning("IconProvider: invalid SVG \"%s\"", name.toUtf8().constData());
        return {};
    }

    qreal ratio = 1.0;
    if (QGuiApplication::primaryScreen() != nullptr)
        ratio = QGuiApplication::primaryScreen()->devicePixelRatio();
    if (ratio <= 0.0)
        ratio = 1.0;

    const int pixels = static_cast<int>(std::ceil(logical_size * ratio));
    QPixmap pixmap(pixels, pixels);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    renderer.render(&painter);
    painter.end();

    return QIcon(pixmap);
}

} // namespace vss::gui
