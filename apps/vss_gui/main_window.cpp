//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "main_window.hpp"

#include "about_dialog.hpp"
#include "connect_dialog.hpp"
#include "dialogs.hpp"
#include "inspector_panel.hpp"
#include "monitor_model.hpp"
#include "port_model.hpp"
#include "send_panel.hpp"
#include "theme.hpp"
#include "topology_view.hpp"

#include <vss/config/document.hpp>
#include <vss/studio/studio.hpp>

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QItemSelectionModel>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSize>
#include <QStatusBar>
#include <QStyle>
#include <QStringList>
#include <QSysInfo>
#include <QTableView>
#include <QTabWidget>
#include <QTime>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace vss::gui {
namespace {

constexpr int poll_interval_ms = 250;

QString mode_text(const vss::link_info& info)
{
    if (info.mode == vss::link_mode::duplex)
    {
        if (info.sources.size() == 1 && info.targets.size() > 1)
            return QStringLiteral("split");
        return QStringLiteral("duplex");
    }
    if (!info.options.emulate_modem_lines)
        return QStringLiteral("monitor");
    if (info.sources.size() > 1)
        return QStringLiteral("join");
    return QStringLiteral("redirect");
}

QLabel* make_status_label(QWidget* parent, const QString& text, int width)
{
    auto* label = new QLabel(text, parent);
    label->setMinimumWidth(width);
    label->setAlignment(Qt::AlignCenter);
    return label;
}

/// The configuration library takes narrow std::string paths; encodeName maps
/// them to the local file-system encoding the C runtime expects.
std::string local_path(const QString& path)
{
    return QFile::encodeName(path).toStdString();
}

} // namespace

MainWindow::MainWindow(vss::studio& studio, QWidget* parent)
    : QMainWindow(parent)
    , studio_(studio)
{
    setObjectName(QStringLiteral("main_window"));
    resize(1440, 920);
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowTabbedDocks
                   | QMainWindow::AllowNestedDocks);
    setTabPosition(Qt::LeftDockWidgetArea, QTabWidget::North);
    setTabPosition(Qt::RightDockWidgetArea, QTabWidget::North);
    setWindowIcon(style()->standardIcon(QStyle::SP_ComputerIcon));

    port_model_ = new PortTableModel(this);
    monitor_model_ = new MonitorModel(this);
    topology_ = new TopologyView(this);
    inspector_ = new InspectorPanel(studio_, this);
    send_panel_ = new SendPanel(studio_, this);
    setCentralWidget(topology_);

    create_actions();
    create_menus();
    create_docks();
    create_toolbar();
    create_statusbar();
    connect_signals();
    restore_settings();

    rate_timer_.start();
    poll_timer_ = new QTimer(this);
    poll_timer_->setInterval(poll_interval_ms);
    connect(poll_timer_, &QTimer::timeout, this, &MainWindow::poll);
    poll_timer_->start();

    log_line(tr("Virtual Serial Studio %1 started with Qt %2.")
                 .arg(QStringLiteral(VSS_GUI_VERSION),
                      QString::fromLatin1(qVersion())));
    refresh_now();
    update_window_title();
    statusBar()->showMessage(tr("Ready. Use Ports → Add virtual port to begin."),
                             6000);
}

MainWindow::~MainWindow()
{
    if (poll_timer_)
        poll_timer_->stop();
}

void MainWindow::create_actions()
{
    const auto icon = [this](QStyle::StandardPixmap pixmap) {
        return style()->standardIcon(pixmap);
    };

    new_action_ = new QAction(icon(QStyle::SP_FileIcon), tr("&New"), this);
    new_action_->setShortcut(QKeySequence::New);
    new_action_->setStatusTip(tr("Start an empty topology."));
    connect(new_action_, &QAction::triggered, this,
            &MainWindow::on_new_document);

    open_action_ =
        new QAction(icon(QStyle::SP_DialogOpenButton), tr("&Open…"), this);
    open_action_->setShortcut(QKeySequence::Open);
    open_action_->setStatusTip(tr("Open a saved topology."));
    connect(open_action_, &QAction::triggered, this,
            &MainWindow::on_open_document);

    save_action_ =
        new QAction(icon(QStyle::SP_DialogSaveButton), tr("&Save"), this);
    save_action_->setShortcut(QKeySequence::Save);
    save_action_->setStatusTip(tr("Save the current topology."));
    connect(save_action_, &QAction::triggered, this,
            &MainWindow::on_save_document);

    save_as_action_ = new QAction(tr("Save &As…"), this);
    save_as_action_->setShortcut(QKeySequence::SaveAs);
    save_as_action_->setStatusTip(tr("Save the topology under a new name."));
    connect(save_as_action_, &QAction::triggered, this,
            &MainWindow::on_save_document_as);

    quit_action_ = new QAction(tr("&Quit"), this);
    quit_action_->setShortcut(QKeySequence::Quit);
    quit_action_->setStatusTip(tr("Close the application."));
    connect(quit_action_, &QAction::triggered, this, &QWidget::close);

    add_virtual_action_ =
        new QAction(tr("Add &virtual port…"), this);
    add_virtual_action_->setStatusTip(
        tr("Create an operating-system virtual port."));
    connect(add_virtual_action_, &QAction::triggered, this,
            &MainWindow::on_add_virtual_port);

    attach_serial_action_ =
        new QAction(tr("&Attach serial port…"), this);
    attach_serial_action_->setStatusTip(
        tr("Open a detected serial device or a network URI."));
    connect(attach_serial_action_, &QAction::triggered, this,
            &MainWindow::on_attach_serial);

    add_mock_action_ = new QAction(tr("Add &mock device…"), this);
    add_mock_action_->setStatusTip(
        tr("Create a scriptable in-memory device."));
    connect(add_mock_action_, &QAction::triggered, this,
            &MainWindow::on_add_mock_device);

    remove_port_action_ =
        new QAction(icon(QStyle::SP_TrashIcon), tr("&Remove port"), this);
    remove_port_action_->setShortcut(QKeySequence::Delete);
    remove_port_action_->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    remove_port_action_->setStatusTip(
        tr("Remove the selected port and its links."));
    remove_port_action_->setEnabled(false);
    connect(remove_port_action_, &QAction::triggered, this,
            &MainWindow::on_remove_selected_ports);

    remove_link_action_ = new QAction(tr("Remove selected &link"), this);
    remove_link_action_->setShortcut(QKeySequence::Delete);
    remove_link_action_->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    remove_link_action_->setStatusTip(
        tr("Remove the link selected in the topology view."));
    remove_link_action_->setEnabled(false);
    connect(remove_link_action_, &QAction::triggered, this,
            &MainWindow::on_remove_selected_link);

    fit_action_ = new QAction(tr("&Fit topology"), this);
    fit_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+0")));
    fit_action_->setStatusTip(tr("Fit every node into the topology view."));
    connect(fit_action_, &QAction::triggered, this,
            &MainWindow::on_fit_topology);

    refresh_action_ = new QAction(tr("&Refresh now"), this);
    refresh_action_->setShortcut(QKeySequence(QStringLiteral("F5")));
    refresh_action_->setStatusTip(tr("Snapshot the studio immediately."));
    connect(refresh_action_, &QAction::triggered, this,
            [this] { refresh_now(); });

    share_action_ = new QAction(tr("Share a serial port…"), this);
    share_action_->setStatusTip(
        tr("Open one port once and split it to several listeners."));
    connect(share_action_, &QAction::triggered, this,
            &MainWindow::on_share_serial);

    join_action_ = new QAction(tr("Join publishers…"), this);
    join_action_->setStatusTip(tr("Merge several ports into one target."));
    connect(join_action_, &QAction::triggered, this,
            &MainWindow::on_join_publishers);

    bridge_action_ = new QAction(tr("Bridge to network…"), this);
    bridge_action_->setStatusTip(tr("Connect a local port to a network URI."));
    connect(bridge_action_, &QAction::triggered, this,
            &MainWindow::on_bridge_network);

    null_modem_action_ = new QAction(tr("Null-modem loopback"), this);
    null_modem_action_->setStatusTip(
        tr("Create two virtual ports connected by a demo link."));
    connect(null_modem_action_, &QAction::triggered, this,
            &MainWindow::on_null_modem_demo);

    connect_action_ = new QAction(tr("&Connect ports…"), this);
    connect_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+L")));
    connect_action_->setStatusTip(
        tr("Wire two or more ports: two-way, redirect, monitor, split or "
           "join."));
    connect(connect_action_, &QAction::triggered, this,
            &MainWindow::on_connect_ports);

    settings_action_ = new QAction(tr("&Settings…"), this);
    settings_action_->setShortcut(QKeySequence::Preferences);
    settings_action_->setStatusTip(
        tr("Appearance, refresh rate and the default baud rate."));
    connect(settings_action_, &QAction::triggered, this,
            &MainWindow::on_settings);

    advanced_action_ = new QAction(tr("&Advanced mode"), this);
    advanced_action_->setCheckable(true);
    advanced_action_->setStatusTip(
        tr("Show the fixtures, topology and networking tools."));
    connect(advanced_action_, &QAction::toggled, this,
            &MainWindow::on_toggle_advanced);

    about_action_ = new QAction(tr("&About Virtual Serial Studio"), this);
    connect(about_action_, &QAction::triggered, this, &MainWindow::on_about);
}

