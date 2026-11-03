//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "panes/monitor_panel.hpp"

#include "panes/monitor_model.hpp"
#include "theme/theme.hpp"
#include "widgets/chips.hpp"
#include "widgets/controls.hpp"
#include "widgets/notes.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QTableView>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace vss::gui
{
namespace
{

QFont semibold(QFont font)
{
    font.setWeight(QFont::DemiBold);
    return font;
}

QString kind_name(vss::event_kind kind)
{
    switch (kind)
    {
    case vss::event_kind::started:
        return QStringLiteral("started");
    case vss::event_kind::stopped:
        return QStringLiteral("stopped");
    case vss::event_kind::data:
        return QStringLiteral("data");
    case vss::event_kind::lines:
        return QStringLiteral("lines");
    case vss::event_kind::error:
        return QStringLiteral("error");
    case vss::event_kind::stats:
        return QStringLiteral("stats");
    case vss::event_kind::log_message:
        return QStringLiteral("log");
    }
    return {};
}

/// DirectionBadge, painted in the monitor delegate rather than a per-row
/// widget.
class DirectionDelegate final : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        const Theme& theme = Theme::instance();
        const bool selected = (option.state & QStyle::State_Selected) != 0;
        painter->save();
        painter->fillRect(option.rect,
                          selected ? theme.colour(colour_role::accentSoft)
                                   : theme.colour(colour_role::panel));
        const auto direction = static_cast<traffic_direction>(
            index.data(MonitorModel::direction_role).toInt());
        const QString text = index.data(Qt::DisplayRole).toString();
        QColor fill = theme.colour(colour_role::panel);
        QColor edge = theme.colour(colour_role::line);
        QColor ink = theme.colour(colour_role::ink);
        if (direction == traffic_direction::tx)
        {
            fill = theme.colour(colour_role::accentSoft);
            edge = theme.colour(colour_role::accentLine);
            ink = theme.colour(colour_role::accentStrong);
        }
        else if (direction == traffic_direction::drop)
        {
            fill = theme.colour(colour_role::badSoft);
            edge = theme.colour(colour_role::badLine);
            ink = theme.colour(colour_role::bad);
        }
        const QRect badge(option.rect.left() + 6, option.rect.center().y() - 9,
                          44, 18);
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(QPen(edge, 1.0));
        painter->setBrush(fill);
        painter->drawRoundedRect(QRectF(badge).adjusted(0.5, 0.5, -0.5, -0.5),
                                 4, 4);
        QFont font = theme.font(text_style::caption);
        font.setWeight(QFont::DemiBold);
        painter->setFont(font);
        painter->setPen(ink);
        painter->drawText(badge, Qt::AlignCenter, text);
        painter->restore();
    }
};

} // namespace

MonitorPanel::MonitorPanel(StudioBackend& backend, QWidget* parent)
    : QWidget(parent), backend_(backend)
{
    build();
    connect(&backend_, &StudioBackend::batch_ready, this,
            [this](const event_batch& events) { handle_events(events); });
    connect(&backend_, &StudioBackend::ports_changed, this,
            [this]
            {
                refresh_ports();
                update_drop_rows();
                update_empty();
            });
    connect(&backend_, &StudioBackend::links_changed, this,
            [this]
            {
                update_drop_rows();
                refresh_ports();
            });
    refresh_ports();
    update_actions();
    update_empty();
}

