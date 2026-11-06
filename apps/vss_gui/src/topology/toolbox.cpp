//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "topology/toolbox.hpp"

#include "theme/icon_provider.hpp"
#include "theme/theme.hpp"

#include <QCoreApplication>
#include <QKeyEvent>
#include <QMimeData>

#include <array>

namespace vss::gui::topology
{
namespace
{

constexpr int kRole = Qt::UserRole + 1;
constexpr int kTileWidth = 64;
constexpr int kTileHeight = 56;
constexpr int kGroupHeight = 24;

constexpr std::array kPorts = {canvas_item::port, canvas_item::fixture,
                               canvas_item::loopback, canvas_item::device,
                               canvas_item::network};
constexpr std::array kBundles = {
    canvas_item::pair,     canvas_item::split,   canvas_item::join,
    canvas_item::bus,      canvas_item::switch_link, canvas_item::redirect,
    canvas_item::monitor,  canvas_item::share,   canvas_item::bench};

} // namespace

QString toolbox_mime_type()
{
    return QStringLiteral("application/x-vss-canvas-item");
}

std::optional<canvas_item> item_from_mime(const QMimeData* data)
{
    if (data == nullptr || !data->hasFormat(toolbox_mime_type()))
        return std::nullopt;
    bool ok = false;
    const int value =
        QString::fromLatin1(data->data(toolbox_mime_type())).toInt(&ok);
    if (!ok || value < 0 || value > static_cast<int>(canvas_item::bench))
        return std::nullopt;
    return static_cast<canvas_item>(value);
}

QString item_label(canvas_item item)
{
    switch (item)
    {
    case canvas_item::port:
        return Toolbox::tr("Port");
    case canvas_item::fixture:
        return Toolbox::tr("Fixture");
    case canvas_item::loopback:
        return Toolbox::tr("Loopback");
    case canvas_item::device:
        return Toolbox::tr("Device");
    case canvas_item::network:
        return Toolbox::tr("Network");
    case canvas_item::pair:
        return Toolbox::tr("Pair");
    case canvas_item::split:
        return Toolbox::tr("Split");
    case canvas_item::join:
        return Toolbox::tr("Join");
    case canvas_item::bus:
        return Toolbox::tr("Bus");
    case canvas_item::switch_link:
        return Toolbox::tr("Switch");
    case canvas_item::redirect:
        return Toolbox::tr("Redirect");
    case canvas_item::monitor:
        return Toolbox::tr("Monitor");
    case canvas_item::share:
        return Toolbox::tr("Share");
    case canvas_item::bench:
        return Toolbox::tr("Bench");
    }
    return {};
}

QString item_description(canvas_item item)
{
    switch (item)
    {
    case canvas_item::port:
        return Toolbox::tr("A virtual port of the best available kind");
    case canvas_item::fixture:
        return Toolbox::tr("A mock device");
    case canvas_item::loopback:
        return Toolbox::tr("A port that sends back everything it receives");
    case canvas_item::device:
        return Toolbox::tr("A real serial device on this computer");
    case canvas_item::network:
        return Toolbox::tr("A TCP, UDP or RFC 2217 endpoint");
    case canvas_item::pair:
        return Toolbox::tr(
            "Two ports linked both ways, like a null-modem cable");
    case canvas_item::split:
        return Toolbox::tr("One port feeds several; their replies merge back");
    case canvas_item::join:
        return Toolbox::tr("Several ports send into one, one way");
    case canvas_item::bus:
        return Toolbox::tr(
            "Several ports on one shared line; each hears all the others");
    case canvas_item::switch_link:
        return Toolbox::tr(
            "Several programs send to one port, one owner at a time");
    case canvas_item::redirect:
        return Toolbox::tr("Bytes from one port go to another, one way");
    case canvas_item::monitor:
        return Toolbox::tr("A passive copy of one port's traffic");
    case canvas_item::share:
        return Toolbox::tr("A real device given to several programs");
    case canvas_item::bench:
        return Toolbox::tr("Several fixtures, each paired with its own port");
    }
    return {};
}

QString item_icon(canvas_item item)
{
    switch (item)
    {
    case canvas_item::port:
        return QStringLiteral("port");
    case canvas_item::fixture:
        return QStringLiteral("fixture");
    case canvas_item::loopback:
        return QStringLiteral("loopback");
    case canvas_item::device:
        return QStringLiteral("device");
    case canvas_item::network:
        return QStringLiteral("network");
    case canvas_item::pair:
        return QStringLiteral("pair");
    case canvas_item::split:
        return QStringLiteral("split");
    case canvas_item::join:
        return QStringLiteral("join");
    case canvas_item::bus:
        return QStringLiteral("bus");
    case canvas_item::switch_link:
        return QStringLiteral("switch");
    case canvas_item::redirect:
        return QStringLiteral("redirect");
    case canvas_item::monitor:
        return QStringLiteral("watch");
    case canvas_item::share:
        return QStringLiteral("share");
    case canvas_item::bench:
        return QStringLiteral("bench");
    }
    return {};
}

Toolbox::Toolbox(QWidget* parent) : QListWidget(parent)
{
    setObjectName(QStringLiteral("canvasToolbox"));
    setAccessibleName(tr("Toolbox"));
    setAccessibleDescription(
        tr("Drag a tile onto the canvas, or press Enter on it."));
    setViewMode(QListView::IconMode);
    setFlow(QListView::TopToBottom);
    setWrapping(false);
    setMovement(QListView::Static);
    setResizeMode(QListView::Adjust);
    setUniformItemSizes(false);
    setSpacing(2);
    setIconSize(QSize(20, 20));
    setSelectionMode(QAbstractItemView::SingleSelection);
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragOnly);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFrameShape(QFrame::NoFrame);
    setFixedWidth(kTileWidth + 12);
    setProperty("surface", QStringLiteral("sunken"));