void MainWindow::create_menus()
{
    auto* file_menu = menuBar()->addMenu(tr("&File"));
    file_menu->addAction(new_action_);
    file_menu->addAction(open_action_);
    file_menu->addSeparator();
    file_menu->addAction(save_action_);
    file_menu->addAction(save_as_action_);
    file_menu->addSeparator();
    file_menu->addAction(quit_action_);

    auto* ports_menu = menuBar()->addMenu(tr("&Ports"));
    ports_menu->addAction(add_virtual_action_);
    ports_menu->addAction(attach_serial_action_);
    ports_menu->addAction(add_mock_action_);
    ports_menu->addSeparator();
    ports_menu->addAction(connect_action_);
    ports_menu->addSeparator();
    ports_menu->addAction(remove_port_action_);

    auto* topology_menu = menuBar()->addMenu(tr("&Topology"));
    topology_menu->addAction(fit_action_);
    topology_menu->addAction(remove_link_action_);
    topology_menu->addSeparator();
    topology_menu->addAction(refresh_action_);

    auto* scenarios_menu = menuBar()->addMenu(tr("&Scenarios"));
    scenarios_menu->addAction(share_action_);
    scenarios_menu->addAction(join_action_);
    scenarios_menu->addAction(bridge_action_);
    scenarios_menu->addSeparator();
    scenarios_menu->addAction(null_modem_action_);

    view_menu_ = menuBar()->addMenu(tr("&View"));
    view_menu_->addAction(advanced_action_);
    view_menu_->addSeparator();

    auto* tools_menu = menuBar()->addMenu(tr("&Tools"));
    tools_menu->addAction(settings_action_);

    auto* help_menu = menuBar()->addMenu(tr("&Help"));
    help_menu->addAction(about_action_);
    help_menu->addAction(tr("About &Qt"), this,
                         [] { QApplication::aboutQt(); });
}

