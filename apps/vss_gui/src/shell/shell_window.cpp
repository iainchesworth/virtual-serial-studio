//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "shell/shell_window.hpp"

#include "shell/command_palette.hpp"
#include "shell/format.hpp"
#include "shell/launcher.hpp"
#include "shell/status_bar.hpp"
#include "theme/icon_provider.hpp"
#include "theme/theme.hpp"
#include "topology/links_list.hpp"
#include "topology/topology_text.hpp"
#include "topology/topology_view.hpp"
#include "widgets/chips.hpp"
#include "widgets/controls.hpp"
#include "widgets/dialogs.hpp"
#include "widgets/notes.hpp"

#include <vss/config/document.hpp>
#include <vss/studio/studio.hpp>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QHash>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QResizeEvent>
#include <QSettings>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <cstdio>
#include <functional>

namespace vss::gui
{
namespace
{

constexpr const char* kRecentFiles = "ui/recentFiles";
constexpr const char* kRecentSummary = "ui/recentSummary";

QString document_name(const QString& path)
{
    return path.isEmpty() ? QStringLiteral("untitled")
                          : QFileInfo(path).fileName();
}

} // namespace

ShellWindow::ShellWindow(StudioBackend& backend, QWidget* parent)
    : QMainWindow(parent), backend_(backend)
{
    Theme::instance().initialise();
    setObjectName(QStringLiteral("ShellWindow"));
    setMinimumSize(Theme::instance().size(QStringLiteral("layout.windowMin")));
    resize(Theme::instance().size(QStringLiteral("layout.windowDefault")));
    setDockOptions(QMainWindow::AllowTabbedDocks | QMainWindow::AnimatedDocks);

    build_actions();
    build_menus();
    build_toolbar();
    build_central();
    build_docks();
    build_status();
    connect_backend();

    apply_theme_setting();
    apply_density_setting();
    apply_menu_palette();

    registry_.set_level(level_.level());
    {
        const QSignalBlocker blocker(advanced_action_);
        advanced_action_->setChecked(level_.advanced());
    }
    status_->set_advanced(level_.advanced());

    restore_layout();
    apply_compact();
    update_content();
    update_actions();
    update_title();

    connect(&level_, &LevelService::changed, this,
            &ShellWindow::on_level_changed);
    connect(&selection_, &SelectionService::port_changed, this,
            [this](vss::endpoint_id)
            {
                sync_selection();
                update_actions();
            });
    connect(&selection_, &SelectionService::link_changed, this,
            [this](vss::link_id)
            {
                sync_selection();
                update_actions();
            });
    connect(&Theme::instance(), &Theme::themeChanged, this,
            [this]
            {
                launcher_->refresh();
                apply_menu_palette();
                update();
            });
}

ShellWindow::~ShellWindow()
{
    // Child docks emit visibilityChanged while QObject tears the window down.
    // Their lambdas use members that C++ destroys before the QObject base, so
    // sever every child-to-this connection first.
    const auto children = findChildren<QObject*>();
    for (QObject* child : children)
        QObject::disconnect(child, nullptr, this, nullptr);
    QObject::disconnect(&Theme::instance(), nullptr, this, nullptr);
    monitor_action_ = nullptr;
    advanced_action_ = nullptr;
}

void ShellWindow::build_actions()
{
    const auto add =
        [this](const QString& id, const QString& text, const QString& keywords,
               const QString& icon, const QKeySequence& shortcut,
               action_level level, std::function<void()> handler,
               bool checkable = false, std::function<bool()> enabled = {})
    {
        registry_.add({id, text, keywords, icon, shortcut, level, checkable,
                       std::move(handler), std::move(enabled)});
    };
    const auto later = [this](const QString& what)
    { return [this, what] { placeholder(what); }; };

    // File
    add(QStringLiteral("file.new"), QStringLiteral("New"),
        QStringLiteral("new document setup"), QStringLiteral("new-file"),
        QKeySequence::New, action_level::simple, [this] { new_document(); });
    add(QStringLiteral("file.open"), QStringLiteral("Open"),
        QStringLiteral("open file setup"), QStringLiteral("open"),
        QKeySequence::Open, action_level::simple, [this] { open_document(); });
    add(QStringLiteral("file.recent"), QStringLiteral("Open recent"),
        QStringLiteral("recent file"), {}, {}, action_level::simple,
        [this]
        {
            const QStringList recent =
                QSettings().value(QLatin1String(kRecentFiles)).toStringList();
            if (!recent.isEmpty())
                open_path(recent.first());
        });
    add(QStringLiteral("file.templates"),
        QStringLiteral("Setups and templates"),
        QStringLiteral("template setup"), {}, {}, action_level::simple,
        later(QStringLiteral("Setups and templates")));
    add(QStringLiteral("file.save"), QStringLiteral("Save"),
        QStringLiteral("save file setup"), QStringLiteral("save"),
        QKeySequence::Save, action_level::simple, [this] { save_document(); });
    add(QStringLiteral("file.saveAs"), QStringLiteral("Save as"),
        QStringLiteral("save as file"), {}, QKeySequence::SaveAs,
        action_level::simple, [this] { save_document_as(); });
    add(QStringLiteral("file.exportCommand"),
        QStringLiteral("Export as command"), QStringLiteral("cli command"), {},
        {}, action_level::advanced, later(QStringLiteral("Export as command")));
    add(QStringLiteral("file.bundle"), QStringLiteral("Diagnostic bundle"),
        QStringLiteral("support zip"), {}, {}, action_level::simple,
        later(QStringLiteral("The diagnostic bundle")));
    add(QStringLiteral("file.quit"), QStringLiteral("Quit"),
        QStringLiteral("exit"), {}, QKeySequence::Quit, action_level::simple,
        [this] { close(); });

    // Edit
    add(QStringLiteral("edit.undo"), QStringLiteral("Undo"),
        QStringLiteral("redo"), {}, QKeySequence::Undo, action_level::simple,
        later(QStringLiteral("Undo")));
    add(QStringLiteral("edit.redo"), QStringLiteral("Redo"),
        QStringLiteral("undo"), {}, QKeySequence::Redo, action_level::simple,
        later(QStringLiteral("Redo")));
    add(
        QStringLiteral("edit.remove"), QStringLiteral("Remove selected"),
        QStringLiteral("delete remove"), QStringLiteral("remove"),
        QKeySequence(Qt::Key_Delete), action_level::simple,
        [this] { remove_selection(); }, false,
        [this] { return selection_.port() != 0 || selection_.link() != 0; });

    // Ports
    add(QStringLiteral("ports.add"), QStringLiteral("Add port"),
        QStringLiteral("new virtual create"), QStringLiteral("add"),
        QKeySequence(QStringLiteral("Ctrl+Shift+N")), action_level::simple,
        [this] { placeholder(QStringLiteral("Add a port")); });
    add(QStringLiteral("ports.virtual"), QStringLiteral("Virtual port"),
        QStringLiteral("new virtual create"), {}, {}, action_level::simple,
        [this] { placeholder(QStringLiteral("Add a port")); });
    add(QStringLiteral("ports.pair"), QStringLiteral("Linked pair"),
        QStringLiteral("pair two null modem"), QStringLiteral("connect"), {},
        action_level::simple,
        [this] { placeholder(QStringLiteral("Linked pair")); });
    add(QStringLiteral("ports.attach"), QStringLiteral("Attach real device"),
        QStringLiteral("serial device"), QStringLiteral("device"), {},
        action_level::simple, [this] { run_job(QStringLiteral("attach")); });
    add(QStringLiteral("ports.network"), QStringLiteral("Network endpoint"),
        QStringLiteral("tcp udp rfc2217"), {}, {}, action_level::advanced,
        [this] { run_job(QStringLiteral("network")); });
    add(QStringLiteral("ports.mock"), QStringLiteral("Mock fixture"),
        QStringLiteral("fixture mock script"), QStringLiteral("fixture"), {},
        action_level::advanced, [this] { run_job(QStringLiteral("mock")); });
    add(QStringLiteral("ports.copyName"), QStringLiteral("Copy port name"),
        QStringLiteral("clipboard path"), {},
        QKeySequence(QStringLiteral("Ctrl+Shift+C")), action_level::simple,
        later(QStringLiteral("Copy port name")), false,
        [this] { return selection_.port() != 0; });
    add(QStringLiteral("ports.remove"), QStringLiteral("Remove port"),
        QStringLiteral("delete remove"), {}, {}, action_level::simple,
        later(QStringLiteral("Remove port")), false,
        [this] { return selection_.port() != 0; });

    // Topology
    add(
        QStringLiteral("topology.connect"), QStringLiteral("Connect ports"),
        QStringLiteral("link split join redirect monitor"),
        QStringLiteral("connect"), QKeySequence(QStringLiteral("Ctrl+L")),
        action_level::simple, [this] { run_job(QStringLiteral("connect")); },
        false, [this] { return backend_.ports().size() >= 2; });
    add(QStringLiteral("topology.share"), QStringLiteral("Share a device"),
        QStringLiteral("split listeners"), QStringLiteral("share"), {},
        action_level::simple,
        [this]
        {
            later_package(QStringLiteral("Share a device"),
                          QStringLiteral("WP6, connect and share"));
        });
    add(QStringLiteral("topology.watch"), QStringLiteral("Watch traffic"),
        QStringLiteral("monitor copy"), QStringLiteral("watch"), {},
        action_level::simple,
        [this]
        {
            later_package(QStringLiteral("Watch traffic"),
                          QStringLiteral("WP6, connect and share"));
        });
    add(
        QStringLiteral("topology.removeLink"), QStringLiteral("Remove link"),
        QStringLiteral("delete disconnect"), {}, {}, action_level::simple,
        [this] { remove_selected_link(); }, false,
        [this] { return selection_.link() != 0; });
    add(QStringLiteral("topology.wiring"), QStringLiteral("Link wiring"),
        QStringLiteral("line matrix faults"), {}, {}, action_level::advanced,
        [this]
        {
            later_package(QStringLiteral("Link wiring"),
                          QStringLiteral("WP7, Advanced tools"));
        });
    add(QStringLiteral("topology.owner"), QStringLiteral("Owner"),
        QStringLiteral("policy arbitration"), {}, {}, action_level::advanced,
        [this]
        {
            later_package(QStringLiteral("Owner control"),
                          QStringLiteral("WP7, Advanced tools"));
        });
    add(QStringLiteral("topology.fit"), QStringLiteral("Fit"),
        QStringLiteral("zoom"), QStringLiteral("fit"),
        QKeySequence(QStringLiteral("Ctrl+0")), action_level::advanced,
        [this]
        {
            if (topology_ != nullptr)
                topology_->fit_content();
        });

    // Tools
    add(QStringLiteral("tools.search"), QStringLiteral("Search actions"),
        QStringLiteral("command palette"), QStringLiteral("search"),
        QKeySequence(QStringLiteral("Ctrl+K")), action_level::simple,
        [this] { open_palette(); });
    add(QStringLiteral("tools.setupCheck"), QStringLiteral("Setup check"),
        QStringLiteral("driver com0com"), {}, {}, action_level::simple,
        later(QStringLiteral("Setup check")));
    add(QStringLiteral("tools.benchmark"), QStringLiteral("Benchmark"),
        QStringLiteral("throughput speed"), {}, {}, action_level::advanced,
        later(QStringLiteral("Benchmark")));
    add(QStringLiteral("tools.refresh"), QStringLiteral("Refresh now"),
        QStringLiteral("reload"), QStringLiteral("refresh"),
        QKeySequence(Qt::Key_F5), action_level::simple,
        [this]
        {
            backend_.refresh();
            status_->refresh();
            status_->showMessage(QStringLiteral("Refreshed."), 2000);
        });
    add(QStringLiteral("tools.settings"), QStringLiteral("Settings"),
        QStringLiteral("preferences"), QStringLiteral("settings"),
        QKeySequence(QStringLiteral("Ctrl+,")), action_level::simple,
        later(QStringLiteral("Settings")));

    // View
    add(
        QStringLiteral("view.advanced"), QStringLiteral("Advanced mode"),
        QStringLiteral("simple expert level"), {},
        QKeySequence(QStringLiteral("Ctrl+Shift+A")), action_level::simple,
        [] {}, true);
    add(
        QStringLiteral("view.monitor"), QStringLiteral("Monitor"),
        QStringLiteral("traffic"), {}, {}, action_level::simple, [] {}, true);
    add(
        QStringLiteral("view.links"), QStringLiteral("Show links as a list"),
        QStringLiteral("links topology list accessible"), {}, {},
        action_level::simple, [] {}, true);
    add(QStringLiteral("view.log"), QStringLiteral("Log"),
        QStringLiteral("events"), {}, {}, action_level::advanced,
        later(QStringLiteral("The event log")));
    add(QStringLiteral("view.json"), QStringLiteral("Topology JSON"),
        QStringLiteral("editor schema"), {}, {}, action_level::advanced,
        later(QStringLiteral("The topology JSON editor")));
    add(
        QStringLiteral("view.theme.system"),
        QStringLiteral("Follow the system"),
        QStringLiteral("theme colour scheme"), {}, {}, action_level::simple,
        [this]
        {
            Theme::instance().set_mode(theme_mode::system);
            QSettings().setValue(QStringLiteral("ui/theme"),
                                 QStringLiteral("system"));
        },
        true);
    add(
        QStringLiteral("view.theme.light"), QStringLiteral("Light"),
        QStringLiteral("theme"), {}, {}, action_level::simple,
        [this]
        {
            Theme::instance().set_mode(theme_mode::light);
            QSettings().setValue(QStringLiteral("ui/theme"),
                                 QStringLiteral("light"));
        },
        true);
    add(
        QStringLiteral("view.theme.dark"), QStringLiteral("Dark"),
        QStringLiteral("theme"), {}, {}, action_level::simple,
        [this]
        {
            Theme::instance().set_mode(theme_mode::dark);
            QSettings().setValue(QStringLiteral("ui/theme"),
                                 QStringLiteral("dark"));
        },
        true);
    add(
        QStringLiteral("view.density.comfortable"),
        QStringLiteral("Comfortable"), QStringLiteral("density spacing"), {},
        {}, action_level::simple,
        [this]
        {
            Theme::instance().set_density(density_mode::comfortable);
            QSettings().setValue(QStringLiteral("ui/density"),
                                 QStringLiteral("comfortable"));
        },
        true);
    add(
        QStringLiteral("view.density.compact"), QStringLiteral("Compact"),
        QStringLiteral("density spacing"), {}, {}, action_level::simple,
        [this]
        {
            Theme::instance().set_density(density_mode::compact);
            QSettings().setValue(QStringLiteral("ui/density"),
                                 QStringLiteral("compact"));
        },
        true);
    add(QStringLiteral("view.resetLayout"), QStringLiteral("Reset layout"),
        QStringLiteral("docks window"), {}, {}, action_level::simple,
        [this] { reset_layout(); });
    add(QStringLiteral("view.showLauncher"), QStringLiteral("Show launcher"),
        QStringLiteral("empty start"), {}, {}, action_level::simple,
        [this] { show_launcher(); });

    // Help
    add(QStringLiteral("help.contents"), QStringLiteral("Help contents"),
        QStringLiteral("manual"), {}, QKeySequence(Qt::Key_F1),
        action_level::simple, later(QStringLiteral("Help contents")));
    add(QStringLiteral("help.why"), QStringLiteral("Why is nothing arriving?"),
        QStringLiteral("troubleshoot"), {}, {}, action_level::simple,
        later(QStringLiteral("The troubleshooting checklist")));
    add(QStringLiteral("help.shortcuts"), QStringLiteral("Keyboard shortcuts"),
        QStringLiteral("keys"), {}, {}, action_level::simple,
        later(QStringLiteral("Keyboard shortcuts")));
    add(QStringLiteral("help.environment"), QStringLiteral("Copy environment"),
        QStringLiteral("build bug report"), {}, {}, action_level::simple,
        later(QStringLiteral("Copy environment")));
    add(QStringLiteral("help.about"), QStringLiteral("About"),
        QStringLiteral("version licence"), {}, {}, action_level::simple,
        later(QStringLiteral("About")));

    advanced_action_ = registry_.action(QStringLiteral("view.advanced"));
    monitor_action_ = registry_.action(QStringLiteral("view.monitor"));
    links_action_ = registry_.action(QStringLiteral("view.links"));
    connect(advanced_action_, &QAction::toggled, this,
            [this](bool on) { level_.set_advanced(on); });

    auto* theme_group = new QActionGroup(this);
    theme_group->setExclusive(true);
    theme_group->addAction(
        registry_.action(QStringLiteral("view.theme.system")));
    theme_group->addAction(
        registry_.action(QStringLiteral("view.theme.light")));
    theme_group->addAction(registry_.action(QStringLiteral("view.theme.dark")));

    auto* density_group = new QActionGroup(this);
    density_group->setExclusive(true);
    density_group->addAction(
        registry_.action(QStringLiteral("view.density.comfortable")));
    density_group->addAction(
        registry_.action(QStringLiteral("view.density.compact")));
}

void ShellWindow::build_menus()
{
    const auto add = [this](QMenu* menu, const QString& id)
    {
        if (QAction* action = registry_.action(id))
            menu->addAction(action);
    };
    const auto add_menu = [this](const QString& title)
    { return menuBar()->addMenu(title); };

    QMenu* file = add_menu(QStringLiteral("&File"));
    add(file, QStringLiteral("file.new"));
    add(file, QStringLiteral("file.open"));
    add(file, QStringLiteral("file.recent"));
    add(file, QStringLiteral("file.templates"));
    file->addSeparator();
    add(file, QStringLiteral("file.save"));
    add(file, QStringLiteral("file.saveAs"));
    file->addSeparator();
    add(file, QStringLiteral("file.exportCommand"));
    add(file, QStringLiteral("file.bundle"));
    file->addSeparator();
    add(file, QStringLiteral("file.quit"));

    QMenu* edit = add_menu(QStringLiteral("&Edit"));
    add(edit, QStringLiteral("edit.undo"));
    add(edit, QStringLiteral("edit.redo"));
    edit->addSeparator();
    add(edit, QStringLiteral("edit.remove"));

    QMenu* ports = add_menu(QStringLiteral("&Ports"));
    add(ports, QStringLiteral("ports.add"));
    add(ports, QStringLiteral("ports.virtual"));
    add(ports, QStringLiteral("ports.pair"));
    add(ports, QStringLiteral("ports.attach"));
    ports->addSeparator();
    add(ports, QStringLiteral("ports.network"));
    add(ports, QStringLiteral("ports.mock"));
    ports->addSeparator();
    add(ports, QStringLiteral("ports.copyName"));
    add(ports, QStringLiteral("ports.remove"));

    QMenu* topology = add_menu(QStringLiteral("&Topology"));
    add(topology, QStringLiteral("topology.connect"));
    add(topology, QStringLiteral("topology.share"));
    add(topology, QStringLiteral("topology.watch"));
    topology->addSeparator();
    add(topology, QStringLiteral("topology.removeLink"));
    add(topology, QStringLiteral("topology.wiring"));
    add(topology, QStringLiteral("topology.owner"));
    topology->addSeparator();
    add(topology, QStringLiteral("topology.fit"));

    QMenu* tools = add_menu(QStringLiteral("&Tools"));
    add(tools, QStringLiteral("tools.search"));
    add(tools, QStringLiteral("tools.setupCheck"));
    add(tools, QStringLiteral("tools.benchmark"));
    tools->addSeparator();
    add(tools, QStringLiteral("tools.refresh"));
    add(tools, QStringLiteral("tools.settings"));

    QMenu* view = add_menu(QStringLiteral("&View"));
    add(view, QStringLiteral("view.advanced"));
    view->addSeparator();
    add(view, QStringLiteral("view.monitor"));
    add(view, QStringLiteral("view.links"));
    add(view, QStringLiteral("view.log"));
    add(view, QStringLiteral("view.json"));
    view->addSeparator();
    QMenu* theme = view->addMenu(QStringLiteral("Theme"));
    add(theme, QStringLiteral("view.theme.system"));
    add(theme, QStringLiteral("view.theme.light"));
    add(theme, QStringLiteral("view.theme.dark"));
    QMenu* density = view->addMenu(QStringLiteral("Density"));
    add(density, QStringLiteral("view.density.comfortable"));
    add(density, QStringLiteral("view.density.compact"));
    view->addSeparator();
    add(view, QStringLiteral("view.resetLayout"));
    add(view, QStringLiteral("view.showLauncher"));

    QMenu* help = add_menu(QStringLiteral("&Help"));
    add(help, QStringLiteral("help.contents"));
    add(help, QStringLiteral("help.why"));
    add(help, QStringLiteral("help.shortcuts"));
    add(help, QStringLiteral("help.environment"));
    add(help, QStringLiteral("help.about"));
}

void ShellWindow::build_toolbar()
{
    toolbar_ = new QToolBar(QStringLiteral("Main toolbar"), this);
    toolbar_->setObjectName(QStringLiteral("mainToolbar"));
    toolbar_->setMovable(false);
    toolbar_->setFloatable(false);
    toolbar_->setIconSize(QSize(16, 16));
    toolbar_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    addToolBar(Qt::TopToolBarArea, toolbar_);

    const auto add = [this](const QString& id)
    {
        if (QAction* action = registry_.action(id))
            toolbar_->addAction(action);
    };
    add(QStringLiteral("file.new"));
    add(QStringLiteral("file.open"));
    add(QStringLiteral("file.save"));
    toolbar_->addSeparator();
    add(QStringLiteral("ports.add"));
    add(QStringLiteral("ports.attach"));
    add(QStringLiteral("topology.connect"));
    add(QStringLiteral("ports.remove"));
    toolbar_->addSeparator();
    add(QStringLiteral("ports.mock"));
    add(QStringLiteral("topology.fit"));

    auto* spacer = new QWidget(toolbar_);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar_->addWidget(spacer);

    search_ = new QLineEdit(toolbar_);
    search_->setPlaceholderText(QStringLiteral("Search actions"));
    search_->setAccessibleName(QStringLiteral("Search actions"));
    search_->setClearButtonEnabled(true);
    search_->setFixedWidth(240);
    search_->setFont(Theme::instance().font(text_style::body));
    toolbar_->addWidget(search_);
    connect(search_, &QLineEdit::returnPressed, this,
            [this] { open_palette(); });
}

void ShellWindow::build_central()
{
    central_ = new QStackedWidget(this);
    setCentralWidget(central_);

    launcher_ = new Launcher(backend_, central_);
    connect(launcher_, &Launcher::job_requested, this,
            [this](const QString& job) { run_job(job); });
    connect(launcher_, &Launcher::setup_check_requested, this,
            [this] { placeholder(QStringLiteral("Setup check")); });
    connect(launcher_, &Launcher::browse_requested, this,
            [this] { open_document(); });
    connect(launcher_, &Launcher::open_recent_requested, this,
            [this](const QString& path) { open_path(path); });
    connect(launcher_, &Launcher::advanced_requested, this,
            [this] { level_.set_advanced(true); });
    central_->addWidget(launcher_);

    workspace_ = new QWidget(central_);
    auto* layout = new QVBoxLayout(workspace_);
    layout->setContentsMargins(8, 8, 8, 8);

    auto* card = new QFrame(workspace_);
    card->setProperty("card", true);
    polish(card);
    auto* card_layout = new QVBoxLayout(card);
    card_layout->setContentsMargins(0, 0, 0, 0);

    topology_ = new topology::TopologyView(card);
    topology_->set_capabilities(backend_.caps());
    card_layout->addWidget(topology_);
    layout->addWidget(card);
    central_->addWidget(workspace_);

    connect(topology_, &topology::TopologyView::port_selected, this,
            [this](vss::endpoint_id id)
            {
                if (id != 0)
                {
                    selection_.select_link(0);
                    selection_.select_port(id);
                }
                else
                {
                    selection_.select_port(0);
                }
            });
    connect(topology_, &topology::TopologyView::link_selected, this,
            [this](vss::link_id id)
            {
                if (id != 0)
                {
                    selection_.select_port(0);
                    selection_.select_link(id);
                }
                else
                {
                    selection_.select_link(0);
                }
            });
    connect(topology_, &topology::TopologyView::inspect_requested, this,
            [this](vss::endpoint_id) { focus_inspector(); });
    connect(topology_, &topology::TopologyView::copy_name_requested, this,
            [this](vss::endpoint_id id) { copy_port_name(id); });
    connect(topology_, &topology::TopologyView::connect_requested, this,
            [this](vss::endpoint_id id) { connect_from_port(id); });
    connect(topology_, &topology::TopologyView::remove_port_requested, this,
            [this](vss::endpoint_id id) { remove_port(id); });
    connect(topology_, &topology::TopologyView::remove_link_requested, this,
            [this](vss::link_id id) { remove_link(id); });
    connect(topology_, &topology::TopologyView::rename_requested, this,
            [this](vss::endpoint_id)
            {
                status_->showMessage(
                    QStringLiteral("Not available in this build."), 5000);
            });
    connect(topology_, &topology::TopologyView::status_message, this,
            [this](const QString& text) { status_->showMessage(text, 4000); });
}

void ShellWindow::build_docks()
{
    const auto make_dock = [this](const QString& object_name,
                                  const QString& title, const QString& caption)
    {
        auto* dock = new QDockWidget(title, this);
        dock->setObjectName(object_name);
        dock->setAllowedAreas(Qt::AllDockWidgetAreas);
        auto* page = new QWidget(dock);
        auto* layout = new QVBoxLayout(page);
        layout->setContentsMargins(12, 12, 12, 12);
        auto* text = new QLabel(caption, page);
        text->setFont(Theme::instance().font(text_style::body));
        text->setWordWrap(true);
        text->setProperty("muted", true);
        layout->addWidget(text);
        layout->addStretch(1);
        dock->setWidget(page);
        return dock;
    };

    ports_dock_ =
        make_dock(QStringLiteral("portsDock"), QStringLiteral("Ports"),
                  QStringLiteral("The ports table arrives with WP3."));
    inspector_dock_ =
        make_dock(QStringLiteral("inspectorDock"), QStringLiteral("Inspector"),
                  QStringLiteral("The inspector arrives with WP3."));
    send_dock_ = make_dock(QStringLiteral("sendDock"), QStringLiteral("Send"),
                           QStringLiteral("The send panel arrives with WP3."));
    monitor_dock_ =
        make_dock(QStringLiteral("monitorDock"), QStringLiteral("Monitor"),
                  QStringLiteral("The monitor arrives with WP3."));

    ports_dock_->setMinimumWidth(
        Theme::instance().metric(QStringLiteral("layout.ports.min")));
    inspector_dock_->setMinimumWidth(
        Theme::instance().metric(QStringLiteral("layout.inspector.min")));
    send_dock_->setMinimumHeight(
        Theme::instance().metric(QStringLiteral("layout.send.default")));
    monitor_dock_->setMinimumHeight(
        Theme::instance().metric(QStringLiteral("layout.monitor.min")));

    addDockWidget(Qt::LeftDockWidgetArea, ports_dock_);
    addDockWidget(Qt::RightDockWidgetArea, inspector_dock_);
    addDockWidget(Qt::RightDockWidgetArea, send_dock_);
    splitDockWidget(inspector_dock_, send_dock_, Qt::Vertical);
    addDockWidget(Qt::BottomDockWidgetArea, monitor_dock_);

    if (QAction* action = monitor_action_)
    {
        action->setChecked(true);
        connect(action, &QAction::toggled, this,
                [this](bool shown)
                {
                    if (monitor_dock_ != nullptr)
                        monitor_dock_->setVisible(shown);
                });
    }
    connect(monitor_dock_, &QDockWidget::visibilityChanged, this,
            [this](bool visible)
            {
                if (monitor_action_ == nullptr)
                    return;
                const QSignalBlocker blocker(monitor_action_);
                monitor_action_->setChecked(visible);
            });

    links_dock_ = new QDockWidget(QStringLiteral("Links"), this);
    links_dock_->setObjectName(QStringLiteral("linksDock"));
    links_dock_->setAllowedAreas(Qt::AllDockWidgetAreas);
    links_ = new topology::LinksList(links_dock_);
    links_dock_->setWidget(links_);
    addDockWidget(Qt::BottomDockWidgetArea, links_dock_);
    tabifyDockWidget(monitor_dock_, links_dock_);
    links_dock_->hide();

    connect(links_, &topology::LinksList::link_selected, this,
            [this](vss::link_id id)
            {
                if (id != 0)
                {
                    selection_.select_port(0);
                    selection_.select_link(id);
                }
                else
                {
                    selection_.select_link(0);
                }
            });
    if (links_action_ != nullptr)
    {
        links_action_->setChecked(false);
        connect(links_action_, &QAction::toggled, this,
                [this](bool shown) { show_links_dock(shown); });
    }
    connect(links_dock_, &QDockWidget::visibilityChanged, this,
            [this](bool visible)
            {
                if (links_action_ == nullptr)
                    return;
                const QSignalBlocker blocker(links_action_);
                links_action_->setChecked(visible);
            });

    resizeDocks(
        {ports_dock_},
        {Theme::instance().metric(QStringLiteral("layout.ports.default"))},
        Qt::Horizontal);
    resizeDocks(
        {inspector_dock_},
        {Theme::instance().metric(QStringLiteral("layout.inspector.default"))},
        Qt::Horizontal);
    resizeDocks(
        {send_dock_},
        {Theme::instance().metric(QStringLiteral("layout.send.default"))},
        Qt::Vertical);
    resizeDocks(
        {monitor_dock_},
        {Theme::instance().metric(QStringLiteral("layout.monitor.default"))},
        Qt::Vertical);
}

void ShellWindow::build_status()
{
    status_ = new StatusBar(backend_, this);
    setStatusBar(status_);
    connect(status_, &StatusBar::level_requested, this,
            [this](bool advanced) { level_.set_advanced(advanced); });
    status_->set_advanced(level_.advanced());
    menuBar()->setFont(Theme::instance().font(text_style::body));
    menuBar()->setFixedHeight(
        Theme::instance().metric(QStringLiteral("menuBar")));
    toolbar_->setFixedHeight(
        Theme::instance().metric(QStringLiteral("toolbar")));
    status_->setFixedHeight(
        Theme::instance().metric(QStringLiteral("statusBar")));
}

void ShellWindow::connect_backend()
{
    connect(&backend_, &StudioBackend::ports_changed, this,
            &ShellWindow::on_backend_changed);
    connect(&backend_, &StudioBackend::links_changed, this,
            &ShellWindow::on_backend_changed);
    connect(&backend_, &StudioBackend::lines_changed, this,
            [this](vss::endpoint_id) { status_->refresh(); });
    connect(&backend_, &StudioBackend::batch_ready, this,
            [this]
            {
                status_->refresh();
                backend_.refresh();
            });
}

void ShellWindow::on_backend_changed()
{
    if (!backend_.ports().empty())
        launcher_forced_ = false;
    status_->refresh();
    validate_selection();
    update_content();
    if (!loading_)
        set_dirty(true);
    update_actions();
}

void ShellWindow::on_level_changed(bool advanced)
{
    registry_.set_level(level_.level());
    if (advanced_action_ != nullptr)
    {
        const QSignalBlocker blocker(advanced_action_);
        advanced_action_->setChecked(advanced);
    }
    status_->set_advanced(advanced);
    update_actions();
}

void ShellWindow::update_level_ui()
{
    status_->set_advanced(level_.advanced());
}

void ShellWindow::update_content()
{
    const bool has_ports = !backend_.ports().empty();
    const bool show_launcher_page = launcher_forced_ || !has_ports;
    if (show_launcher_page)
    {
        central_->setCurrentWidget(launcher_);
        launcher_->refresh();
        for (QDockWidget* dock : {ports_dock_, inspector_dock_, send_dock_,
                                  monitor_dock_, links_dock_})
        {
            if (dock != nullptr)
                dock->hide();
        }
    }
    else
    {
        central_->setCurrentWidget(workspace_);
        refresh_topology();
        for (QDockWidget* dock :
             {ports_dock_, inspector_dock_, send_dock_, monitor_dock_})
        {
            if (dock != nullptr)
                dock->show();
        }
        const bool links_shown =
            links_action_ != nullptr && links_action_->isChecked();
        if (links_dock_ != nullptr)
            links_dock_->setVisible(links_shown);
        if (monitor_action_ != nullptr)
        {
            const QSignalBlocker blocker(monitor_action_);
            monitor_action_->setChecked(monitor_dock_->isVisible());
        }
    }
    sync_selection();
    status_->refresh();
}

void ShellWindow::update_title()
{
    QString name = document_name(document_path_);
    if (dirty_)
        name += QLatin1Char('*');
    setWindowTitle(QStringLiteral("%1 - Virtual Serial Studio").arg(name));
}

void ShellWindow::update_actions()
{
    registry_.refresh();
    update_title();
}

void ShellWindow::update_status()
{
    status_->refresh();
}

void ShellWindow::apply_compact()
{
    const bool compact = width() < 1100 || height() < 700;
    if (compact == compact_)
        return;
    compact_ = compact;

    toolbar_->setToolButtonStyle(compact ? Qt::ToolButtonIconOnly
                                         : Qt::ToolButtonTextBesideIcon);
    if (compact)
    {
        if (!tabifiedDockWidgets(inspector_dock_).contains(send_dock_))
            tabifyDockWidget(inspector_dock_, send_dock_);
    }
    else if (tabifiedDockWidgets(inspector_dock_).contains(send_dock_))
    {
        splitDockWidget(inspector_dock_, send_dock_, Qt::Vertical);
    }
    apply_dock_sizes();
}

void ShellWindow::show_launcher()
{
    launcher_forced_ = true;
    update_content();
}

void ShellWindow::show_workspace()
{
    launcher_forced_ = false;
    update_content();
}

void ShellWindow::open_palette()
{
    CommandPalette palette(registry_, level_, this);
    if (!search_->text().trimmed().isEmpty())
        palette.set_query(search_->text());
    palette.exec();
    search_->clear();
    status_->refresh();
}

void ShellWindow::run_job(const QString& job)
{
    if (job == QLatin1String("add"))
        registry_.action(QStringLiteral("ports.add"))->trigger();
    else if (job == QLatin1String("pair"))
        registry_.action(QStringLiteral("ports.pair"))->trigger();
    else if (job == QLatin1String("share"))
        registry_.action(QStringLiteral("topology.share"))->trigger();
    else if (job == QLatin1String("watch"))
        registry_.action(QStringLiteral("topology.watch"))->trigger();
    else if (job == QLatin1String("attach"))
    {
        placeholder(QStringLiteral("Attach a real device"));
        return;
    }
    else if (job == QLatin1String("connect"))
    {
        later_package(QStringLiteral("Connect ports"),
                      QStringLiteral("WP6, connect and share"));
        return;
    }
    else if (job == QLatin1String("network"))
    {
        placeholder(QStringLiteral("Network endpoint"));
        return;
    }
    else if (job == QLatin1String("mock"))
    {
        placeholder(QStringLiteral("Mock fixture"));
        return;
    }
    else
    {
        placeholder(job);
        return;
    }
    // The actions above currently show their own placeholder; this path is
    // here so the launcher wiring stays one call.
}

void ShellWindow::placeholder(const QString& what)
{
    status_->showMessage(
        QStringLiteral("%1 is part of a later work package.").arg(what), 5000);
}

void ShellWindow::later_package(const QString& what, const QString& package)
{
    status_->showMessage(
        QStringLiteral("%1 arrives with %2.").arg(what, package), 6000);
}

void ShellWindow::refresh_topology()
{
    if (topology_ != nullptr)
        topology_->set_topology(backend_.ports(), backend_.links());
    if (links_ != nullptr)
        links_->set_links(backend_.links(), backend_.ports());
}

void ShellWindow::sync_selection()
{
    if (topology_ != nullptr)
        topology_->set_selection(selection_.port(), selection_.link());
    if (links_ != nullptr)
        links_->set_selected(selection_.link());
}

void ShellWindow::validate_selection()
{
    bool port_exists = false;
    for (const auto& port : backend_.ports())
        port_exists = port_exists || port.id == selection_.port();
    if (!port_exists && selection_.port() != 0)
        selection_.select_port(0);

    bool link_exists = false;
    for (const auto& link : backend_.links())
        link_exists = link_exists || link.id == selection_.link();
    if (!link_exists && selection_.link() != 0)
        selection_.select_link(0);
}

void ShellWindow::focus_inspector()
{
    if (inspector_dock_ == nullptr)
        return;
    inspector_dock_->show();
    inspector_dock_->raise();
    inspector_dock_->setFocus(Qt::ShortcutFocusReason);
}

void ShellWindow::copy_port_name(vss::endpoint_id id)
{
    for (const auto& port : backend_.ports())
    {
        if (port.id != id)
            continue;
        QApplication::clipboard()->setText(port.name);
        status_->showMessage(topology_text::copied(port.name), 3000);
        return;
    }
}

void ShellWindow::connect_from_port(vss::endpoint_id id)
{
    selection_.select_link(0);
    selection_.select_port(id);
    if (QAction* action = registry_.action(QStringLiteral("topology.connect")))
        action->trigger();
}

void ShellWindow::remove_selection()
{
    if (selection_.link() != 0)
    {
        remove_link(selection_.link());
        return;
    }
    if (selection_.port() != 0)
        remove_port(selection_.port());
}

void ShellWindow::remove_selected_link()
{
    remove_link(selection_.link());
}

void ShellWindow::remove_link(vss::link_id id)
{
    if (id == 0)
        return;
    const auto link = backend_.link(id);
    if (!link)
        return;

    QHash<vss::endpoint_id, QString> names;
    for (const auto& port : backend_.ports())
        names.insert(port.id, port.name);
    const QString description = topology_text::link_name(*link, names);

    if (!ConfirmDialog::ask(
            QStringLiteral("Remove link"),
            QStringLiteral("Remove this link? %1. Nothing is saved until you "
                           "save the setup.")
                .arg(description),
            QStringLiteral("Remove link"), QStringLiteral("Cancel"), true,
            this))
    {
        return;
    }

    const auto removed = backend_.disconnect(id);
    if (!removed)
    {
        ErrorDialog dialog(QStringLiteral("Could not remove the link"),
                           error_text(removed.error()), {}, this);
        dialog.exec();
        return;
    }

    selection_.select_link(0);
    backend_.refresh();
    status_->showMessage(QStringLiteral("Removed the link."), 3000);
}

void ShellWindow::remove_port(vss::endpoint_id id)
{
    if (id == 0)
        return;
    const auto port = backend_.port(id);
    if (!port)
        return;

    if (!ConfirmDialog::ask(
            QStringLiteral("Remove port"),
            QStringLiteral("Remove %1 and its links? Nothing is saved until "
                           "you save the setup.")
                .arg(port->name),
            QStringLiteral("Remove port"), QStringLiteral("Cancel"), true,
            this))
    {
        return;
    }

    const auto removed = backend_.remove_port(id);
    if (!removed)
    {
        ErrorDialog dialog(
            QStringLiteral("Could not remove %1").arg(port->name),
            error_text(removed.error()), {}, this);
        dialog.exec();
        return;
    }

    selection_.clear();
    backend_.refresh();
    status_->showMessage(QStringLiteral("Removed %1.").arg(port->name), 3000);
}

void ShellWindow::show_links_dock(bool shown)
{
    if (links_dock_ == nullptr)
        return;
    if (shown && backend_.ports().empty())
        return;
    if (shown)
    {
        links_dock_->show();
        links_dock_->raise();
    }
    else
    {
        links_dock_->hide();
    }
}

void ShellWindow::new_document()
{
    if (!confirm_discard())
        return;
    loading_ = true;
    const auto ports = backend_.ports();
    for (const auto& port : ports)
        static_cast<void>(backend_.remove_port(port.id));
    backend_.refresh();
    loading_ = false;
    document_path_.clear();
    set_dirty(false);
    selection_.clear();
    show_launcher();
    status_->refresh();
}

void ShellWindow::open_document()
{
    if (!confirm_discard())
        return;
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Open setup"), QString(),
        QStringLiteral("Virtual Serial Studio setups (*.json);;All files (*)"));
    if (path.isEmpty())
        return;
    open_path(path);
}

