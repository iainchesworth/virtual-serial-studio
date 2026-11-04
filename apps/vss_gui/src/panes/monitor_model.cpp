//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "panes/monitor_model.hpp"

#include "theme/theme.hpp"

#include <QLocale>
#include <QStringList>
#include <QTime>

#include <algorithm>
#include <cstddef>

namespace vss::gui
{

MonitorModel::MonitorModel(QObject* parent) : QAbstractTableModel(parent) {}

void MonitorModel::append_events(const event_batch& events)
{
    if (paused_)
        return;

    bool changed = false;
    for (const gui_event& event : events)
    {
        if (event.kind != vss::event_kind::data || event.data.isEmpty())
            continue;
        traffic_row row;
        row.time = QDateTime::currentDateTime();
        row.endpoint = event.endpoint;
        row.port = event.name;
        row.direction = event.direction == vss::data_direction::tx
                            ? traffic_direction::tx
                            : traffic_direction::rx;
        row.bytes = static_cast<std::uint64_t>(event.data.size());
        row.preview = event.data.left(kPreviewBytes);
        rows_.push_back(std::move(row));
        changed = true;
    }
    if (!changed)
        return;

    beginResetModel();
    trim();
    rebuild_visible();
    endResetModel();
}

void MonitorModel::append_drop(const QString& port, std::uint64_t bytes)
{
    if (paused_ || bytes == 0)
        return;
    traffic_row row;
    row.time = QDateTime::currentDateTime();
    row.port = port;
    row.direction = traffic_direction::drop;
    row.bytes = bytes;
    row.dropped = true;
    beginResetModel();
    rows_.push_back(std::move(row));
    trim();
    rebuild_visible();
    endResetModel();
}

void MonitorModel::clear()
{
    beginResetModel();
    rows_.clear();
    visible_.clear();
    endResetModel();
}

void MonitorModel::set_paused(bool paused)
{
    paused_ = paused;
}

void MonitorModel::set_hex(bool hex)
{
    if (hex_ == hex)
        return;
    hex_ = hex;
    if (!visible_.isEmpty())
    {
        Q_EMIT dataChanged(index(0, 4), index(rowCount() - 1, 4),
                           {Qt::DisplayRole, Qt::AccessibleTextRole});
    }
}

void MonitorModel::set_filters(vss::endpoint_id port, bool rx, bool tx,
                               bool dropped, const QString& search)
{
    if (port_filter_ == port && rx_ == rx && tx_ == tx && dropped_ == dropped &&
        search_ == search)
        return;
    port_filter_ = port;
    rx_ = rx;
    tx_ = tx;
    dropped_ = dropped;
    search_ = search;
    beginResetModel();
    rebuild_visible();
    endResetModel();
}

void MonitorModel::set_selected_port(vss::endpoint_id id)
{
    if (selected_ == id)
        return;
    selected_ = id;
    if (!visible_.isEmpty())
    {
        Q_EMIT dataChanged(index(0, 0), index(rowCount() - 1, 4),
                           {Qt::BackgroundRole});
    }
}

void MonitorModel::trim()
{
    if (rows_.size() <= static_cast<std::size_t>(kMaxRows))
        return;
    const auto excess = rows_.size() - static_cast<std::size_t>(kMaxRows);
    rows_.erase(rows_.begin(),
                rows_.begin() + static_cast<std::ptrdiff_t>(excess));
}

void MonitorModel::rebuild_visible()
{
    visible_.clear();
    visible_.reserve(static_cast<qsizetype>(rows_.size()));
    for (int index = 0; index < static_cast<int>(rows_.size()); ++index)
    {
        if (matches(rows_[static_cast<std::size_t>(index)]))
            visible_.push_back(index);
    }
}

bool MonitorModel::matches(const traffic_row& row) const
{
    if (port_filter_ != 0 && row.endpoint != port_filter_)
        return false;
    switch (row.direction)
    {
    case traffic_direction::rx:
        if (!rx_)
            return false;
        break;
    case traffic_direction::tx:
        if (!tx_)
            return false;
        break;
    case traffic_direction::drop:
        if (!dropped_)
            return false;
        break;
    }
    if (!search_.isEmpty())
    {
        const QString text = preview_for(row);
        if (!row.port.contains(search_, Qt::CaseInsensitive) &&
            !text.contains(search_, Qt::CaseInsensitive))
            return false;
    }
    return true;
}

int MonitorModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(visible_.size());
}

int MonitorModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : 5;
}