void MainWindow::create_docks()
{
    ports_view_ = new QTableView(this);
    ports_view_->setObjectName(QStringLiteral("ports_table"));
    ports_view_->setModel(port_model_);
    ports_view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    ports_view_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    ports_view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ports_view_->setAlternatingRowColors(true);
    ports_view_->setShowGrid(false);
    ports_view_->setWordWrap(false);
    ports_view_->setContextMenuPolicy(Qt::CustomContextMenu);
    ports_view_->verticalHeader()->setVisible(false);
    ports_view_->verticalHeader()->setDefaultSectionSize(24);
    ports_view_->horizontalHeader()->setHighlightSections(false);
    ports_view_->horizontalHeader()->setStretchLastSection(false);
    ports_view_->horizontalHeader()->setSectionResizeMode(
        QHeaderView::Interactive);
    ports_view_->horizontalHeader()->setSectionResizeMode(
        PortTableModel::PathColumn, QHeaderView::Stretch);
    ports_view_->setColumnWidth(PortTableModel::NameColumn, 150);
    ports_view_->setColumnWidth(PortTableModel::KindColumn, 80);
    ports_view_->setColumnWidth(PortTableModel::StateColumn, 78);
    ports_view_->setColumnWidth(PortTableModel::LinesColumn, 150);
    ports_view_->setColumnWidth(PortTableModel::RxColumn, 88);
    ports_view_->setColumnWidth(PortTableModel::TxColumn, 88);
    ports_view_->setColumnWidth(PortTableModel::ErrorsColumn, 66);

    auto* ports_panel = new QWidget(this);
    auto* ports_layout = new QVBoxLayout(ports_panel);
    ports_layout->setContentsMargins(0, 0, 0, 0);
    ports_layout->setSpacing(0);
    auto* ports_toolbar = new QToolBar(ports_panel);
    ports_toolbar->setObjectName(QStringLiteral("ports_toolbar"));
    ports_toolbar->setIconSize(QSize(16, 16));
    ports_toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    ports_toolbar->addAction(add_virtual_action_);
    ports_toolbar->addAction(attach_serial_action_);
    ports_toolbar->addAction(add_mock_action_);
    ports_toolbar->addSeparator();
    ports_toolbar->addAction(connect_action_);
    ports_toolbar->addAction(remove_port_action_);
    ports_layout->addWidget(ports_toolbar);
    ports_layout->addWidget(ports_view_, 1);

    ports_dock_ = new QDockWidget(tr("Ports"), this);
    ports_dock_->setObjectName(QStringLiteral("ports_dock"));
    ports_dock_->setWidget(ports_panel);
    ports_dock_->setMinimumWidth(320);
    addDockWidget(Qt::LeftDockWidgetArea, ports_dock_);

    inspector_dock_ = new QDockWidget(tr("Inspector"), this);
    inspector_dock_->setObjectName(QStringLiteral("inspector_dock"));
    auto* inspector_scroll = new QScrollArea(inspector_dock_);
    inspector_scroll->setObjectName(QStringLiteral("inspector_scroll"));
    inspector_scroll->setWidgetResizable(true);
    inspector_scroll->setFrameShape(QFrame::NoFrame);
    inspector_scroll->setWidget(inspector_);
    inspector_dock_->setWidget(inspector_scroll);
    inspector_dock_->setMinimumWidth(320);
    addDockWidget(Qt::RightDockWidgetArea, inspector_dock_);

    send_dock_ = new QDockWidget(tr("Send"), this);
    send_dock_->setObjectName(QStringLiteral("send_dock"));
    send_dock_->setWidget(send_panel_);
    addDockWidget(Qt::RightDockWidgetArea, send_dock_);
    splitDockWidget(inspector_dock_, send_dock_, Qt::Vertical);

    auto* monitor_panel = new QWidget(this);
    auto* monitor_layout = new QVBoxLayout(monitor_panel);
    monitor_layout->setContentsMargins(6, 6, 6, 6);
    monitor_layout->setSpacing(6);

    auto* controls = new QHBoxLayout();
    pause_button_ = new QPushButton(tr("Pause"), monitor_panel);
    pause_button_->setCheckable(true);
    pause_button_->setToolTip(tr("Stop appending new traffic to the monitor."));
    auto* clear_button = new QPushButton(tr("Clear"), monitor_panel);
    clear_button->setToolTip(tr("Remove every row currently displayed."));
    hex_check_ = new QCheckBox(tr("Hex"), monitor_panel);
    hex_check_->setToolTip(tr("Show payloads as hexadecimal instead of ASCII."));
    follow_check_ = new QCheckBox(tr("Follow"), monitor_panel);
    follow_check_->setChecked(true);
    follow_check_->setToolTip(tr("Scroll to the newest row automatically."));
    monitor_count_label_ = new QLabel(tr("0 rows"), monitor_panel);
    monitor_count_label_->setMinimumWidth(80);
    monitor_count_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    controls->addWidget(pause_button_);
    controls->addWidget(clear_button);
    controls->addSpacing(8);
    controls->addWidget(hex_check_);
    controls->addWidget(follow_check_);
    controls->addStretch(1);
    controls->addWidget(monitor_count_label_);
    monitor_layout->addLayout(controls);

    monitor_view_ = new QTableView(monitor_panel);
    monitor_view_->setObjectName(QStringLiteral("monitor_table"));
    monitor_view_->setModel(monitor_model_);
    monitor_view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    monitor_view_->setSelectionMode(QAbstractItemView::SingleSelection);
    monitor_view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    monitor_view_->setAlternatingRowColors(true);
    monitor_view_->setShowGrid(false);
    monitor_view_->setWordWrap(false);
    monitor_view_->setTextElideMode(Qt::ElideRight);
    monitor_view_->verticalHeader()->setVisible(false);
    monitor_view_->verticalHeader()->setDefaultSectionSize(22);
    monitor_view_->horizontalHeader()->setHighlightSections(false);
    monitor_view_->horizontalHeader()->setStretchLastSection(false);
    monitor_view_->horizontalHeader()->setSectionResizeMode(
        QHeaderView::Interactive);
    monitor_view_->horizontalHeader()->setSectionResizeMode(
        MonitorModel::PreviewColumn, QHeaderView::Stretch);
    monitor_view_->setColumnWidth(MonitorModel::TimeColumn, 98);
    monitor_view_->setColumnWidth(MonitorModel::EndpointColumn, 140);
    monitor_view_->setColumnWidth(MonitorModel::DirectionColumn, 72);
    monitor_view_->setColumnWidth(MonitorModel::LengthColumn, 72);
    monitor_layout->addWidget(monitor_view_, 1);

    monitor_dock_ = new QDockWidget(tr("Monitor"), this);
    monitor_dock_->setObjectName(QStringLiteral("monitor_dock"));
    monitor_dock_->setWidget(monitor_panel);
    addDockWidget(Qt::BottomDockWidgetArea, monitor_dock_);

    log_view_ = new QPlainTextEdit(this);
    log_view_->setObjectName(QStringLiteral("log_view"));
    log_view_->setReadOnly(true);
    log_view_->setMaximumBlockCount(5000);
    log_view_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    log_dock_ = new QDockWidget(tr("Log"), this);
    log_dock_->setObjectName(QStringLiteral("log_dock"));
    log_dock_->setWidget(log_view_);
    addDockWidget(Qt::BottomDockWidgetArea, log_dock_);
    tabifyDockWidget(monitor_dock_, log_dock_);
    monitor_dock_->raise();

    view_menu_->addSeparator();
    view_menu_->addAction(ports_dock_->toggleViewAction());
    view_menu_->addAction(inspector_dock_->toggleViewAction());
    view_menu_->addAction(send_dock_->toggleViewAction());
    view_menu_->addAction(monitor_dock_->toggleViewAction());
    view_menu_->addAction(log_dock_->toggleViewAction());
    view_menu_->addSeparator();
    view_menu_->addAction(fit_action_);

    resizeDocks({ports_dock_, inspector_dock_}, {360, 360}, Qt::Horizontal);
    resizeDocks({monitor_dock_}, {260}, Qt::Vertical);

    connect(pause_button_, &QPushButton::toggled, this,
            &MainWindow::on_monitor_pause_toggled);
    connect(clear_button, &QPushButton::clicked, this,
            &MainWindow::on_clear_monitor);
    connect(hex_check_, &QCheckBox::toggled, this,
            &MainWindow::on_monitor_hex_toggled);
}

void MainWindow::create_toolbar()
{
    auto* toolbar = addToolBar(tr("Main"));
    toolbar->setObjectName(QStringLiteral("main_toolbar"));
    toolbar->setMovable(true);
    toolbar->setIconSize(QSize(18, 18));
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolbar->addAction(new_action_);
    toolbar->addAction(open_action_);
    toolbar->addAction(save_action_);
    toolbar->addSeparator();
    toolbar->addAction(add_virtual_action_);
    toolbar->addAction(attach_serial_action_);
    toolbar->addAction(add_mock_action_);
    toolbar->addAction(remove_port_action_);
    toolbar->addSeparator();
    toolbar->addAction(connect_action_);
    toolbar->addSeparator();
    toolbar->addAction(null_modem_action_);
    toolbar->addAction(share_action_);
    toolbar->addSeparator();
    toolbar->addAction(fit_action_);
}

