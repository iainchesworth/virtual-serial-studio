//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_MONITOR_MODEL_HPP
#define VSS_GUI_MONITOR_MODEL_HPP

#include <vss/core/stats.hpp>
#include <vss/core/traffic_log.hpp>
#include <vss/core/types.hpp>

#include <QAbstractTableModel>
#include <QByteArray>
#include <QDateTime>
#include <QString>

#include <cstdint>
#include <deque>
#include <vector>

namespace vss::gui {

/// Bounded live traffic log fed from `hub::traffic()`.
///
/// The hub keeps a ring buffer without sequence numbers, so the model
/// remembers the last record it consumed (its steady-clock stamp and its byte
/// block identity) and appends only what follows it.  When the marker has been
/// evicted the model clears and restarts from the newest records instead of
/// displaying duplicates.
class MonitorModel final : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column
    {
        TimeColumn = 0,
        EndpointColumn,
        DirectionColumn,
        LengthColumn,
        PreviewColumn,
        ColumnCount
    };

    explicit MonitorModel(QObject* parent = nullptr);

    /// Appends records newer than the last consumed one and returns how many
    /// rows were added.
    int append(const std::vector<vss::traffic_record>& records);

    /// Moves the "last consumed" marker to the newest record without adding
    /// rows.  Used after Clear so the cleared history does not come back.
    void skip_to_end(const std::vector<vss::traffic_record>& records);

    void clear();

    void set_hex_view(bool hex);
    bool hex_view() const noexcept { return hex_; }

    void set_capacity(int rows);
    int capacity() const noexcept { return capacity_; }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index,
                  int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

private:
    struct Row
    {
        QDateTime when;
        QString name;
        vss::data_direction direction = vss::data_direction::rx;
        qint64 length = 0;
        QByteArray preview;
    };

    Row make_row(const vss::traffic_record& record) const;
    QString preview_text(const Row& row) const;

    std::deque<Row> rows_;
    int capacity_ = 2000;
    bool hex_ = false;

    bool have_last_ = false;
    vss::time_point last_when_{};
    const void* last_data_ = nullptr;

    QDateTime wall_epoch_;
    vss::time_point steady_epoch_;
};

} // namespace vss::gui

#endif // VSS_GUI_MONITOR_MODEL_HPP