void MonitorPanel::build()
{
    setAccessibleName(QStringLiteral("Monitor"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    tabs_ = new QTabWidget(this);
    tabs_->setDocumentMode(true);
    tabs_->setTabBarAutoHide(true);
    tabs_->setAccessibleName(QStringLiteral("Monitor tabs"));
    root->addWidget(tabs_);

    monitor_page_ = new QWidget(tabs_);
    auto* page_layout = new QVBoxLayout(monitor_page_);
    page_layout->setContentsMargins(0, 0, 0, 0);
    page_layout->setSpacing(0);

    simple_bar_ = new QWidget(monitor_page_);
    simple_bar_->setFixedHeight(36);
    auto* simple_layout = new QHBoxLayout(simple_bar_);
    simple_layout->setContentsMargins(12, 0, 12, 0);
    simple_layout->setSpacing(12);
    simple_pause_ = new QPushButton(QStringLiteral("Pause"), simple_bar_);
    simple_pause_->setProperty("size", QStringLiteral("small"));
    polish(simple_pause_);
    clear_ = new QPushButton(QStringLiteral("Clear"), simple_bar_);
    clear_->setProperty("size", QStringLiteral("small"));
    polish(clear_);
    hex_ = new QCheckBox(QStringLiteral("Hex"), simple_bar_);
    follow_ = new QCheckBox(QStringLiteral("Follow"), simple_bar_);
    follow_->setChecked(true);
    all_ports_ = new QLabel(QStringLiteral("All ports"), simple_bar_);
    all_ports_->setFont(Theme::instance().font(text_style::caption));
    all_ports_->setProperty("muted", true);
    polish(all_ports_);
    simple_layout->addWidget(simple_pause_);
    simple_layout->addWidget(clear_);
    simple_layout->addWidget(hex_);
    simple_layout->addWidget(follow_);
    simple_layout->addStretch(1);
    simple_layout->addWidget(all_ports_);
    page_layout->addWidget(simple_bar_);

    advanced_bar_ = new QWidget(monitor_page_);
    advanced_bar_->setFixedHeight(36);
    auto* advanced_layout = new QHBoxLayout(advanced_bar_);
    advanced_layout->setContentsMargins(12, 0, 12, 0);
    advanced_layout->setSpacing(8);
    port_filter_ = new QComboBox(advanced_bar_);
    port_filter_->setAccessibleName(QStringLiteral("Filter by port"));
    port_filter_->setMinimumWidth(120);
    port_filter_->setMaximumWidth(160);
    rx_ = new QCheckBox(QStringLiteral("RX"), advanced_bar_);
    tx_ = new QCheckBox(QStringLiteral("TX"), advanced_bar_);
    drop_ = new QCheckBox(QStringLiteral("Dropped"), advanced_bar_);
    rx_->setChecked(true);
    tx_->setChecked(true);
    drop_->setChecked(true);
    search_ = new QLineEdit(advanced_bar_);
    search_->setPlaceholderText(QStringLiteral("Search payload"));
    search_->setAccessibleName(QStringLiteral("Search payload"));
    search_->setClearButtonEnabled(true);
    search_->setMaximumWidth(190);
    mode_ = new SegmentedControl(advanced_bar_);
    mode_->set_compact(true);
    mode_->setAccessibleName(QStringLiteral("Preview format"));
    mode_->add_item(QStringLiteral("ASCII"));
    mode_->add_item(QStringLiteral("Hex"), 1);
    pause_ = new QPushButton(QStringLiteral("Pause"), advanced_bar_);
    pause_->setProperty("size", QStringLiteral("small"));
    polish(pause_);
    auto* advanced_clear =
        new QPushButton(QStringLiteral("Clear"), advanced_bar_);
    advanced_clear->setProperty("size", QStringLiteral("small"));
    polish(advanced_clear);
    auto* advanced_follow =
        new QCheckBox(QStringLiteral("Follow"), advanced_bar_);
    advanced_follow->setChecked(true);
    export_ = new QPushButton(QStringLiteral("Export"), advanced_bar_);
    export_->setProperty("size", QStringLiteral("small"));
    polish(export_);
    advanced_layout->addWidget(port_filter_);
    advanced_layout->addWidget(rx_);
    advanced_layout->addWidget(tx_);
    advanced_layout->addWidget(drop_);
    advanced_layout->addWidget(search_);
    advanced_layout->addWidget(mode_);
    advanced_layout->addStretch(1);
    advanced_layout->addWidget(pause_);
    advanced_layout->addWidget(advanced_clear);
    advanced_layout->addWidget(advanced_follow);
    advanced_layout->addWidget(export_);
    advanced_bar_->setVisible(false);
    page_layout->addWidget(advanced_bar_);

    model_ = new MonitorModel(this);
    table_ = new QTableView(monitor_page_);
    table_->setModel(model_);
    table_->setAccessibleName(QStringLiteral("Traffic"));
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setShowGrid(false);
    table_->setWordWrap(false);
    table_->verticalHeader()->hide();
    table_->verticalHeader()->setDefaultSectionSize(28);
    table_->horizontalHeader()->setHighlightSections(false);
    table_->horizontalHeader()->setFixedHeight(28);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    table_->setColumnWidth(0, 130);
    table_->setColumnWidth(1, 140);
    table_->setColumnWidth(2, 70);
    table_->setColumnWidth(3, 70);
    table_->setItemDelegateForColumn(2, new DirectionDelegate(table_));
    table_->setFocusPolicy(Qt::StrongFocus);
    table_->setMinimumHeight(60);
    table_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    empty_ = new QLabel(monitor_page_);
    empty_->setAlignment(Qt::AlignCenter);
    empty_->setWordWrap(true);
    empty_->setFont(Theme::instance().font(text_style::body));
    empty_->setProperty("muted", true);
    empty_->setContentsMargins(16, 16, 16, 16);
    empty_->setText(
        QStringLiteral("No traffic yet. Send some bytes, or open the port in a "
                       "program."));

    content_ = new QStackedWidget(monitor_page_);
    content_->setMinimumHeight(60);
    content_->addWidget(table_);
    content_->addWidget(empty_);
    page_layout->addWidget(content_, 1);
    tabs_->addTab(monitor_page_, QStringLiteral("Monitor"));

    log_ = new QPlainTextEdit(tabs_);
    log_->setReadOnly(true);
    log_->setAccessibleName(QStringLiteral("Log"));
    log_->setFont(Theme::instance().font(text_style::data));
    log_->setMaximumBlockCount(10'000);
    tabs_->addTab(log_, QStringLiteral("Log"));

    json_ = new QLabel(
        QStringLiteral("The topology JSON editor arrives with WP7."), tabs_);
    json_->setAlignment(Qt::AlignCenter);
    json_->setWordWrap(true);
    json_->setProperty("muted", true);
    polish(json_);
    tabs_->addTab(json_, QStringLiteral("Topology JSON"));
    tabs_->setTabVisible(1, false);
    tabs_->setTabVisible(2, false);
    tabs_->tabBar()->hide();

    auto* pause_shortcut = new QShortcut(QKeySequence(Qt::Key_Space), table_);
    pause_shortcut->setContext(Qt::WidgetShortcut);
    connect(pause_shortcut, &QShortcut::activated, this,
            [this] { toggle_pause(); });
    auto* search_shortcut = new QShortcut(QKeySequence::Find, this);
    search_shortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(search_shortcut, &QShortcut::activated, this,
            [this]
            {
                if (advanced_)
                    search_->setFocus();
            });

    connect(pause_, &QPushButton::clicked, this, [this] { toggle_pause(); });
    connect(simple_pause_, &QPushButton::clicked, this,
            [this] { toggle_pause(); });
    connect(advanced_clear, &QPushButton::clicked, this,
            [this]
            {
                model_->clear();
                update_actions();
                update_empty();
            });
    connect(advanced_follow, &QCheckBox::toggled, this,
            [this](bool on) { follow_enabled_ = on; });
    connect(clear_, &QPushButton::clicked, this,
            [this]
            {
                model_->clear();
                update_actions();
                update_empty();
            });
    connect(export_, &QPushButton::clicked, this, [this] { export_visible(); });
    connect(hex_, &QCheckBox::toggled, this,
            [this](bool on)
            {
                model_->set_hex(on);
                const QSignalBlocker blocker(mode_);
                mode_->set_current(on ? 1 : 0);
            });
    connect(follow_, &QCheckBox::toggled, this,
            [this](bool on) { follow_enabled_ = on; });
    connect(mode_, &SegmentedControl::current_changed, this,
            [this](int id)
            {
                model_->set_hex(id == 1);
                const QSignalBlocker blocker(hex_);
                hex_->setChecked(id == 1);
            });
    connect(port_filter_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { update_filters(); });
    for (QCheckBox* box : {rx_, tx_, drop_})
        connect(box, &QCheckBox::toggled, this,
                [this](bool) { update_filters(); });
    connect(search_, &QLineEdit::textChanged, this,
            [this](const QString&) { update_filters(); });
    connect(table_, &QTableView::clicked, this,
            [this](const QModelIndex& index)
            {
                const auto id = model_->port_at(index.row());
                if (id != 0)
                    Q_EMIT port_selected(id);
            });
    connect(model_, &QAbstractItemModel::modelReset, this,
            [this]
            {
                update_actions();
                update_empty();
                follow_if_enabled();
            });
}

void MonitorPanel::set_advanced(bool advanced)
{
    if (advanced_ == advanced)
        return;
    advanced_ = advanced;
    simple_bar_->setVisible(!advanced);
    advanced_bar_->setVisible(advanced);
    tabs_->setTabVisible(1, advanced && show_advanced_tabs_);
    tabs_->setTabVisible(2, advanced && show_advanced_tabs_);
    tabs_->tabBar()->setVisible(advanced && show_advanced_tabs_);
    if (!advanced)
        tabs_->setCurrentIndex(0);
    update_filters();
    update_actions();
}

void MonitorPanel::set_compact(bool compact)
{
    table_->verticalHeader()->setDefaultSectionSize(compact ? 24 : 28);
}

void MonitorPanel::set_show_advanced_tabs(bool show)
{
    if (show_advanced_tabs_ == show)
        return;
    show_advanced_tabs_ = show;
    const bool advanced = advanced_;
    advanced_ = !advanced;
    set_advanced(advanced);
}

void MonitorPanel::set_selected_port(vss::endpoint_id id)
{
    model_->set_selected_port(id);
}

void MonitorPanel::refresh_ports()
{
    QStringList signature;
    for (const auto& port : backend_.ports())
    {
        signature << QStringLiteral("%1|%2")
                         .arg(static_cast<qulonglong>(port.id))
                         .arg(port.name);
    }
    if (signature == port_signature_)
        return;
    port_signature_ = signature;

    const vss::endpoint_id previous =
        port_filter_ != nullptr && port_filter_->currentIndex() >= 0
            ? static_cast<vss::endpoint_id>(
                  port_filter_->currentData().toULongLong())
            : 0;
    refreshing_ports_ = true;
    if (port_filter_ != nullptr)
    {
        const QSignalBlocker blocker(port_filter_);
        port_filter_->clear();
        port_filter_->addItem(QStringLiteral("All ports"),
                              QVariant::fromValue<qulonglong>(0));
        for (const auto& port : backend_.ports())
        {
            port_filter_->addItem(port.name,
                                  QVariant::fromValue<qulonglong>(port.id));
        }
        const int index =
            port_filter_->findData(QVariant::fromValue<qulonglong>(previous));
        port_filter_->setCurrentIndex(index >= 0 ? index : 0);
    }
    refreshing_ports_ = false;
    if (all_ports_ != nullptr)
        all_ports_->setText(QStringLiteral("All ports"));
    update_filters();
}

void MonitorPanel::handle_events(const event_batch& events)
{
    model_->append_events(events);
    for (const gui_event& event : events)
    {
        if (event.kind == vss::event_kind::data)
            continue;
        append_log(event);
    }
    update_actions();
    update_empty();
    follow_if_enabled();
}

void MonitorPanel::update_filters()
{
    if (model_ == nullptr || port_filter_ == nullptr)
        return;
    const vss::endpoint_id port =
        refreshing_ports_ ? 0
                          : static_cast<vss::endpoint_id>(
                                port_filter_->currentData().toULongLong());
    if (advanced_)
    {
        model_->set_filters(port, rx_->isChecked(), tx_->isChecked(),
                            drop_->isChecked(), search_->text());
    }
    else
    {
        model_->set_filters(0, true, true, true, {});
    }
    update_empty();
}

void MonitorPanel::update_actions()
{
    if (model_ == nullptr)
        return;
    const QString pause_text =
        model_->paused() ? QStringLiteral("Resume") : QStringLiteral("Pause");
    pause_->setText(pause_text);
    if (simple_pause_ != nullptr)
        simple_pause_->setText(pause_text);
    if (export_ != nullptr)
        export_->setEnabled(model_->rowCount() > 0);
    if (hex_ != nullptr && hex_->isChecked() != model_->hex())
    {
        const QSignalBlocker blocker(hex_);
        hex_->setChecked(model_->hex());
    }
    if (mode_ != nullptr)
    {
        const QSignalBlocker blocker(mode_);
        mode_->set_current(model_->hex() ? 1 : 0);
    }
}

void MonitorPanel::update_empty()
{
    if (content_ == nullptr || model_ == nullptr)
        return;
    if (model_->rowCount() > 0)
    {
        content_->setCurrentWidget(table_);
        return;
    }
    if (model_->total_rows() > 0)
    {
        empty_->setText(QStringLiteral("No traffic matches these filters."));
    }
    else
    {
        empty_->setText(
            QStringLiteral("No traffic yet. Send some bytes, or open the port "
                           "in a program."));
    }
    content_->setCurrentWidget(empty_);
}

void MonitorPanel::update_drop_rows()
{
    const auto links = backend_.links();
    const auto ports = backend_.ports();
    QHash<vss::link_id, std::uint64_t> current;
    for (const auto& link : links)
    {
        current.insert(link.id, link.stats.dropped);
        const auto previous = previous_drops_.constFind(link.id);
        if (previous == previous_drops_.constEnd())
            continue;
        if (link.stats.dropped <= *previous)
            continue;
        const auto name_of = [&ports](vss::endpoint_id id) -> QString
        {
            const auto it = std::find_if(ports.begin(), ports.end(),
                                         [id](const port_view& view)
                                         { return view.id == id; });
            return it == ports.end() ? QStringLiteral("link") : it->name;
        };
        const vss::endpoint_id id = !link.sources.empty() ? link.sources.front()
                                    : !link.targets.empty()
                                        ? link.targets.front()
                                        : 0;
        model_->append_drop(name_of(id), link.stats.dropped - *previous);
    }
    previous_drops_ = std::move(current);
}

void MonitorPanel::append_log(const gui_event& event)
{
    if (log_ == nullptr)
        return;
    const QString time =
        QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    QString text = event.error;
    if (text.isEmpty())
        text = event.message;
    if (text.isEmpty())
        text = kind_name(event.kind);
    log_->appendPlainText(
        QStringLiteral("%1  %2  %3  %4")
            .arg(time, event.name, kind_name(event.kind), text));
}

void MonitorPanel::toggle_pause()
{
    model_->set_paused(!model_->paused());
    update_actions();
}

void MonitorPanel::export_visible()
{
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export visible traffic"),
        QStringLiteral("traffic.txt"),
        QStringLiteral("Text files (*.txt);;All files (*)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        Q_EMIT error_message(QStringLiteral("Could not save %1.").arg(path));
        return;
    }
    file.write(model_->export_text().toUtf8());
    Q_EMIT status_message(QStringLiteral("Exported %1.").arg(path));
}

void MonitorPanel::follow_if_enabled()
{
    if (!follow_enabled_ || table_ == nullptr)
        return;
    QTimer::singleShot(0, table_,
                       [this]
                       {
                           if (model_->rowCount() > 0)
                               table_->scrollToBottom();
                       });
}

} // namespace vss::gui
