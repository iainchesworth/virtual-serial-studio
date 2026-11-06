//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_SHELL_SHELL_WINDOW_HPP
#define VSS_GUI_SHELL_SHELL_WINDOW_HPP

#include "backend/studio_backend.hpp"
#include "panes/selection_service.hpp"
#include "shell/action_registry.hpp"
#include "shell/level_service.hpp"
#include "shell/undo_service.hpp"
#include "support/event_log.hpp"
#include "widgets/port_menu.hpp"

#include <QMainWindow>
#include <QString>

#include <vector>

class QAction;
class QDockWidget;
class QLabel;
class QLineEdit;
class QStackedWidget;
class QToolBar;

namespace vss::gui
{

class EventLogPanel;
class InspectorPanel;
class Launcher;
class FixtureBehaviours;
class MonitorPanel;
class OwnerBar;
class PortsPanel;
class SendPanel;
class StatusBar;
class TopologyJsonEditor;

namespace topology
{
class LinksList;
class TopologyView;
} // namespace topology

/// The redesigned frame: menu, toolbar, launcher, docks and status bar.
///
/// The panes are placeholders until WP3 to WP8.  Every action is registered
/// once and the menus, toolbar and palette are built from that registry.
class ShellWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit ShellWindow(StudioBackend& backend, QWidget* parent = nullptr);
    ~ShellWindow() override;

    bool run_selftest();

protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void build_actions();
    void build_menus();
    void build_toolbar();
    void build_docks();
    void build_central();
    void build_status();

    void connect_backend();
    void on_backend_changed();
    void on_level_changed(bool advanced);
    void update_level_ui();
    void update_content();
    void update_title();
    void update_actions();
    void update_status();
    void update_dock_titles();
    void apply_compact();
    QString document_signature() const;
    void remove_selected_port();
    void copy_selected_port();
    void focus_inspector();

    void show_launcher();
    void show_workspace();
    void open_palette();
    void open_connect_ports();
    void open_share_wizard();
    void open_network_endpoint();
    void open_templates();
    void show_monitor();
    /// Shows the Send dock and puts the cursor in its input.
    void show_send();
    port_menu_state port_menu_state_for(vss::endpoint_id id) const;
    void run_port_action(vss::endpoint_id id, port_menu_action action);
    void run_job(const QString& job);
    void placeholder(const QString& what);
    void later_package(const QString& what, const QString& package);

    void refresh_topology();
    void sync_selection();
    void validate_selection();
    void copy_port_name(vss::endpoint_id id);
    void remove_selection();
    void remove_selected_link();
    void remove_link(vss::link_id id);
    void undo_last();
    void redo_last();
    void update_undo_actions();
    void remove_port(vss::endpoint_id id);
    void show_rename(vss::endpoint_id id);
    void show_links_dock(bool shown);
    void connect_from_port(vss::endpoint_id id);

    // Advanced tools, WP7.  Each one replaces a placeholder handler with the
    // real dialog or dock; unsupported capabilities stay disabled inside.
    void open_add_fixture();
    void open_wiring();
    void open_benchmark();
    void open_export_command();
    void show_owner_controls();
    void update_owner_bar();
    void sync_advanced_docks();
    void log_local(const QString& text);

    void open_setup_check();
    void open_settings();
    void open_troubleshoot();
    void open_shortcuts();
    void open_about();
    void open_help_contents();
    void open_bundle();
    void copy_environment();
    void restart_elevated();

    // WP5 basic flows.  Each opens one real dialog and acts on its result.
    void show_add_port(bool pair_selected = false);
    void show_pair();
    void show_attach_device();
    void show_watch();
    void show_port_ready(vss::endpoint_id port, const QString& virtual_kind);
    void focus_monitor(vss::endpoint_id port);

    void new_document();
    void open_document();
    void open_path(const QString& path);
    void save_document();
    void save_document_as();
    bool confirm_discard();
    void add_recent(const QString& path, const QString& summary);
    void set_dirty(bool dirty);

    void save_layout();
    void restore_layout();
    void reset_layout();
    void apply_dock_sizes();
    void apply_theme_setting();
    void apply_density_setting();
    void apply_menu_palette();

    StudioBackend& backend_;
    ActionRegistry registry_{this};
    LevelService level_{this};
    SelectionService selection_{this};
    support::EventLog support_log_{this};

    QStackedWidget* central_ = nullptr;
    Launcher* launcher_ = nullptr;
    QWidget* workspace_ = nullptr;
    topology::TopologyView* topology_ = nullptr;
    topology::LinksList* links_ = nullptr;
    QDockWidget* ports_dock_ = nullptr;
    QDockWidget* inspector_dock_ = nullptr;
    QDockWidget* send_dock_ = nullptr;
    QDockWidget* monitor_dock_ = nullptr;
    QDockWidget* links_dock_ = nullptr;
    QDockWidget* log_dock_ = nullptr;
    QDockWidget* json_dock_ = nullptr;
    QToolBar* toolbar_ = nullptr;
    QLineEdit* search_ = nullptr;
    StatusBar* status_ = nullptr;
    PortsPanel* ports_panel_ = nullptr;
    InspectorPanel* inspector_panel_ = nullptr;
    SendPanel* send_panel_ = nullptr;
    FixtureBehaviours* behaviours_ = nullptr;
    MonitorPanel* monitor_panel_ = nullptr;
    OwnerBar* owner_bar_ = nullptr;
    EventLogPanel* event_log_ = nullptr;
    TopologyJsonEditor* json_editor_ = nullptr;
    QAction* advanced_action_ = nullptr;
    QAction* monitor_action_ = nullptr;
    QAction* links_action_ = nullptr;
    QAction* log_action_ = nullptr;
    QAction* json_action_ = nullptr;

    QString document_path_;
    QString last_signature_;
    QString title_override_;
    bool dirty_ = false;
    bool loading_ = false;
    UndoService undo_;
    /// The ports the history was recorded against; a change to the set
    /// clears the history.
    std::vector<vss::endpoint_id> history_ports_;
    bool launcher_forced_ = false;
    bool compact_ = false;
    bool layout_sized_ = false;
    bool restored_state_ = false;
    bool restarting_ = false;
};

} // namespace vss::gui

#endif // VSS_GUI_SHELL_SHELL_WINDOW_HPP
