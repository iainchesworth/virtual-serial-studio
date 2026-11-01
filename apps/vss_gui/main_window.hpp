//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_MAIN_WINDOW_HPP
#define VSS_GUI_MAIN_WINDOW_HPP

#include <vss/core/endpoint.hpp>
#include <vss/core/link.hpp>
#include <vss/core/types.hpp>

#include <QElapsedTimer>
#include <QMainWindow>
#include <QPoint>
#include <QString>

#include <cstdint>
#include <unordered_map>
#include <vector>

class QAction;
class QCheckBox;
class QCloseEvent;
class QDockWidget;
class QLabel;
class QMenu;
class QPlainTextEdit;
class QPushButton;
class QTableView;
class QTimer;

namespace vss {
class studio;
}

namespace vss::gui {

class InspectorPanel;
class MonitorModel;
class PortTableModel;
class SendPanel;
class TopologyView;

/// The application shell: menus, toolbar, docks, polling and scenario glue.
class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(vss::studio& studio, QWidget* parent = nullptr);
    ~MainWindow() override;

    /// Headless end-to-end check used by `vss_gui --selftest`.
    bool run_selftest();

    /// Snapshots the studio, updates every model and feeds the monitor.
    void refresh_now();

protected:
    void closeEvent(QCloseEvent* event) override;

private Q_SLOTS:
    void poll();

    void on_add_virtual_port();
    void on_attach_serial();
    void on_add_mock_device();
    void on_remove_selected_ports();
    void on_ports_context_menu(const QPoint& position);
    void on_table_selection_changed();
    void on_topology_selection_changed(quint64 endpoint, quint64 link);
    void on_remove_selected_link();

    void on_new_document();
    void on_open_document();
    void on_save_document();
    void on_save_document_as();

    void on_share_serial();
    void on_join_publishers();
    void on_bridge_network();
    void on_null_modem_demo();

    void on_clear_monitor();
    void on_monitor_pause_toggled(bool paused);
    void on_monitor_hex_toggled(bool hex);
    void on_fit_topology();
    void on_about();

private:
    void create_actions();
    void create_menus();
    void create_toolbar();
    void create_docks();
    void create_statusbar();
    void connect_signals();
    void restore_settings();
    void save_settings();

    void apply_refresh(const std::vector<vss::endpoint_info>& endpoints,
                       const std::vector<vss::link_info>& links,
                       double elapsed_seconds);
    void feed_monitor();
    void log_differences(const std::vector<vss::endpoint_info>& endpoints,
                         const std::vector<vss::link_info>& links);
    void log_line(const QString& text);
    void show_error(const QString& title, const QString& detail);
    void set_dirty(bool dirty = true);
    void update_window_title();
    void update_action_states();
    void update_status(const std::vector<vss::endpoint_info>& endpoints,
                       const std::vector<vss::link_info>& links,
                       double elapsed_seconds);
    void set_active_endpoint(vss::endpoint_id id, bool sync_table);
    bool save_to(const QString& path);
    bool confirm_discard();
    void clear_topology();

    static bool has_endpoint(
        vss::endpoint_id id,
        const std::vector<vss::endpoint_info>& endpoints);
    static bool has_link(vss::link_id id,
                         const std::vector<vss::link_info>& links);
    static QString error_text(const vss::error_code& error);
    static QString describe_endpoints(
        const std::vector<vss::endpoint_info>& endpoints);

    vss::studio& studio_;

    PortTableModel* port_model_ = nullptr;
    MonitorModel* monitor_model_ = nullptr;
    TopologyView* topology_ = nullptr;
    InspectorPanel* inspector_ = nullptr;
    SendPanel* send_panel_ = nullptr;

    QTableView* ports_view_ = nullptr;
    QTableView* monitor_view_ = nullptr;
    QPlainTextEdit* log_view_ = nullptr;
    QPushButton* pause_button_ = nullptr;
    QCheckBox* hex_check_ = nullptr;
    QCheckBox* follow_check_ = nullptr;
    QLabel* monitor_count_label_ = nullptr;

    QLabel* port_count_label_ = nullptr;
    QLabel* link_count_label_ = nullptr;
    QLabel* throughput_label_ = nullptr;
    QLabel* hub_label_ = nullptr;

    QDockWidget* ports_dock_ = nullptr;
    QDockWidget* inspector_dock_ = nullptr;
    QDockWidget* send_dock_ = nullptr;
    QDockWidget* monitor_dock_ = nullptr;
    QDockWidget* log_dock_ = nullptr;
    QMenu* view_menu_ = nullptr;

    QAction* new_action_ = nullptr;
    QAction* open_action_ = nullptr;
    QAction* save_action_ = nullptr;
    QAction* save_as_action_ = nullptr;
    QAction* quit_action_ = nullptr;
    QAction* add_virtual_action_ = nullptr;
    QAction* attach_serial_action_ = nullptr;
    QAction* add_mock_action_ = nullptr;
    QAction* remove_port_action_ = nullptr;
    QAction* remove_link_action_ = nullptr;
    QAction* fit_action_ = nullptr;
    QAction* refresh_action_ = nullptr;
    QAction* share_action_ = nullptr;
    QAction* join_action_ = nullptr;
    QAction* bridge_action_ = nullptr;
    QAction* null_modem_action_ = nullptr;
    QAction* about_action_ = nullptr;

    QTimer* poll_timer_ = nullptr;
    QElapsedTimer rate_timer_;
    qint64 last_rate_ns_ = 0;
    std::uint64_t previous_rx_ = 0;
    std::uint64_t previous_tx_ = 0;

    std::unordered_map<vss::endpoint_id, vss::endpoint_info> previous_endpoints_;
    std::unordered_map<vss::link_id, vss::link_info> previous_links_;

    vss::endpoint_id selected_endpoint_ = 0;
    vss::link_id selected_link_ = 0;

    bool monitor_paused_ = false;
    QString document_path_;
    QString document_name_ = QStringLiteral("untitled");
    bool dirty_ = false;
};

} // namespace vss::gui

#endif // VSS_GUI_MAIN_WINDOW_HPP
