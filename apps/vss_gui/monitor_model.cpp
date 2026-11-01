//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "monitor_model.hpp"

#include "theme.hpp"

#include <vss/core/bytes.hpp>

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QFontDatabase>
#include <QStringList>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <utility>

namespace vss::gui {
namespace {

constexpr std::size_t preview_limit = 96;
constexpr const char* preview_ellipsis = "\u2026";

const char* const column_titles[MonitorModel::ColumnCount] = {
    QT_TRANSLATE_NOOP("MonitorModel", "Time"),
    QT_TRANSLATE_NOOP("MonitorModel", "Endpoint"),
    QT_TRANSLATE_NOOP("MonitorModel", "Direction"),
    QT_TRANSLATE_NOOP("MonitorModel", "Length"),
    QT_TRANSLATE_NOOP("MonitorModel", "Preview")};

QFont fixed_font()
{
    static const QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    return font;
}

} // namespace

MonitorModel::MonitorModel(QObject* parent)
    : QAbstractTableModel(parent)
    , wall_epoch_(QDateTime::currentDateTime())
    , steady_epoch_(vss::clock_type::now())
{
}

int MonitorModel::append(const std::vector<vss::traffic_record>& records)
{
    if (records.empty())
        return 0;

    std::size_t start = 0;
    if (have_last_)
    {
        bool found = false;
        for (std::size_t index = records.size(); index > 0; --index)
        {
            const auto& record = records[index - 1];
            if (record.when == last_when_
                && static_cast<const void*>(record.data.get()) == last_data_)
            {
                start = index;
                found = true;
                break;
            }
        }
        if (!found)
        {
            // The marker was dropped by the hub ring buffer (or the log was
            // cleared underneath us): restart from the newest records.
            const int previous_rows = rowCount();
            if (previous_rows > 0)
            {
                beginRemoveRows(QModelIndex(), 0, previous_rows - 1);
                rows_.clear();
                endRemoveRows();
            }
            have_last_ = false;
        }
    }

    if (start >= records.size())
    {
        // Nothing new; keep the marker on the tail.
        last_when_ = records.back().when;
        last_data_ = static_cast<const void*>(records.back().data.get());
        have_last_ = true;
        return 0;
    }

    if (records.size() - start > static_cast<std::size_t>(capacity_))
        start = records.size() - static_cast<std::size_t>(capacity_);

    const int incoming = static_cast<int>(records.size() - start);
    const int overflow = static_cast<int>(rows_.size()) + incoming - capacity_;
    if (overflow > 0)
    {
        const int remove_count =
            std::min(overflow, static_cast<int>(rows_.size()));
        if (remove_count > 0)
        {
            beginRemoveRows(QModelIndex(), 0, remove_count - 1);
            rows_.erase(rows_.begin(), rows_.begin() + remove_count);
            endRemoveRows();
        }
    }

    beginInsertRows(QModelIndex(), static_cast<int>(rows_.size()),
                    static_cast<int>(rows_.size()) + incoming - 1);
    for (std::size_t index = start; index < records.size(); ++index)
        rows_.push_back(make_row(records[index]));
    endInsertRows();

    last_when_ = records.back().when;
    last_data_ = static_cast<const void*>(records.back().data.get());
    have_last_ = true;
    return incoming;
}

void MonitorModel::skip_to_end(
    const std::vector<vss::traffic_record>& records)
{
    if (records.empty())
    {
        have_last_ = false;
        last_when_ = vss::time_point{};
        last_data_ = nullptr;
        return;
    }
    last_when_ = records.back().when;
    last_data_ = static_cast<const void*>(records.back().data.get());
    have_last_ = true;
}

void MonitorModel::clear()
{
    if (!rows_.empty())
    {
        beginResetModel();
        rows_.clear();
        endResetModel();
    }
    have_last_ = false;
    last_when_ = vss::time_point{};
    last_data_ = nullptr;
}

void MonitorModel::set_hex_view(bool hex)
{
    if (hex_ == hex)
        return;
    hex_ = hex;
    if (!rows_.empty())
    {
        Q_EMIT dataChanged(index(0, PreviewColumn),
                         index(static_cast<int>(rows_.size()) - 1,
                               PreviewColumn));
    }
}

void MonitorModel::set_capacity(int rows)
{
    capacity_ = std::max(1, rows);
    if (static_cast<int>(rows_.size()) <= capacity_)
        return;

    const int remove_count = static_cast<int>(rows_.size()) - capacity_;
    beginRemoveRows(QModelIndex(), 0, remove_count - 1);
    rows_.erase(rows_.begin(), rows_.begin() + remove_count);
    endRemoveRows();
}

MonitorModel::Row MonitorModel::make_row(
    const vss::traffic_record& record) const
{
    Row row;
    row.name = QString::fromStdString(record.name);
    row.direction = record.direction;
    if (record.when == vss::time_point{})
    {
        row.when = QDateTime::currentDateTime();
    }
    else
    {
        const auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(
            record.when - steady_epoch_);
        row.when = wall_epoch_.addMSecs(static_cast<qint64>(delta.count()));
    }

    if (record.data)
    {
        row.length = static_cast<qint64>(record.data->size());
        const std::size_t count =
            std::min<std::size_t>(record.data->size(), preview_limit);
        row.preview = QByteArray(
            reinterpret_cast<const char*>(record.data->data()),
            static_cast<int>(count));
    }
    return row;
}

QString MonitorModel::preview_text(const Row& row) const
{
    const auto* bytes =
        reinterpret_cast<const vss::byte*>(row.preview.constData());
    const auto view =
        vss::bytes_view(bytes, static_cast<std::size_t>(row.preview.size()));

    const std::string rendered =
        hex_ ? vss::to_hex(view) : vss::to_printable(view);
    QString text = QString::fromUtf8(rendered.data(),
                                     static_cast<int>(rendered.size()));
    if (row.length > row.preview.size())
        text += QString::fromUtf8(preview_ellipsis);
    return text;
}

int MonitorModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(rows_.size());
}

