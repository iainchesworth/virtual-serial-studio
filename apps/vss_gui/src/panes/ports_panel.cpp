//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "panes/ports_panel.hpp"

#include "panes/port_delegate.hpp"
#include "panes/port_model.hpp"

#include <QApplication>
#include <QClipboard>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLineEdit>
#include <QListView>
#include <QStackedWidget>
#include <QTableView>
#include <QVBoxLayout>

namespace vss::gui
{

PortsPanel::PortsPanel(StudioBackend& backend, SelectionService& selection,
                       QWidget* parent)
    : QWidget(parent), backend_(backend), selection_(selection)
{
    build();
    connect(&backend_, &StudioBackend::ports_changed, this,
            [this] { refresh(); });
    connect(&backend_, &StudioBackend::links_changed, this,
            [this] { refresh(); });
    connect(&selection_, &SelectionService::port_changed, this,
            [this](vss::endpoint_id id) { select_port(id); });
    refresh();
}

void PortsPanel::build()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    filter_ = new QLineEdit(this);
    filter_->setPlaceholderText(QStringLiteral("Filter ports"));
    filter_->setAccessibleName(QStringLiteral("Filter ports"));
    filter_->setClearButtonEnabled(true);
    filter_->setVisible(false);
    filter_->setContentsMargins(8, 8, 8, 8);
    connect(filter_, &QLineEdit::textChanged, this,
            [this](const QString& text)
            {
                const vss::endpoint_id selected = selection_.port();
                model_->set_filter(text);
                if (selected != 0)
                    select_port(selected);
            });
    layout->addWidget(filter_);

    model_ = new PortModel(this);
    stack_ = new QStackedWidget(this);

    simple_ = new QListView(stack_);
    simple_->setModel(model_);
    simple_->setAccessibleName(QStringLiteral("Ports"));
    simple_->setSelectionMode(QAbstractItemView::SingleSelection);
    simple_->setSelectionBehavior(QAbstractItemView::SelectRows);
    simple_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    simple_->setUniformItemSizes(true);
    simple_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    simple_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    simple_->setMouseTracking(true);
    simple_delegate_ = new PortDelegate(simple_);
    simple_delegate_->set_copy_handler(
        [this](vss::endpoint_id id)
        {
            select_port(id);
            copy_selected_path();
        });
    simple_->setItemDelegate(simple_delegate_);
    simple_->installEventFilter(this);
    connect(simple_->selectionModel(), &QItemSelectionModel::currentChanged,
            this, [this](const QModelIndex& current, const QModelIndex&)
            { on_current_changed(current); });

    table_ = new QTableView(stack_);
    table_->setModel(model_);
    table_->setAccessibleName(QStringLiteral("Ports table"));
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    table_->setAlternatingRowColors(false);
    table_->verticalHeader()->hide();
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->horizontalHeader()->setHighlightSections(false);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    table_->setColumnWidth(1, 96);
    for (int column = 2; column <= 4; ++column)
    {
        table_->horizontalHeader()->setSectionResizeMode(column,
                                                         QHeaderView::Fixed);
        table_->setColumnWidth(column, column == 4 ? 64 : 52);
    }
    table_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    table_->verticalHeader()->setDefaultSectionSize(44);
    table_delegate_ = new PortDelegate(table_);
    table_delegate_->set_table_mode(true);
    table_delegate_->set_copy_handler(
        [this](vss::endpoint_id id)
        {
            select_port(id);
            copy_selected_path();
        });
    table_->setItemDelegate(table_delegate_);
    table_->installEventFilter(this);
    connect(table_->selectionModel(), &QItemSelectionModel::currentChanged,
            this, [this](const QModelIndex& current, const QModelIndex&)
            { on_current_changed(current); });

    stack_->addWidget(simple_);
    stack_->addWidget(table_);
    layout->addWidget(stack_, 1);
}