void MainWindow::create_statusbar()
{
    port_count_label_ = make_status_label(this, tr("Ports: 0"), 90);
    link_count_label_ = make_status_label(this, tr("Links: 0"), 80);
    throughput_label_ = make_status_label(this, tr("RX 0 B/s · TX 0 B/s"), 250);
    hub_label_ = make_status_label(this, tr("Hub: stopped"), 110);
    port_count_label_->setToolTip(tr("Endpoints currently registered."));
    link_count_label_->setToolTip(tr("Links currently registered."));
    throughput_label_->setToolTip(
        tr("Aggregate endpoint throughput measured between polls."));
    hub_label_->setToolTip(tr("Hub worker threads."));

    statusBar()->addPermanentWidget(port_count_label_);
    statusBar()->addPermanentWidget(link_count_label_);
    statusBar()->addPermanentWidget(throughput_label_);
    statusBar()->addPermanentWidget(hub_label_);
}

void MainWindow::connect_signals()
{
    if (ports_view_->selectionModel())
    {
        connect(ports_view_->selectionModel(),
                &QItemSelectionModel::selectionChanged, this,
                &MainWindow::on_table_selection_changed);
    }
    connect(ports_view_, &QTableView::customContextMenuRequested, this,
            &MainWindow::on_ports_context_menu);
    connect(topology_, &TopologyView::selection_changed, this,
            &MainWindow::on_topology_selection_changed);

    connect(inspector_, &InspectorPanel::status_message, this,
            [this](const QString& text, bool error) {
                log_line(text);
                statusBar()->showMessage(text, error ? 10000 : 4000);
            });
    connect(send_panel_, &SendPanel::status_message, this,
            [this](const QString& text, bool error) {
                log_line(text);
                statusBar()->showMessage(text, error ? 10000 : 4000);
            });

    ports_view_->addAction(remove_port_action_);
    topology_->addAction(remove_link_action_);
}

void MainWindow::restore_settings()
{
    if (QGuiApplication::platformName() == QLatin1String("offscreen"))
        return;

    QSettings settings;
    const QByteArray geometry =
        settings.value(QStringLiteral("main/geometry")).toByteArray();
    if (!geometry.isEmpty())
        restoreGeometry(geometry);
    const QByteArray state =
        settings.value(QStringLiteral("main/state")).toByteArray();
    if (!state.isEmpty())
        restoreState(state);

    settings_ = load_ui_settings();
    apply_ui_settings(settings_);
}

