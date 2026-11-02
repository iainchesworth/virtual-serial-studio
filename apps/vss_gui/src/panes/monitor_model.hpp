//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_PANES_MONITOR_MODEL_HPP
#define VSS_GUI_PANES_MONITOR_MODEL_HPP

#include "backend/view_types.hpp"

#include <QAbstractTableModel>
#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QVector>

#include <cstdint>
#include <vector>

namespace vss::gui
{

enum class traffic_direction
{
    rx,
    tx,
    drop
};

struct traffic_row
{
    QDateTime time;
    vss::endpoint_id endpoint = 0;
    QString port;
    traffic_direction direction = traffic_direction::rx;
    std::uint64_t bytes = 0;
    QByteArray preview;
    bool dropped = false;
};

/// Bounded traffic model.  Events arrive in coalesced batches from the
/// backend; the model never owns a widget per row.
class MonitorModel final : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum role
    {
        endpoint_role = Qt::UserRole + 1,
        direction_role,
        dropped_role,
        bytes_role
    };

    /// The design's named cap: 10,000 rows.
    static constexpr int max_rows = 10'000;
    static constexpr int preview_bytes = 256;

    explicit MonitorModel(QObject* parent = nullptr);

    void append_events(const event_batch& events);
    void append_drop(const QString& port, std::uint64_t bytes);
    void clear();
    void set_paused(bool paused);
    bool paused() const noexcept { return paused_; }
    void set_hex(bool hex);
    bool hex() const noexcept { return hex_; }
    void set_filters(vss::endpoint_id port, bool rx, bool tx, bool dropped,
                     const QString& search);
    void set_selected_port(vss::endpoint_id id);
    vss::endpoint_id selected_port() const noexcept { return selected_; }

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index,
                  int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    vss::endpoint_id port_at(int row) const;
    const traffic_row* row_at(int row) const;
    int total_rows() const noexcept { return static_cast<int>(rows_.size()); }
    QString export_text() const;

    static QString ascii_preview(const QByteArray& data);
    static QString hex_preview(const QByteArray& data);

private:
    void append_row(traffic_row row);
    void rebuild_visible();
    bool matches(const traffic_row& row) const;
    QString preview_for(const traffic_row& row) const;
    void trim();

    std::vector<traffic_row> rows_;
    QVector<int> visible_;
    vss::endpoint_id port_filter_ = 0;
    vss::endpoint_id selected_ = 0;
    QString search_;
    bool rx_ = true;
    bool tx_ = true;
    bool dropped_ = true;
    bool paused_ = false;
    bool hex_ = false;
};

} // namespace vss::gui

#endif // VSS_GUI_PANES_MONITOR_MODEL_HPP
