//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "topology/links_list.hpp"

#include "theme/theme.hpp"
#include "topology/topology_text.hpp"

#include <QHash>
#include <QListWidget>
#include <QListWidgetItem>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>

namespace vss::gui::topology
{

LinksList::LinksList(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("linksList"));
    setAccessibleName(topology_text::links_list_name());

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("linksListWidget"));
    list_->setAccessibleName(topology_text::links_list_name());
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setUniformItemSizes(true);
    list_->setAlternatingRowColors(false);
    list_->setWordWrap(false);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layout->addWidget(list_);

    connect(list_, &QListWidget::currentRowChanged, this,
            [this](int row)
            {
                if (updating_)
                    return;
                QListWidgetItem* item = list_->item(row);
                if (item == nullptr)
                    return;
                const vss::link_id id = item->data(Qt::UserRole).toULongLong();
                Q_EMIT link_selected(id);
            });
}

void LinksList::set_links(const std::vector<link_view>& links,
                          const std::vector<port_view>& ports)
{
    QHash<vss::endpoint_id, QString> names;
    for (const auto& port : ports)
        names.insert(port.id, port.name);

    QVector<link_view> ordered(links.begin(), links.end());
    std::sort(ordered.begin(), ordered.end(),
              [](const link_view& left, const link_view& right)
              { return left.id < right.id; });

    const QSignalBlocker blocker(list_);
    updating_ = true;
    list_->clear();
    count_ = static_cast<int>(ordered.size());

    const int row_height = std::max(24, Theme::instance().row_height());

    if (ordered.isEmpty())
    {
        auto* item = new QListWidgetItem(topology_text::no_links(), list_);
        item->setFlags(Qt::NoItemFlags);
        item->setSizeHint(QSize(0, row_height));
        item->setData(Qt::UserRole, static_cast<qulonglong>(0));
    }
    else
    {
        for (const auto& link : ordered)
        {
            auto* item = new QListWidgetItem(
                topology_text::link_name(link, names), list_);
            item->setData(Qt::UserRole, static_cast<qulonglong>(link.id));
            item->setToolTip(topology_text::link_name(link, names));
            item->setSizeHint(QSize(0, row_height));
        }
    }
    list_->setCurrentIndex(QModelIndex());
    updating_ = false;
}

void LinksList::set_selected(vss::link_id id)
{
    const QSignalBlocker blocker(list_);
    updating_ = true;
    int row = -1;
    for (int index = 0; index < list_->count(); ++index)
    {
        const vss::link_id candidate =
            list_->item(index)->data(Qt::UserRole).toULongLong();
        if (candidate == id && id != 0)
        {
            row = index;
            break;
        }
    }
    if (row >= 0)
    {
        list_->setCurrentRow(row);
    }
    else
    {
        list_->setCurrentIndex(QModelIndex());
        list_->clearSelection();
    }
    updating_ = false;
}

vss::link_id LinksList::selected() const
{
    QListWidgetItem* item = list_->currentItem();
    if (item == nullptr)
        return 0;
    return item->data(Qt::UserRole).toULongLong();
}

QStringList LinksList::item_texts() const
{
    QStringList texts;
    for (int index = 0; index < list_->count(); ++index)
        texts.append(list_->item(index)->text());
    return texts;
}

} // namespace vss::gui::topology