void PortsPanel::refresh()
{
    const auto ports = backend_.ports();
    const auto links = backend_.links();
    const vss::endpoint_id wanted = selection_.port();
    syncing_ = true;
    model_->set_snapshot(ports, links);
    syncing_ = false;

    Q_EMIT count_changed(static_cast<int>(ports.size()));

    if (ports.empty())
    {
        if (selection_.port() != 0)
            selection_.select_port(0);
        return;
    }

    const int wanted_row = wanted != 0 ? model_->row_for_id(wanted) : -1;
    if (wanted_row < 0)
    {
        const int first = model_->rowCount() > 0 ? 0 : -1;
        if (first >= 0)
            selection_.select_port(model_->port_id(first));
        return;
    }
    select_port(wanted);
}

void PortsPanel::set_advanced(bool advanced)
{
    if (advanced_ == advanced)
        return;
    advanced_ = advanced;
    filter_->setVisible(advanced);
    stack_->setCurrentWidget(advanced ? static_cast<QWidget*>(table_)
                                      : static_cast<QWidget*>(simple_));
    if (advanced)
    {
        model_->set_filter(filter_->text());
        table_delegate_->set_compact(compact_);
    }
    select_port(selection_.port());
}

void PortsPanel::set_compact(bool compact)
{
    compact_ = compact;
    if (simple_delegate_ != nullptr)
        simple_delegate_->set_compact(compact);
    if (table_delegate_ != nullptr)
        table_delegate_->set_compact(compact);
    if (simple_ != nullptr)
    {
        simple_->setSpacing(0);
        simple_->doItemsLayout();
    }
}

void PortsPanel::select_port(vss::endpoint_id id)
{
    if (syncing_ || model_ == nullptr)
        return;
    if (id == 0)
    {
        syncing_ = true;
        simple_->selectionModel()->clearCurrentIndex();
        table_->selectionModel()->clearCurrentIndex();
        syncing_ = false;
        return;
    }
    if (selection_.port() != id)
    {
        syncing_ = true;
        selection_.select_port(id);
        syncing_ = false;
    }
    sync_views(id);
}

void PortsPanel::on_current_changed(const QModelIndex& current)
{
    if (syncing_ || !current.isValid())
        return;
    const auto id = static_cast<vss::endpoint_id>(
        current.data(PortModel::port_id_role).toULongLong());
    if (id == 0)
        return;

    syncing_ = true;
    if (selection_.port() != id)
        selection_.select_port(id);
    sync_views(id);
    syncing_ = false;
}

void PortsPanel::sync_views(vss::endpoint_id id)
{
    const int row = model_->row_for_id(id);
    if (row < 0)
        return;
    const bool previous = syncing_;
    syncing_ = true;
    const QModelIndex index = model_->index(row, 0);
    if (simple_->currentIndex() != index)
        simple_->setCurrentIndex(index);
    if (table_->currentIndex() != index)
        table_->setCurrentIndex(index);
    simple_->scrollTo(index, QAbstractItemView::EnsureVisible);
    table_->scrollTo(index, QAbstractItemView::EnsureVisible);
    syncing_ = previous;
}

int PortsPanel::current_row() const
{
    const QModelIndex current =
        advanced_ ? table_->currentIndex() : simple_->currentIndex();
    return current.isValid() ? current.row() : -1;
}

void PortsPanel::copy_selected_path()
{
    const int row = current_row();
    if (row < 0)
        return;
    const auto port = model_->port_at(row);
    if (!port || port->path.isEmpty())
        return;
    QApplication::clipboard()->setText(port->path);
    Q_EMIT path_copied(port->path);
    Q_EMIT status_message(QStringLiteral("Copied %1.").arg(port->path));
}

bool PortsPanel::eventFilter(QObject* watched, QEvent* event)
{
    if ((watched == simple_ || watched == table_) &&
        event->type() == QEvent::KeyPress)
    {
        auto* key = static_cast<QKeyEvent*>(event);
        const int row = current_row();
        if (row < 0)
            return QWidget::eventFilter(watched, event);
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)
        {
            Q_EMIT inspect_requested(model_->port_id(row));
            return true;
        }
        if (key->key() == Qt::Key_Delete)
        {
            Q_EMIT remove_requested(model_->port_id(row));
            return true;
        }
        if (QKeySequence(key->keyCombination()) ==
            QKeySequence(QStringLiteral("Ctrl+Shift+C")))
        {
            copy_selected_path();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace vss::gui
