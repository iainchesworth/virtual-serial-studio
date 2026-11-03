//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_PANES_PORT_MODEL_HPP
#define VSS_GUI_PANES_PORT_MODEL_HPP

#include "backend/view_types.hpp"

#include <QAbstractTableModel>
#include <QString>
#include <QVector>

#include <cstdint>
#include <optional>
#include <vector>

namespace vss::gui
{

/// The six state words plus the two distinctive port kinds.
enum class port_state_kind
{
    open,
    waiting,
    connected,
    studio_only,
    closed,
    failed,
    fixture
};

/// One row per port.  Simple and Advanced share the model; only the view
/// changes.  No widgets are created per row.
class PortModel final : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum role
    {
        port_id_role = Qt::UserRole + 1,
        name_role,
        role_text_role,
        path_role,
        state_text_role,
        state_kind_role,
        bytes_rx_role,
        bytes_tx_role,
        dropped_role,
        serial_role
    };

    explicit PortModel(QObject* parent = nullptr);

    void set_snapshot(std::vector<port_view> ports,
                      std::vector<link_view> links);
    void set_filter(const QString& filter);
    QString filter() const { return filter_; }

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index,
                  int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    vss::endpoint_id port_id(int row) const;
    int row_for_id(vss::endpoint_id id) const;
    std::optional<port_view> port_at(int row) const;
    const std::vector<port_view>& ports() const noexcept { return ports_; }

    static QString state_text(const port_view& port);
    static port_state_kind state_kind(const port_view& port);
    static QString role_text(const port_view& port,
                             const std::vector<port_view>& ports,
                             const std::vector<link_view>& links);
    static QString serial_text(const port_view& port);

private:
    void rebuild_visible();
    bool matches(const port_view& port) const;

    std::vector<port_view> ports_;
    std::vector<link_view> links_;
    QVector<int> visible_;
    QString filter_;
};

} // namespace vss::gui

#endif // VSS_GUI_PANES_PORT_MODEL_HPP