void ShellWindow::open_path(const QString& path)
{
    if (!confirm_discard())
        return;
    const auto document = vss::config::load_file(path.toStdString());
    if (!document)
    {
        ErrorDialog dialog(
            QStringLiteral("Could not open %1").arg(QFileInfo(path).fileName()),
            error_text(document.error()), {}, this);
        dialog.exec();
        return;
    }

    loading_ = true;
    const auto applied = backend_.apply(*document, true);
    loading_ = false;
    if (!applied)
    {
        ErrorDialog dialog(
            QStringLiteral("Could not open %1").arg(QFileInfo(path).fileName()),
            error_text(applied.error()), {}, this);
        dialog.exec();
        return;
    }
    document_path_ = path;
    set_dirty(false);
    const auto ports = backend_.ports();
    const auto links = backend_.links();
    add_recent(path, QStringLiteral("%1 ports, %2 links")
                         .arg(static_cast<qulonglong>(ports.size()))
                         .arg(static_cast<qulonglong>(links.size())));
    show_workspace();
    status_->refresh();
}

void ShellWindow::save_document()
{
    if (document_path_.isEmpty())
    {
        save_document_as();
        return;
    }
    const auto saved = vss::config::save_file(document_path_.toStdString(),
                                              backend_.snapshot());
    if (!saved)
    {
        ErrorDialog dialog(QStringLiteral("Could not save this setup"),
                           error_text(saved.error()), {}, this);
        dialog.exec();
        return;
    }
    set_dirty(false);
    const auto ports = backend_.ports();
    const auto links = backend_.links();
    add_recent(document_path_, QStringLiteral("%1 ports, %2 links")
                                   .arg(static_cast<qulonglong>(ports.size()))
                                   .arg(static_cast<qulonglong>(links.size())));
    status_->showMessage(
        QStringLiteral("Saved %1.").arg(QFileInfo(document_path_).fileName()),
        3000);
}

