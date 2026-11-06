//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_PANES_PORTS_PANEL_HPP
#define VSS_GUI_PANES_PORTS_PANEL_HPP

#include "backend/studio_backend.hpp"
#include "panes/selection_service.hpp"
#include "widgets/port_menu.hpp"

#include <QWidget>

#include <functional>

class QAbstractItemView;
class QLineEdit;
class QListView;
class QStackedWidget;
class QTableView;

namespace vss::gui
{

class PortDelegate;
class PortModel;

/// The Ports dock: three-line Simple rows, or the Advanced table.
class PortsPanel final : public QWidget
{
    Q_OBJECT

public:
    PortsPanel(StudioBackend& backend, SelectionService& selection,
               QWidget* parent = nullptr);

    void set_advanced(bool advanced);
    void set_compact(bool compact);
    void refresh();

    vss::endpoint_id selected_port() const { return selection_.port(); }
    void select_port(vss::endpoint_id id);

    /// How a port's menu reads its Send, Monitor and fixture actions; see
    /// `add_port_menu_actions`.
    void set_menu_state_provider(
        std::function<port_menu_state(vss::endpoint_id)> provider)
    {
        menu_state_ = std::move(provider);
    }

Q_SIGNALS:
    void count_changed(int count);
    void inspect_requested(vss::endpoint_id id);
    void remove_requested(vss::endpoint_id id);
    void path_copied(const QString& path);
    /// Send bytes, repeat, Open in monitor, and a fixture's generator and
    /// echo, chosen from a row's menu.
    void port_action_requested(vss::endpoint_id id, port_menu_action action);
    void status_message(const QString& message);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void build();
    void on_current_changed(const QModelIndex& current);
    void sync_views(vss::endpoint_id id);
    void copy_selected_path();
    void open_menu(QAbstractItemView* view, const QPoint& position);
    int current_row() const;

    StudioBackend& backend_;
    SelectionService& selection_;
    PortModel* model_ = nullptr;
    QListView* simple_ = nullptr;
    QTableView* table_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QLineEdit* filter_ = nullptr;
    PortDelegate* simple_delegate_ = nullptr;
    PortDelegate* table_delegate_ = nullptr;
    std::function<port_menu_state(vss::endpoint_id)> menu_state_;
    bool advanced_ = false;
    bool compact_ = false;
    bool syncing_ = false;
};

} // namespace vss::gui

#endif // VSS_GUI_PANES_PORTS_PANEL_HPP