    connect(this, &QListWidget::itemActivated, this,
            [this](QListWidgetItem* tile)
            {
                if (tile == nullptr || !(tile->flags() & Qt::ItemIsEnabled))
                    return;
                const QVariant value = tile->data(kRole);
                if (value.isValid())
                    Q_EMIT add_requested(
                        static_cast<canvas_item>(value.toInt()));
            });
    connect(&Theme::instance(), &Theme::theme_changed, this,
            [this] { rebuild(); });
    rebuild();
}

void Toolbox::set_capabilities(const capabilities& caps)
{
    caps_ = caps;
    rebuild();
}

void Toolbox::rebuild()
{
    clear();
    const QFont label_font = Theme::instance().font(text_style::caption);
    const auto add_group = [this, &label_font](const QString& text)
    {
        auto* group = new QListWidgetItem(text, this);
        group->setFlags(Qt::NoItemFlags);
        group->setFont(label_font);
        group->setSizeHint(QSize(kTileWidth, kGroupHeight));
        group->setData(Qt::AccessibleTextRole, text);
    };
    const auto add_tile = [this, &label_font](canvas_item item)
    {
        const bool enabled = item_available(caps_, item);
        auto* tile = new QListWidgetItem(
            IconProvider::instance().icon(item_icon(item),
                                          enabled ? colour_role::ink
                                                  : colour_role::disabledText,
                                          20),
            item_label(item), this);
        tile->setData(kRole, static_cast<int>(item));
        tile->setFont(label_font);
        tile->setTextAlignment(Qt::AlignCenter);
        tile->setSizeHint(QSize(kTileWidth, kTileHeight));
        tile->setData(Qt::AccessibleTextRole, item_label(item));
        tile->setData(Qt::AccessibleDescriptionRole,
                      tr("%1: %2").arg(item_label(item), item_description(item)));
        if (enabled)
        {
            tile->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable |
                           Qt::ItemIsDragEnabled);
            tile->setToolTip(item_description(item));
        }
        else
        {
            tile->setFlags(Qt::ItemIsSelectable);
            tile->setToolTip(QCoreApplication::translate(
                "Advanced", "Not available in this build"));
        }
    };

    add_group(tr("Ports"));
    for (const canvas_item item : kPorts)
        add_tile(item);
    add_group(tr("Bundles"));
    for (const canvas_item item : kBundles)
        add_tile(item);
}

QStringList Toolbox::tile_labels() const
{
    QStringList labels;
    for (int row = 0; row < count(); ++row)
    {
        if (item(row)->data(kRole).isValid())
            labels.append(item(row)->text());
    }
    return labels;
}

bool Toolbox::tile_enabled(canvas_item wanted) const
{
    for (int row = 0; row < count(); ++row)
    {
        const QVariant value = item(row)->data(kRole);
        if (value.isValid() && value.toInt() == static_cast<int>(wanted))
            return (item(row)->flags() & Qt::ItemIsEnabled) != 0;
    }
    return false;
}

void Toolbox::focus_item(canvas_item wanted)
{
    for (int row = 0; row < count(); ++row)
    {
        const QVariant value = item(row)->data(kRole);
        if (value.isValid() && value.toInt() == static_cast<int>(wanted))
        {
            setCurrentRow(row);
            return;
        }
    }
}

QMimeData* Toolbox::mimeData(const QList<QListWidgetItem*>& items) const
{
    if (items.isEmpty() || !items.first()->data(kRole).isValid())
        return nullptr;
    auto* mime = new QMimeData();
    mime->setData(toolbox_mime_type(),
                  QByteArray::number(items.first()->data(kRole).toInt()));
    mime->setText(items.first()->text());
    return mime;
}

QStringList Toolbox::mimeTypes() const
{
    return {toolbox_mime_type()};
}

void Toolbox::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Space && currentItem() != nullptr)
    {
        Q_EMIT itemActivated(currentItem());
        event->accept();
        return;
    }
    QListWidget::keyPressEvent(event);
}

} // namespace vss::gui::topology
