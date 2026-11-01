//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_PORT_MODEL_HPP
#define VSS_GUI_PORT_MODEL_HPP

#include <vss/core/endpoint.hpp>
#include <vss/core/types.hpp>

#include <QAbstractTableModel>

#include <cstddef>
#include <vector>

namespace vss::gui {

/// Table of the studio endpoints, refreshed from a snapshot on every poll.
///
/// Rows are ordered by endpoint id (the order the hub reports).  When the set
/// of ids changes the model resets, which tells the view to rebuild its
/// selection; statistical changes only emit dataChanged.
class PortTableModel final : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column
    {
        NameColumn = 0,
        KindColumn,
        PathColumn,
        StateColumn,
        LinesColumn,
        RxColumn,
        TxColumn,
        ErrorsColumn,
        ColumnCount
    };

    explicit PortTableModel(QObject* parent = nullptr);

    /// Replaces the snapshot.  Returns true when rows were added, removed or
    /// reordered (the model was reset).
    bool set_endpoints(std::vector<vss::endpoint_info> endpoints);

    const std::vector<vss::endpoint_info>& endpoints() const noexcept
    {
        return endpoints_;
    }

    const vss::endpoint_info* endpoint_at(int row) const;
    const vss::endpoint_info* find_endpoint(vss::endpoint_id id) const;
    QModelIndex index_for_id(vss::endpoint_id id) const;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index,
                  int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

private:
    std::vector<vss::endpoint_info> endpoints_;
};

} // namespace vss::gui

#endif // VSS_GUI_PORT_MODEL_HPP