QVariant MonitorModel::data(const QModelIndex& index, int item_role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
        return {};
    const traffic_row& row =
        rows_[static_cast<std::size_t>(visible_[index.row()])];
    switch (item_role)
    {
    case Qt::DisplayRole:
        switch (index.column())
        {
        case 0:
            return row.time.toString(QStringLiteral("HH:mm:ss.zzz"));
        case 1:
            return row.port;
        case 2:
            switch (row.direction)
            {
            case traffic_direction::rx:
                return tr("RX");
            case traffic_direction::tx:
                return tr("TX");
            case traffic_direction::drop:
                return tr("DROP");
            }
            return {};
        case 3:
            return QLocale().toString(static_cast<qulonglong>(row.bytes));
        case 4:
            return preview_for(row);
        default:
            return {};
        }
    case Qt::FontRole:
        if (index.column() == 0 || index.column() == 3 || index.column() == 4)
            return Theme::instance().font(text_style::data);
        return {};
    case Qt::TextAlignmentRole:
        if (index.column() == 3)
            return QVariant::fromValue(Qt::AlignRight | Qt::AlignVCenter);
        return QVariant::fromValue(Qt::AlignLeft | Qt::AlignVCenter);
    case Qt::BackgroundRole:
        if (selected_ != 0 && row.endpoint == selected_)
            return Theme::instance().colour(colour_role::accentSoft);
        return {};
    case Qt::ToolTipRole:
        return row.dropped ? tr("Not the owner, not forwarded.")
                           : preview_for(row);
    case Qt::AccessibleTextRole:
    {
        const QString dir =
            row.direction == traffic_direction::rx   ? tr("received")
            : row.direction == traffic_direction::tx ? tr("sent")
                                                     : tr("dropped");
        return tr("%1, %2, %3, %4 bytes, %5")
            .arg(row.time.toString(QStringLiteral("HH:mm:ss.zzz")), row.port,
                 dir, QLocale().toString(static_cast<qulonglong>(row.bytes)),
                 preview_for(row));
    }
    case endpoint_role:
        return QVariant::fromValue<qulonglong>(row.endpoint);
    case direction_role:
        return static_cast<int>(row.direction);
    case dropped_role:
        return row.dropped;
    case bytes_role:
        return QVariant::fromValue<qulonglong>(row.bytes);
    default:
        return {};
    }
}

QVariant MonitorModel::headerData(int section, Qt::Orientation orientation,
                                  int item_role) const
{
    if (orientation != Qt::Horizontal || item_role != Qt::DisplayRole)
        return QAbstractTableModel::headerData(section, orientation, item_role);
    switch (section)
    {
    case 0:
        return tr("Time");
    case 1:
        return tr("Port");
    case 2:
        return tr("Direction");
    case 3:
        return tr("Bytes");
    case 4:
        return tr("Preview");
    default:
        return {};
    }
}

vss::endpoint_id MonitorModel::port_at(int row) const
{
    if (row < 0 || row >= rowCount())
        return 0;
    return rows_[static_cast<std::size_t>(visible_[row])].endpoint;
}

const traffic_row* MonitorModel::row_at(int row) const
{
    if (row < 0 || row >= rowCount())
        return nullptr;
    return &rows_[static_cast<std::size_t>(visible_[row])];
}

QString MonitorModel::preview_for(const traffic_row& row) const
{
    if (row.dropped)
        return tr("not the owner, not forwarded");
    return hex_ ? hex_preview(row.preview) : ascii_preview(row.preview);
}

QString MonitorModel::ascii_preview(const QByteArray& data)
{
    QString text;
    text.reserve(data.size());
    for (const char raw : data)
    {
        const auto value = static_cast<unsigned char>(raw);
        if (value == '\r')
            text += QStringLiteral("\\r");
        else if (value == '\n')
            text += QStringLiteral("\\n");
        else if (value < 0x20 || value > 0x7e)
            text += QLatin1Char('.');
        else
            text += QLatin1Char(static_cast<char>(value));
    }
    return text;
}

QString MonitorModel::hex_preview(const QByteArray& data)
{
    static const char* digits = "0123456789ABCDEF";
    QString text;
    text.reserve(data.size() * 3);
    for (const char raw : data)
    {
        const auto value = static_cast<unsigned char>(raw);
        if (!text.isEmpty())
            text += QLatin1Char(' ');
        text += QLatin1Char(digits[value >> 4]);
        text += QLatin1Char(digits[value & 0x0f]);
    }
    return text;
}

QString MonitorModel::export_text() const
{
    QStringList lines;
    lines << QStringLiteral("Time\tPort\tDirection\tBytes\tPreview");
    for (const traffic_row& row : rows_)
    {
        if (!matches(row))
            continue;
        const QString direction =
            row.direction == traffic_direction::rx   ? QStringLiteral("RX")
            : row.direction == traffic_direction::tx ? QStringLiteral("TX")
                                                     : QStringLiteral("DROP");
        lines << QStringLiteral("%1\t%2\t%3\t%4\t%5")
                     .arg(
                         row.time.toString(QStringLiteral("HH:mm:ss.zzz")),
                         row.port, direction,
                         QLocale().toString(static_cast<qulonglong>(row.bytes)),
                         preview_for(row));
    }
    return lines.join(QLatin1Char('\n'));
}

} // namespace vss::gui