int MonitorModel::columnCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return ColumnCount;
}

QVariant MonitorModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0
        || index.row() >= static_cast<int>(rows_.size()) || index.column() < 0
        || index.column() >= ColumnCount)
        return {};

    const auto& row = rows_[static_cast<std::size_t>(index.row())];
    switch (role)
    {
    case Qt::DisplayRole:
        switch (index.column())
        {
        case TimeColumn:
            return row.when.toString(QStringLiteral("HH:mm:ss.zzz"));
        case EndpointColumn:
            return row.name;
        case DirectionColumn:
            return QString::fromLatin1(vss::to_string(row.direction))
                .toUpper();
        case LengthColumn:
            return row.length;
        case PreviewColumn:
            return preview_text(row);
        default:
            break;
        }
        break;
    case Qt::TextAlignmentRole:
        if (index.column() == LengthColumn)
            return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
        if (index.column() == DirectionColumn)
            return static_cast<int>(Qt::AlignCenter);
        return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
    case Qt::ForegroundRole:
        if (index.column() == DirectionColumn)
        {
            return QBrush(row.direction == vss::data_direction::rx
                              ? QColor(0x09, 0x69, 0xda)
                              : QColor(0xbc, 0x4c, 0x00));
        }
        break;
    case Qt::FontRole:
        if (index.column() == PreviewColumn
            || index.column() == TimeColumn)
            return fixed_font();
        break;
    case Qt::ToolTipRole:
        return QStringLiteral("%1 bytes %2 %3 at %4\n%5")
            .arg(row.length)
            .arg(row.direction == vss::data_direction::rx ? tr("from")
                                                          : tr("to"))
            .arg(row.name)
            .arg(row.when.toString(QStringLiteral("HH:mm:ss.zzz")))
            .arg(preview_text(row));
    default:
        break;
    }
    return {};
}

QVariant MonitorModel::headerData(int section, Qt::Orientation orientation,
                                  int role) const
{
    if (orientation != Qt::Horizontal)
        return QAbstractTableModel::headerData(section, orientation, role);

    if (section < 0 || section >= ColumnCount)
        return {};

    if (role == Qt::DisplayRole)
        return tr(column_titles[section]);
    if (role == Qt::TextAlignmentRole)
    {
        if (section == LengthColumn)
            return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
        if (section == DirectionColumn)
            return static_cast<int>(Qt::AlignCenter);
        return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
    }
    return QAbstractTableModel::headerData(section, orientation, role);
}

} // namespace vss::gui