void ShellWindow::save_document_as()
{
    QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save setup"),
        document_path_.isEmpty() ? QStringLiteral("lab.json") : document_path_,
        QStringLiteral("Virtual Serial Studio setups (*.json)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(QLatin1String(".json"), Qt::CaseInsensitive))
        path += QStringLiteral(".json");
    document_path_ = path;
    save_document();
}

bool ShellWindow::confirm_discard()
{
    if (!dirty_)
        return true;
    const QSettings settings;
    if (!settings.value(QStringLiteral("ui/askOnDiscard"), true).toBool())
        return true;

    QMessageBox box(
        QMessageBox::Question, QStringLiteral("Unsaved changes"),
        QStringLiteral("Your ports and links have changed since the "
                       "last save. You can turn this prompt off in "
                       "Settings."),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this);
    box.setWindowTitle(QStringLiteral("Save changes to %1?")
                           .arg(document_name(document_path_)));
    box.button(QMessageBox::Save)->setText(QStringLiteral("Save"));
    box.button(QMessageBox::Discard)->setText(QStringLiteral("Don't save"));
    box.button(QMessageBox::Cancel)->setText(QStringLiteral("Cancel"));
    box.setDefaultButton(QMessageBox::Save);
    const int answer = box.exec();
    if (answer == QMessageBox::Save)
    {
        save_document();
        return !dirty_;
    }
    return answer == QMessageBox::Discard;
}

void ShellWindow::add_recent(const QString& path, const QString& summary)
{
    QSettings settings;
    QStringList recent =
        settings.value(QLatin1String(kRecentFiles)).toStringList();
    recent.removeAll(path);
    recent.prepend(path);
    while (recent.size() > 10)
        recent.removeLast();
    settings.setValue(QLatin1String(kRecentFiles), recent);
    settings.setValue(QLatin1String(kRecentSummary), summary);
    launcher_->refresh();
}

void ShellWindow::set_dirty(bool dirty)
{
    if (dirty_ == dirty)
        return;
    dirty_ = dirty;
    update_title();
}

void ShellWindow::save_layout()
{
    QSettings settings;
    settings.setValue(QStringLiteral("ui/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("ui/windowState"), saveState());
}

void ShellWindow::restore_layout()
{
    QSettings settings;
    const QByteArray geometry =
        settings.value(QStringLiteral("ui/geometry")).toByteArray();
    if (!geometry.isEmpty())
        restoreGeometry(geometry);
    const QByteArray state =
        settings.value(QStringLiteral("ui/windowState")).toByteArray();
    if (!state.isEmpty())
    {
        restored_state_ = restoreState(state);
    }
}

void ShellWindow::reset_layout()
{
    resizeDocks(
        {ports_dock_},
        {Theme::instance().metric(QStringLiteral("layout.ports.default"))},
        Qt::Horizontal);
    resizeDocks(
        {inspector_dock_},
        {Theme::instance().metric(QStringLiteral("layout.inspector.default"))},
        Qt::Horizontal);
    apply_dock_sizes();
    show_workspace();
    status_->showMessage(QStringLiteral("Layout reset."), 2500);
}

void ShellWindow::apply_dock_sizes()
{
    const auto metric = [](const char* name)
    { return Theme::instance().metric(QString::fromLatin1(name)); };
    const int ports =
        metric(compact_ ? "layout.ports.min" : "layout.ports.default");
    const int inspector =
        metric(compact_ ? "layout.inspector.min" : "layout.inspector.default");
    const int monitor = compact_ ? 156 : metric("layout.monitor.default");

    // QMainWindow honours the requested split only when the dock cannot be
    // smaller at that moment. Hold the floor for the first layout pass, then
    // relax it to the documented minimum so the user can still resize.
    ports_dock_->setMinimumWidth(ports);
    inspector_dock_->setMinimumWidth(inspector);
    monitor_dock_->setMinimumHeight(monitor);
    resizeDocks({ports_dock_, inspector_dock_}, {ports, inspector},
                Qt::Horizontal);
    resizeDocks({send_dock_}, {metric("layout.send.default")}, Qt::Vertical);
    resizeDocks({monitor_dock_}, {monitor}, Qt::Vertical);

    if (!compact_)
    {
        QTimer::singleShot(
            0, this,
            [this]
            {
                ports_dock_->setMinimumWidth(Theme::instance().metric(
                    QStringLiteral("layout.ports.min")));
                inspector_dock_->setMinimumWidth(Theme::instance().metric(
                    QStringLiteral("layout.inspector.min")));
                monitor_dock_->setMinimumHeight(Theme::instance().metric(
                    QStringLiteral("layout.monitor.min")));
            });
    }
}

void ShellWindow::apply_theme_setting()
{
    const QString theme =
        QSettings()
            .value(QStringLiteral("ui/theme"), QStringLiteral("system"))
            .toString();
    if (theme == QLatin1String("light"))
        Theme::instance().set_mode(theme_mode::light);
    else if (theme == QLatin1String("dark"))
        Theme::instance().set_mode(theme_mode::dark);
    else
        Theme::instance().set_mode(theme_mode::system);
    for (const QString& name :
         {QStringLiteral("system"), QStringLiteral("light"),
          QStringLiteral("dark")})
    {
        QAction* action =
            registry_.action(QStringLiteral("view.theme.") + name);
        if (action != nullptr)
        {
            const QSignalBlocker blocker(action);
            action->setChecked(theme == name);
        }
    }
}

void ShellWindow::apply_density_setting()
{
    const QString density =
        QSettings()
            .value(QStringLiteral("ui/density"), QStringLiteral("comfortable"))
            .toString();
    Theme::instance().set_density(density == QLatin1String("compact")
                                      ? density_mode::compact
                                      : density_mode::comfortable);
    for (const QString& name :
         {QStringLiteral("comfortable"), QStringLiteral("compact")})
    {
        QAction* action =
            registry_.action(QStringLiteral("view.density.") + name);
        if (action != nullptr)
        {
            const QSignalBlocker blocker(action);
            action->setChecked(density == name);
        }
    }
}

void ShellWindow::apply_menu_palette()
{
    if (menuBar() == nullptr)
        return;
    QPalette palette = Theme::instance().palette();
    const QColor ink = Theme::instance().colour(colour_role::ink);
    palette.setColor(QPalette::WindowText, ink);
    palette.setColor(QPalette::ButtonText, ink);
    palette.setColor(QPalette::Text, ink);
    menuBar()->setPalette(palette);
}

void ShellWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    apply_compact();
}

void ShellWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    if (!layout_sized_)
    {
        if (!restored_state_)
        {
            // QMainWindow lays the docks out after the first show; a queued
            // resize is what makes the default 360/340/196 sizes stick.
            QTimer::singleShot(0, this,
                               [this]
                               {
                                   if (!restored_state_)
                                       apply_dock_sizes();
                               });
        }
        layout_sized_ = true;
    }
    apply_compact();
}

void ShellWindow::closeEvent(QCloseEvent* event)
{
    if (!confirm_discard())
    {
        event->ignore();
        return;
    }
    save_layout();
    event->accept();
}

bool ShellWindow::run_selftest()
{
    QStringList failures;
    const auto fail = [&failures](const QString& text)
    {
        failures << text;
        std::fprintf(stderr, "selftest: FAIL: %s\n", text.toUtf8().constData());
    };

    show();
    QApplication::processEvents();

    if (central_->currentWidget() != launcher_)
        fail(QStringLiteral("the empty studio did not show the launcher"));
    if (registry_.actions().isEmpty())
        fail(QStringLiteral("the action registry is empty"));
    if (!registry_.shortcuts_unique())
    {
        fail(QStringLiteral("duplicate shortcuts: %1")
                 .arg(registry_.duplicate_shortcuts().join(
                     QStringLiteral(", "))));
    }
    if (registry_.action(QStringLiteral("file.new")) == nullptr)
        fail(QStringLiteral("File, New is not registered"));
    if (registry_.action(QStringLiteral("tools.search")) == nullptr)
        fail(QStringLiteral("the command palette action is not registered"));
    if (topology_ == nullptr ||
        topology_->accessibleName() != topology_text::view_name())
    {
        fail(QStringLiteral("the topology view is missing its name"));
    }
    if (links_action_ == nullptr ||
        registry_.action(QStringLiteral("view.links")) == nullptr)
    {
        fail(QStringLiteral("the links list action is not registered"));
    }

    const auto created = backend_.create_virtual_port(
        QStringLiteral("selftest"), vss::virtual_port_kind::loopback, true);
    if (!created)
    {
        fail(QStringLiteral("create_virtual_port: %1")
                 .arg(error_text(created.error())));
    }
    else
    {
        backend_.refresh();
        QApplication::processEvents();
        if (central_->currentWidget() != workspace_)
            fail(QStringLiteral("the workspace did not replace the launcher"));
        if (!ports_dock_->isVisible())
            fail(QStringLiteral("the Ports dock is not visible"));
        if (topology_->node_count() != 1)
        {
            fail(QStringLiteral("the topology did not draw the created port"));
        }
        if (!topology_->summary().contains(QStringLiteral("1 port")))
            fail(QStringLiteral("the topology summary did not update"));
        const auto removed = backend_.remove_port(*created);
        if (!removed)
            fail(QStringLiteral("remove_port: %1")
                     .arg(error_text(removed.error())));
        backend_.refresh();
        QApplication::processEvents();
        if (central_->currentWidget() != launcher_)
            fail(QStringLiteral("the launcher did not return after removal"));
    }

    set_dirty(false);
    if (!failures.isEmpty())
    {
        for (const auto& text : failures)
            std::fprintf(stderr, "selftest: %s\n", text.toUtf8().constData());
        return false;
    }
    return true;
}

} // namespace vss::gui