void MainWindow::save_settings()
{
    if (QGuiApplication::platformName() == QLatin1String("offscreen"))
        return;

    QSettings settings;
    settings.setValue(QStringLiteral("main/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("main/state"), saveState());
    save_ui_settings(settings_);
}

void MainWindow::poll()
{
    refresh_now();
}

void MainWindow::refresh_now()
{
    const std::vector<vss::endpoint_info> endpoints = studio_.endpoint_infos();
    const std::vector<vss::link_info> links = studio_.link_infos();

    const qint64 now = rate_timer_.isValid() ? rate_timer_.nsecsElapsed() : 0;
    double elapsed = 0.0;
    if (last_rate_ns_ > 0 && now > last_rate_ns_)
        elapsed = static_cast<double>(now - last_rate_ns_) / 1.0e9;
    last_rate_ns_ = now;

    apply_refresh(endpoints, links, elapsed);
}

void MainWindow::apply_refresh(
    const std::vector<vss::endpoint_info>& endpoints,
    const std::vector<vss::link_info>& links, double elapsed_seconds)
{
    log_differences(endpoints, links);

    const bool structure_changed = port_model_->set_endpoints(endpoints);
    topology_->set_topology(endpoints, links);

    if (selected_endpoint_ != 0
        && !has_endpoint(selected_endpoint_, endpoints))
    {
        set_active_endpoint(0, true);
        topology_->clear_selection();
    }
    else if (structure_changed && selected_endpoint_ != 0)
    {
        set_active_endpoint(selected_endpoint_, true);
    }

    if (selected_link_ != 0 && !has_link(selected_link_, links))
        selected_link_ = 0;

    inspector_->update_from(endpoints, elapsed_seconds);
    send_panel_->set_endpoints(endpoints);
    update_status(endpoints, links, elapsed_seconds);
    update_action_states();

    previous_endpoints_.clear();
    for (const auto& info : endpoints)
        previous_endpoints_.emplace(info.id, info);
    previous_links_.clear();
    for (const auto& link : links)
        previous_links_.emplace(link.id, link);

    feed_monitor();
}

void MainWindow::feed_monitor()
{
    if (monitor_count_label_)
    {
        monitor_count_label_->setText(
            tr("%1 rows").arg(monitor_model_->rowCount()));
    }
    if (monitor_paused_)
        return;

    const std::vector<vss::traffic_record> records = studio_.engine().traffic();
    const int added = monitor_model_->append(records);
    if (added > 0 && monitor_view_ && follow_check_
        && follow_check_->isChecked())
    {
        monitor_view_->scrollToBottom();
    }
}

void MainWindow::log_differences(
    const std::vector<vss::endpoint_info>& endpoints,
    const std::vector<vss::link_info>& links)
{
    const auto name_of = [&](vss::endpoint_id id) -> QString {
        for (const auto& info : endpoints)
        {
            if (info.id == id)
                return QString::fromStdString(info.name);
        }
        const auto previous = previous_endpoints_.find(id);
        if (previous != previous_endpoints_.end())
            return QString::fromStdString(previous->second.name);
        return QStringLiteral("#%1").arg(static_cast<qulonglong>(id));
    };

    const auto link_label = [&](const vss::link_info& link) -> QString {
        QStringList sources;
        QStringList targets;
        for (const auto id : link.sources)
            sources << name_of(id);
        for (const auto id : link.targets)
            targets << name_of(id);
        return tr("#%1 %2 → %3 [%4]")
            .arg(static_cast<qulonglong>(link.id))
            .arg(sources.join(QStringLiteral(", ")))
            .arg(targets.join(QStringLiteral(", ")))
            .arg(mode_text(link));
    };

    for (const auto& info : endpoints)
    {
        const auto previous = previous_endpoints_.find(info.id);
        if (previous == previous_endpoints_.end())
        {
            log_line(tr("Port added: %1 (%2, %3)")
                         .arg(QString::fromStdString(info.name),
                              kind_text(info.kind),
                              info.path.empty()
                                  ? tr("no advertised path")
                                  : QString::fromStdString(info.path)));
            continue;
        }

        if (previous->second.state != info.state)
        {
            log_line(tr("%1: state %2 → %3")
                         .arg(QString::fromStdString(info.name),
                              state_text(previous->second.state),
                              state_text(info.state)));
        }
        if (info.stats.errors > previous->second.stats.errors)
        {
            log_line(tr("%1: %2 new error(s)")
                         .arg(QString::fromStdString(info.name),
                              format_count(info.stats.errors
                                           - previous->second.stats.errors)));
        }
        if (info.stats.dropped > previous->second.stats.dropped)
        {
            log_line(tr("%1: dropped %2 byte(s)")
                         .arg(QString::fromStdString(info.name),
                              format_count(info.stats.dropped
                                           - previous->second.stats.dropped)));
        }
    }

    for (const auto& [id, info] : previous_endpoints_)
    {
        if (!has_endpoint(id, endpoints))
            log_line(tr("Port removed: %1").arg(
                QString::fromStdString(info.name)));
    }

    for (const auto& link : links)
    {
        const auto previous = previous_links_.find(link.id);
        if (previous == previous_links_.end())
        {
            log_line(tr("Link added: %1").arg(link_label(link)));
        }
        else if (link.stats.dropped > previous->second.stats.dropped)
        {
            log_line(tr("Link %1 dropped %2 byte(s)")
                         .arg(static_cast<qulonglong>(link.id))
                         .arg(format_count(link.stats.dropped
                                           - previous->second.stats.dropped)));
        }
    }

    for (const auto& [id, link] : previous_links_)
    {
        if (!has_link(id, links))
        {
            log_line(tr("Link removed: %1").arg(
                static_cast<qulonglong>(id)));
        }
    }
}

void MainWindow::update_status(
    const std::vector<vss::endpoint_info>& endpoints,
    const std::vector<vss::link_info>& links, double elapsed_seconds)
{
    port_count_label_->setText(
        tr("Ports: %1").arg(static_cast<int>(endpoints.size())));
    link_count_label_->setText(
        tr("Links: %1").arg(static_cast<int>(links.size())));

    std::uint64_t total_rx = 0;
    std::uint64_t total_tx = 0;
    for (const auto& info : endpoints)
    {
        total_rx += info.stats.bytes_rx;
        total_tx += info.stats.bytes_tx;
    }

    double rx_rate = 0.0;
    double tx_rate = 0.0;
    if (elapsed_seconds > 0.0)
    {
        if (total_rx >= previous_rx_)
            rx_rate = static_cast<double>(total_rx - previous_rx_)
                / elapsed_seconds;
        if (total_tx >= previous_tx_)
            tx_rate = static_cast<double>(total_tx - previous_tx_)
                / elapsed_seconds;
    }
    previous_rx_ = total_rx;
    previous_tx_ = total_tx;

    throughput_label_->setText(
        tr("RX %1/s · TX %2/s")
            .arg(format_rate(rx_rate), format_rate(tx_rate)));

    const bool running = studio_.engine().running();
    hub_label_->setText(running ? tr("Hub: running") : tr("Hub: stopped"));
    hub_label_->setStyleSheet(
        running ? QStringLiteral("color: #1a7f37;")
                : QStringLiteral("color: #cf222e;"));
}

void MainWindow::set_active_endpoint(vss::endpoint_id id, bool sync_table)
{
    selected_endpoint_ = id;
    selected_link_ = 0;
    inspector_->set_endpoint(id);
    send_panel_->set_selected_endpoint(id);

    if (sync_table && ports_view_ && ports_view_->selectionModel())
    {
        QItemSelectionModel* selection = ports_view_->selectionModel();
        const QModelIndex index =
            id == 0 ? QModelIndex() : port_model_->index_for_id(id);
        const QSignalBlocker blocker(selection);
        if (index.isValid())
        {
            ports_view_->setCurrentIndex(index);
            selection->select(index, QItemSelectionModel::ClearAndSelect
                                         | QItemSelectionModel::Rows);
        }
        else
        {
            selection->clear();
        }
    }
    update_action_states();
}

void MainWindow::update_action_states()
{
    const bool has_rows = ports_view_ && ports_view_->selectionModel()
        && !ports_view_->selectionModel()->selectedRows().isEmpty();
    if (remove_port_action_)
        remove_port_action_->setEnabled(has_rows);
    if (remove_link_action_)
        remove_link_action_->setEnabled(selected_link_ != 0);
}

void MainWindow::on_add_virtual_port()
{
    AddVirtualPortDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const int copies = dialog.count();
    const QString base = dialog.port_name().isEmpty()
        ? QStringLiteral("virtual-port")
        : dialog.port_name();
    int created_count = 0;
    vss::endpoint_id last = 0;

    for (int index = 0; index < copies; ++index)
    {
        const QString name = copies == 1
            ? dialog.port_name()
            : QStringLiteral("%1-%2").arg(base).arg(index + 1);
        const auto created = studio_.create_virtual_port(
            name.toStdString(), dialog.kind());
        if (!created)
        {
            show_error(tr("Add virtual port"), error_text(created.error()));
            break;
        }
        ++created_count;
        last = (*created)->id();
    }

    if (created_count == 0)
        return;

    set_dirty();
    refresh_now();
    set_active_endpoint(last, true);
    log_line(tr("Created %1 virtual port(s).").arg(created_count));
}

void MainWindow::on_attach_serial()
{
    AttachSerialDialog dialog(studio_, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    vss::endpoint_options options = studio_.default_options();
    options.capture_traffic = dialog.capture();

    const auto attached = studio_.attach_serial(
        dialog.port_name().toStdString(), dialog.uri().toStdString(),
        dialog.settings(), options);
    if (!attached)
    {
        show_error(tr("Attach serial port"), error_text(attached.error()));
        return;
    }

    const std::string name = (*attached)->name();
    set_dirty();
    refresh_now();
    set_active_endpoint((*attached)->id(), true);

    log_line(tr("Attached \"%1\" to %2 (%3).")
                 .arg(QString::fromStdString(name), dialog.uri(),
                      QString::fromStdString(
                          vss::to_label(dialog.settings()))));
    statusBar()->showMessage(
        tr("Attached \"%1\" to %2.").arg(QString::fromStdString(name),
                                         dialog.uri()),
        5000);
}

void MainWindow::on_add_mock_device()
{
    AddMockDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const auto created = studio_.create_mock_port(
        dialog.port_name().toStdString(), dialog.settings(),
        dialog.target().toStdString());
    if (!created)
    {
        show_error(tr("Add mock device"), error_text(created.error()));
        return;
    }

    const std::string name = (*created)->name();
    set_dirty();
    refresh_now();
    set_active_endpoint((*created)->id(), true);

    log_line(tr("Created mock device \"%1\" (fixture %2).")
                 .arg(QString::fromStdString(name), dialog.target()));
    statusBar()->showMessage(
        tr("Mock device \"%1\" created.").arg(QString::fromStdString(name)),
        5000);
}

void MainWindow::on_remove_selected_ports()
{
    std::vector<vss::endpoint_id> ids;
    std::vector<QString> names;

    if (ports_view_ && ports_view_->selectionModel())
    {
        const QModelIndexList rows =
            ports_view_->selectionModel()->selectedRows();
        for (const QModelIndex& index : rows)
        {
            if (const auto* info = port_model_->endpoint_at(index.row()))
            {
                ids.push_back(info->id);
                names.push_back(QString::fromStdString(info->name));
            }
        }
    }
    if (ids.empty() && selected_endpoint_ != 0)
    {
        if (const auto* info = port_model_->find_endpoint(selected_endpoint_))
        {
            ids.push_back(info->id);
            names.push_back(QString::fromStdString(info->name));
        }
    }
    if (ids.empty())
        return;

    QStringList failures;
    int removed = 0;
    for (std::size_t index = 0; index < ids.size(); ++index)
    {
        const auto result = studio_.remove_port(ids[index]);
        if (!result)
        {
            if (result.error() == vss::make_error_code(vss::errc::not_found))
            {
                log_line(tr("Port \"%1\" was already gone.").arg(names[index]));
                continue;
            }
            failures << tr("%1: %2").arg(names[index],
                                         error_text(result.error()));
            continue;
        }
        ++removed;
        log_line(tr("Removed port \"%1\".").arg(names[index]));
    }

    selected_endpoint_ = 0;
    selected_link_ = 0;
    set_active_endpoint(0, true);
    topology_->clear_selection();
    set_dirty();
    refresh_now();

    if (removed > 0)
    {
        statusBar()->showMessage(
            tr("Removed %1 port(s).").arg(removed), 4000);
    }
    if (!failures.isEmpty())
        show_error(tr("Remove port"), failures.join(QLatin1Char('\n')));
}

void MainWindow::on_ports_context_menu(const QPoint& position)
{
    QMenu menu(this);
    menu.addAction(add_virtual_action_);
    menu.addAction(attach_serial_action_);
    menu.addAction(add_mock_action_);
    menu.addSeparator();
    menu.addAction(remove_port_action_);
    menu.exec(ports_view_->mapToGlobal(position));
}

void MainWindow::on_table_selection_changed()
{
    if (!ports_view_->selectionModel())
        return;

    const QModelIndexList rows =
        ports_view_->selectionModel()->selectedRows();
    if (rows.isEmpty())
    {
        set_active_endpoint(0, false);
        topology_->clear_selection();
        return;
    }

    QModelIndex current = ports_view_->currentIndex();
    if (!current.isValid()
        || !ports_view_->selectionModel()->isSelected(current))
    {
        current = rows.front();
    }

    const auto* info = port_model_->endpoint_at(current.row());
    const vss::endpoint_id id = info ? info->id : 0;
    set_active_endpoint(id, false);
    if (id != 0)
        topology_->select_endpoint(id);
    else
        topology_->clear_selection();
}

void MainWindow::on_topology_selection_changed(quint64 endpoint, quint64 link)
{
    const auto endpoint_id = static_cast<vss::endpoint_id>(endpoint);
    // Selecting a link clears the port selection in the table as well; the
    // two views always describe the same current selection.
    set_active_endpoint(endpoint_id, true);
    selected_link_ = static_cast<vss::link_id>(link);
    update_action_states();

    if (endpoint == 0 && link != 0)
    {
        statusBar()->showMessage(
            tr("Link #%1 selected. Use Topology → Remove selected link to "
               "delete it.")
                .arg(link),
            6000);
    }
}

void MainWindow::on_remove_selected_link()
{
    if (selected_link_ == 0)
        return;

    const auto removed = studio_.disconnect(selected_link_);
    if (!removed)
    {
        if (removed.error() != vss::make_error_code(vss::errc::not_found))
        {
            show_error(tr("Remove link"), error_text(removed.error()));
            return;
        }
        log_line(tr("Link #%1 was already gone.").arg(
            static_cast<qulonglong>(selected_link_)));
        selected_link_ = 0;
        topology_->clear_selection();
        refresh_now();
        return;
    }

    log_line(tr("Removed link #%1.").arg(
        static_cast<qulonglong>(selected_link_)));
    selected_link_ = 0;
    topology_->clear_selection();
    set_dirty();
    refresh_now();
    statusBar()->showMessage(tr("Link removed."), 4000);
}

void MainWindow::on_null_modem_demo()
{
    const auto first = studio_.create_virtual_port(
        "null-modem-a", vss::virtual_port_kind::loopback);
    if (!first)
    {
        show_error(tr("Null-modem loopback"),
                   error_text(first.error()));
        return;
    }

    const auto second = studio_.create_virtual_port(
        "null-modem-b", vss::virtual_port_kind::loopback);
    if (!second)
    {
        const auto failure = second.error();
        static_cast<void>(studio_.remove_port((*first)->id()));
        show_error(tr("Null-modem loopback"), error_text(failure));
        return;
    }

    // A loopback transport reflects every write back into the endpoint's own
    // receive path.  A duplex hub link between two such endpoints would route
    // that reflection back and forth for ever, so the demo links them one way:
    // data sent to A is echoed on A and delivered to B, and stops there.
    const auto link = studio_.redirect((*first)->name(), (*second)->name());
    if (!link)
    {
        const auto failure = link.error();
        static_cast<void>(studio_.remove_port((*first)->id()));
        static_cast<void>(studio_.remove_port((*second)->id()));
        show_error(tr("Null-modem loopback"), error_text(failure));
        return;
    }

    set_dirty();
    refresh_now();
    set_active_endpoint((*first)->id(), true);

    const QString first_name = QString::fromStdString((*first)->name());
    const QString second_name = QString::fromStdString((*second)->name());
    log_line(tr("Null-modem demo created: %1 → %2. Loopback ports echo "
                "writes, so the demo uses a one-way tap; a duplex hub link "
                "would feed back on itself.")
                 .arg(first_name, second_name));
    statusBar()->showMessage(
        tr("Null-modem demo ready: data sent to %1 also arrives on %2.")
            .arg(first_name, second_name),
        8000);
}

void MainWindow::on_share_serial()
{
    ShareSerialDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const auto shared = studio_.share_serial(dialog.uri().toStdString(),
                                             dialog.listeners());
    if (!shared)
    {
        show_error(tr("Share serial port"), error_text(shared.error()));
        return;
    }

    set_dirty();
    refresh_now();
    set_active_endpoint(shared->source, true);

    log_line(tr("Shared %1 with %2 listener(s).")
                 .arg(dialog.uri())
                 .arg(shared->listeners.size()));
    statusBar()->showMessage(
        tr("Sharing %1 across %2 listener(s).")
            .arg(dialog.uri())
            .arg(shared->listeners.size()),
        6000);
}

void MainWindow::on_join_publishers()
{
    JoinPublishersDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    std::vector<std::string> sources;
    const QStringList uris = dialog.source_uris();
    for (const QString& uri : uris)
        sources.push_back(uri.toStdString());

    const auto joined = studio_.join_ports(sources,
                                           dialog.target_uri().toStdString());
    if (!joined)
    {
        show_error(tr("Join publishers"), error_text(joined.error()));
        return;
    }

    set_dirty();
    refresh_now();
    log_line(tr("Joined %1 publisher(s) into %2 (link #%3).")
                 .arg(sources.size())
                 .arg(dialog.target_uri())
                 .arg(static_cast<qulonglong>(*joined)));
    statusBar()->showMessage(
        tr("Joined %1 publisher(s) into %2.")
            .arg(sources.size())
            .arg(dialog.target_uri()),
        6000);
}

void MainWindow::on_bridge_network()
{
    BridgeDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const auto link = studio_.bridge(dialog.from_uri().toStdString(),
                                     dialog.to_uri().toStdString());
    if (!link)
    {
        show_error(tr("Bridge to network"), error_text(link.error()));
        return;
    }

    set_dirty();
    refresh_now();
    log_line(tr("Bridged %1 to %2 (link #%3).")
                 .arg(dialog.from_uri(), dialog.to_uri())
                 .arg(static_cast<qulonglong>(*link)));
    statusBar()->showMessage(
        tr("Bridged %1 to %2.").arg(dialog.from_uri(), dialog.to_uri()),
        6000);
}

void MainWindow::on_clear_monitor()
{
    monitor_model_->clear();
    monitor_model_->skip_to_end(studio_.engine().traffic());
    if (monitor_count_label_)
        monitor_count_label_->setText(tr("0 rows"));
    log_line(tr("Monitor cleared."));
}

void MainWindow::on_monitor_pause_toggled(bool paused)
{
    monitor_paused_ = paused;
    pause_button_->setText(paused ? tr("Resume") : tr("Pause"));
    log_line(paused ? tr("Monitor paused.") : tr("Monitor resumed."));
    if (!paused)
        feed_monitor();
}

void MainWindow::on_monitor_hex_toggled(bool hex)
{
    monitor_model_->set_hex_view(hex);
    statusBar()->showMessage(
        hex ? tr("Monitor showing hexadecimal payloads.")
            : tr("Monitor showing printable ASCII payloads."),
        3000);
}

void MainWindow::on_fit_topology()
{
    topology_->fit_content();
    statusBar()->showMessage(tr("Topology fitted to the view."), 3000);
}

void MainWindow::on_about()
{
    AboutDialog dialog(this);
    dialog.exec();
}

void MainWindow::on_connect_ports()
{
    ConnectDialog dialog(studio_.endpoint_infos(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const connect_request request = dialog.request();
    const std::string target = request.target.toStdString();
    bool ok = false;
    vss::error_code failure;

    switch (request.mode)
    {
    case connect_mode::duplex:
    {
        const auto result =
            studio_.connect(request.sources.value(0).toStdString(), target);
        ok = static_cast<bool>(result);
        if (!ok)
            failure = result.error();
        break;
    }
    case connect_mode::redirect:
    {
        const auto result =
            studio_.redirect(request.sources.value(0).toStdString(), target);
        ok = static_cast<bool>(result);
        if (!ok)
            failure = result.error();
        break;
    }
    case connect_mode::monitor:
    {
        const auto result =
            studio_.monitor(request.sources.value(0).toStdString(), target);
        ok = static_cast<bool>(result);
        if (!ok)
            failure = result.error();
        break;
    }
    case connect_mode::split:
    {
        std::vector<std::string> targets;
        for (const QString& name : request.sources)
            targets.push_back(name.toStdString());
        const auto result = studio_.split(target, targets);
        ok = static_cast<bool>(result);
        if (!ok)
            failure = result.error();
        break;
    }
    case connect_mode::join:
    {
        std::vector<std::string> sources;
        for (const QString& name : request.sources)
            sources.push_back(name.toStdString());
        const auto result = studio_.join(sources, target);
        ok = static_cast<bool>(result);
        if (!ok)
            failure = result.error();
        break;
    }
    }

    if (!ok)
    {
        show_error(tr("Connect ports"), error_text(failure));
        return;
    }

    log_line(tr("Connected %1 port(s) to %2.")
                 .arg(request.sources.size())
                 .arg(request.target));
    refresh_now();
    set_dirty();
}

void MainWindow::on_settings()
{
    SettingsDialog dialog(settings_, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    settings_ = dialog.result();
    apply_ui_settings(settings_);
}

void MainWindow::on_toggle_advanced(bool enabled)
{
    settings_.advanced = enabled;
    save_ui_settings(settings_);
    apply_mode(enabled);
}

void MainWindow::apply_mode(bool advanced)
{
    advanced_ = advanced;
    const QAction* const advanced_actions[] = {
        add_mock_action_, null_modem_action_, share_action_,
        join_action_, bridge_action_, fit_action_};
    for (const QAction* action : advanced_actions)
    {
        if (action != nullptr)
            const_cast<QAction*>(action)->setVisible(advanced);
    }
}

void MainWindow::apply_ui_settings(const ui_settings& value)
{
    if (poll_timer_ != nullptr)
        poll_timer_->setInterval(value.refresh_ms);

    apply_theme(value.theme);

    if (advanced_action_ != nullptr)
    {
        const QSignalBlocker blocker(advanced_action_);
        advanced_action_->setChecked(value.advanced);
    }
    apply_mode(value.advanced);
}

void MainWindow::on_new_document()
{
    if (!confirm_discard())
        return;

    clear_topology();
    document_path_.clear();
    document_name_ = QStringLiteral("untitled");
    dirty_ = false;
    monitor_model_->clear();
    studio_.engine().clear_traffic();
    set_active_endpoint(0, true);
    topology_->clear_selection();
    refresh_now();
    update_window_title();
    log_line(tr("New topology."));
    statusBar()->showMessage(tr("New topology."), 3000);
}

void MainWindow::on_open_document()
{
    if (!confirm_discard())
        return;

    const QString directory =
        document_path_.isEmpty() ? QDir::homePath()
                                 : QFileInfo(document_path_).absolutePath();
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open topology"), directory,
        tr("Virtual Serial Studio configuration (*.json);;All files (*)"));
    if (path.isEmpty())
        return;

    const auto loaded = vss::config::load_file(local_path(path));
    if (!loaded)
    {
        show_error(tr("Open topology"),
                   tr("Could not load %1:\n%2").arg(path,
                                                   error_text(loaded.error())));
        return;
    }

    std::vector<vss::endpoint_id> previous_ids;
    for (const auto& info : studio_.endpoint_infos())
        previous_ids.push_back(info.id);

    const auto applied = studio_.apply(*loaded);
    if (!applied)
    {
        show_error(tr("Open topology"),
                   tr("Could not apply %1:\n%2").arg(
                       path, error_text(applied.error())));
        return;
    }

    for (const auto id : previous_ids)
    {
        const auto removed = studio_.remove_port(id);
        if (!removed)
        {
            log_line(tr("Could not remove the previous port #%1: %2")
                         .arg(static_cast<qulonglong>(id))
                         .arg(error_text(removed.error())));
        }
    }

    document_path_ = path;
    document_name_ = QString::fromStdString(loaded->name);
    if (document_name_.isEmpty())
        document_name_ = QFileInfo(path).completeBaseName();
    dirty_ = false;
    selected_endpoint_ = 0;
    selected_link_ = 0;
    monitor_model_->clear();
    studio_.engine().clear_traffic();
    topology_->clear_selection();
    refresh_now();
    update_window_title();
    log_line(tr("Opened %1 (%2 port(s), %3 link(s)).")
                 .arg(path)
                 .arg(static_cast<int>(loaded->ports.size()))
                 .arg(static_cast<int>(loaded->links.size())));
    statusBar()->showMessage(tr("Opened %1.").arg(path), 5000);
}

void MainWindow::on_save_document()
{
    if (document_path_.isEmpty())
    {
        on_save_document_as();
        return;
    }
    static_cast<void>(save_to(document_path_));
}

void MainWindow::on_save_document_as()
{
    const QString directory =
        document_path_.isEmpty() ? QDir::homePath()
                                 : QFileInfo(document_path_).absolutePath();
    const QString suggestion =
        QDir(directory).filePath(document_name_ + QStringLiteral(".json"));
    QString path = QFileDialog::getSaveFileName(
        this, tr("Save topology as"), suggestion,
        tr("Virtual Serial Studio configuration (*.json);;All files (*)"));
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().isEmpty())
        path += QStringLiteral(".json");
    static_cast<void>(save_to(path));
}

bool MainWindow::save_to(const QString& path)
{
    QString saved_name = QFileInfo(path).completeBaseName();
    if (saved_name.isEmpty())
        saved_name = document_name_;

    vss::config::document document = studio_.snapshot();
    document.name = saved_name.toStdString();
    if (document.name.empty())
        document.name = "studio";

    const auto saved = vss::config::save_file(local_path(path), document);
    if (!saved)
    {
        show_error(tr("Save topology"),
                   tr("Could not write %1:\n%2").arg(
                       path, error_text(saved.error())));
        return false;
    }

    document_path_ = path;
    document_name_ = saved_name;
    if (document_name_.isEmpty())
        document_name_ = QStringLiteral("untitled");
    set_dirty(false);
    update_window_title();

    log_line(tr("Saved topology to %1 (%2 port(s), %3 link(s)).")
                 .arg(path)
                 .arg(static_cast<int>(document.ports.size()))
                 .arg(static_cast<int>(document.links.size())));
    statusBar()->showMessage(tr("Saved %1.").arg(path), 4000);
    return true;
}

bool MainWindow::confirm_discard()
{
    if (!dirty_ || !settings_.confirm_exit)
        return true;

    QMessageBox box(
        QMessageBox::Question, tr("Unsaved changes"),
        tr("The current topology has unsaved changes. Do you want to save "
           "them first?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this);
    box.setDefaultButton(QMessageBox::Save);
    const int choice = box.exec();

    if (choice == QMessageBox::Cancel)
        return false;
    if (choice == QMessageBox::Discard)
        return true;

    if (!document_path_.isEmpty())
        return save_to(document_path_);

    const QString suggestion =
        QDir(QDir::homePath()).filePath(document_name_ + QStringLiteral(".json"));
    QString path = QFileDialog::getSaveFileName(
        this, tr("Save topology as"), suggestion,
        tr("Virtual Serial Studio configuration (*.json);;All files (*)"));
    if (path.isEmpty())
        return false;
    if (QFileInfo(path).suffix().isEmpty())
        path += QStringLiteral(".json");
    return save_to(path);
}

void MainWindow::clear_topology()
{
    const std::vector<vss::endpoint_info> endpoints = studio_.endpoint_infos();
    for (const auto& info : endpoints)
    {
        const auto result = studio_.remove_port(info.id);
        if (!result)
        {
            log_line(tr("Could not remove \"%1\": %2")
                         .arg(QString::fromStdString(info.name),
                              error_text(result.error())));
        }
    }
    selected_endpoint_ = 0;
    selected_link_ = 0;
    if (topology_)
        topology_->clear_selection();
}

void MainWindow::set_dirty(bool dirty)
{
    if (dirty_ == dirty)
        return;
    dirty_ = dirty;
    update_window_title();
}

void MainWindow::update_window_title()
{
    setWindowTitle(tr("%1[*] — Virtual Serial Studio").arg(document_name_));
    setWindowModified(dirty_);
}

void MainWindow::log_line(const QString& text)
{
    if (!log_view_)
        return;

    QScrollBar* scroll = log_view_->verticalScrollBar();
    const bool at_bottom = scroll->value() >= scroll->maximum() - 4;
    log_view_->appendPlainText(
        QStringLiteral("[%1] %2")
            .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz")),
                 text));
    if (at_bottom)
        scroll->setValue(scroll->maximum());
}

void MainWindow::show_error(const QString& title, const QString& detail)
{
    log_line(tr("%1: %2").arg(title, detail));
    QMessageBox::critical(this, title, detail);
}

bool MainWindow::has_endpoint(
    vss::endpoint_id id, const std::vector<vss::endpoint_info>& endpoints)
{
    return std::any_of(endpoints.begin(), endpoints.end(),
                       [id](const vss::endpoint_info& info) {
                           return info.id == id;
                       });
}

bool MainWindow::has_link(vss::link_id id,
                          const std::vector<vss::link_info>& links)
{
    return std::any_of(links.begin(), links.end(),
                       [id](const vss::link_info& link) {
                           return link.id == id;
                       });
}

QString MainWindow::error_text(const vss::error_code& error)
{
    if (!error)
        return QStringLiteral("no error");
    const QString message = QString::fromStdString(error.message());
    return QStringLiteral("%1 (code %2)")
        .arg(message.isEmpty() ? QStringLiteral("error") : message)
        .arg(error.value());
}

QString MainWindow::describe_endpoints(
    const std::vector<vss::endpoint_info>& endpoints)
{
    QStringList lines;
    for (const auto& info : endpoints)
    {
        lines << QStringLiteral(
                     "%1 id=%2 kind=%3 state=%4 open=%5 rx=%6 tx=%7 errors=%8")
                     .arg(QString::fromStdString(info.name))
                     .arg(static_cast<qulonglong>(info.id))
                     .arg(kind_text(info.kind), state_text(info.state))
                     .arg(info.open ? QStringLiteral("yes")
                                    : QStringLiteral("no"))
                     .arg(format_count(info.stats.bytes_rx))
                     .arg(format_count(info.stats.bytes_tx))
                     .arg(format_count(info.stats.errors));
    }
    return lines.join(QLatin1Char('\n'));
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (!confirm_discard())
    {
        event->ignore();
        return;
    }

    if (poll_timer_)
        poll_timer_->stop();
    save_settings();
    event->accept();
    QMainWindow::closeEvent(event);
}

} // namespace vss::gui
