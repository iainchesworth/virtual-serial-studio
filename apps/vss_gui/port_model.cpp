//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "port_model.hpp"

#include "theme.hpp"

#include <vss/core/control_lines.hpp>
#include <vss/core/serial_settings.hpp>

#include <QBrush>
#include <QColor>
#include <QStringList>

#include <algorithm>
#include <utility>

namespace vss::gui {
namespace {

QString display_value(const vss::endpoint_info& info, int column)
{
    switch (column)
    {
    case PortTableModel::NameColumn:
        return QString::fromStdString(info.name);
    case PortTableModel::KindColumn:
        return kind_text(info.kind);
    case PortTableModel::PathColumn:
        return info.path.empty() ? QStringLiteral("—")
                                 : QString::fromStdString(info.path);
    case PortTableModel::StateColumn:
        return state_text(info.state);
    case PortTableModel::LinesColumn:
        return QString::fromStdString(vss::to_string(info.lines));
    case PortTableModel::RxColumn:
        return format_count(info.stats.bytes_rx);
    case PortTableModel::TxColumn:
        return format_count(info.stats.bytes_tx);
    case PortTableModel::ErrorsColumn:
        return format_count(info.stats.errors);
    default:
        break;
    }
    return {};
}

const char* const column_titles[PortTableModel::ColumnCount] = {
    QT_TRANSLATE_NOOP("PortTableModel", "Name"),
    QT_TRANSLATE_NOOP("PortTableModel", "Kind"),
    QT_TRANSLATE_NOOP("PortTableModel", "Advertised path"),
    QT_TRANSLATE_NOOP("PortTableModel", "State"),
    QT_TRANSLATE_NOOP("PortTableModel", "Lines"),
    QT_TRANSLATE_NOOP("PortTableModel", "RX bytes"),
    QT_TRANSLATE_NOOP("PortTableModel", "TX bytes"),
    QT_TRANSLATE_NOOP("PortTableModel", "Errors")};

bool numeric_column(int column) noexcept
{
    return column >= PortTableModel::RxColumn
        && column <= PortTableModel::ErrorsColumn;
}

} // namespace

PortTableModel::PortTableModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

bool PortTableModel::set_endpoints(std::vector<vss::endpoint_info> endpoints)
{
    bool same_shape = endpoints.size() == endpoints_.size();
    if (same_shape)
    {
        for (std::size_t row = 0; row < endpoints.size(); ++row)
        {
            if (endpoints[row].id != endpoints_[row].id)
            {
                same_shape = false;
                break;
            }
        }
    }

    if (!same_shape)
    {
        beginResetModel();
        endpoints_ = std::move(endpoints);
        endResetModel();
        return true;
    }

    endpoints_ = std::move(endpoints);
    if (!endpoints_.empty())
    {
        Q_EMIT dataChanged(index(0, 0),
                         index(static_cast<int>(endpoints_.size()) - 1,
                               ColumnCount - 1));
    }
    return false;
}

const vss::endpoint_info* PortTableModel::endpoint_at(int row) const
{
    if (row < 0 || row >= static_cast<int>(endpoints_.size()))
        return nullptr;
    return &endpoints_[static_cast<std::size_t>(row)];
}

const vss::endpoint_info* PortTableModel::find_endpoint(
    vss::endpoint_id id) const
{
    if (id == 0)
        return nullptr;
    for (const auto& info : endpoints_)
    {
        if (info.id == id)
            return &info;
    }
    return nullptr;
}

QModelIndex PortTableModel::index_for_id(vss::endpoint_id id) const
{
    if (id == 0)
        return {};
    for (std::size_t row = 0; row < endpoints_.size(); ++row)
    {
        if (endpoints_[row].id == id)
            return index(static_cast<int>(row), 0);
    }
    return {};
}

int PortTableModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(endpoints_.size());
}

int PortTableModel::columnCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return ColumnCount;
}

QVariant PortTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0
        || index.row() >= static_cast<int>(endpoints_.size())
        || index.column() < 0 || index.column() >= ColumnCount)
        return {};

    const auto& info = endpoints_[static_cast<std::size_t>(index.row())];
    switch (role)
    {
    case Qt::DisplayRole:
        return display_value(info, index.column());
    case Qt::TextAlignmentRole:
        if (numeric_column(index.column()))
            return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
        return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
    case Qt::ForegroundRole:
        if (index.column() == StateColumn)
            return QBrush(state_colour(info.state));
        if (index.column() == ErrorsColumn && info.stats.errors > 0)
            return QBrush(QColor(0xcf, 0x22, 0x2e));
        if (index.column() == PathColumn && info.path.empty())
            return QBrush(QColor(0x8b, 0x94, 0x9e));
        break;
    case Qt::ToolTipRole:
    {
        QStringList tooltip;
        tooltip << QString::fromStdString(info.name);
        if (!info.description.empty())
            tooltip << QString::fromStdString(info.description);
        if (!info.path.empty())
            tooltip << QStringLiteral("Path: %1").arg(
                QString::fromStdString(info.path));
        tooltip << QStringLiteral("Serial: %1").arg(
            QString::fromStdString(vss::to_label(info.serial)));
        tooltip << QStringLiteral("Reads: %1 · writes: %2")
                       .arg(format_count(info.stats.read_ops),
                            format_count(info.stats.write_ops));
        return tooltip.join(QLatin1Char('\n'));
    }
    case Qt::UserRole:
        return QVariant::fromValue<qulonglong>(
            static_cast<qulonglong>(info.id));
    default:
        break;
    }
    return {};
}

QVariant PortTableModel::headerData(int section, Qt::Orientation orientation,
                                    int role) const
{
    if (orientation != Qt::Horizontal)
        return QAbstractTableModel::headerData(section, orientation, role);

    if (section < 0 || section >= ColumnCount)
        return {};

    switch (role)
    {
    case Qt::DisplayRole:
        return tr(column_titles[section]);
    case Qt::TextAlignmentRole:
        if (numeric_column(section))
            return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
        return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
    case Qt::ToolTipRole:
        if (section == RxColumn)
            return tr("Bytes received from the device.");
        if (section == TxColumn)
            return tr("Bytes written to the device.");
        if (section == LinesColumn)
            return tr("Output lines driven by the application, then peer inputs.");
        break;
    default:
        break;
    }
    return QAbstractTableModel::headerData(section, orientation, role);
}

} // namespace vss::gui
