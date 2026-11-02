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

#include <QMainWindow>
#include <QString>

class QAction;
class QDockWidget;
class QLabel;
class QLineEdit;
class QStackedWidget;
class QToolBar;

namespace vss::gui
{

class InspectorPanel;
class Launcher;
class MonitorPanel;
class PortsPanel;
class SendPanel;
class StatusBar;

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
    void run_job(const QString& job);
    void placeholder(const QString& what);

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

    QStackedWidget* central_ = nullptr;
    Launcher* launcher_ = nullptr;
    QWidget* workspace_ = nullptr;
    QDockWidget* ports_dock_ = nullptr;
    QDockWidget* inspector_dock_ = nullptr;
    QDockWidget* send_dock_ = nullptr;
    QDockWidget* monitor_dock_ = nullptr;
    QToolBar* toolbar_ = nullptr;
    QLineEdit* search_ = nullptr;
    StatusBar* status_ = nullptr;
    PortsPanel* ports_panel_ = nullptr;
    InspectorPanel* inspector_panel_ = nullptr;
    SendPanel* send_panel_ = nullptr;
    MonitorPanel* monitor_panel_ = nullptr;
    QAction* advanced_action_ = nullptr;
    QAction* monitor_action_ = nullptr;

    QString document_path_;
    QString last_signature_;
    bool dirty_ = false;
    bool loading_ = false;
    bool launcher_forced_ = false;
    bool compact_ = false;
    bool layout_sized_ = false;
    bool restored_state_ = false;
};

} // namespace vss::gui

#endif // VSS_GUI_SHELL_SHELL_WINDOW_HPP
