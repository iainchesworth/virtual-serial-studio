//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "connect_dialog.hpp"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QVBoxLayout>

namespace vss::gui {
namespace {

QString display_name(const vss::endpoint_info& endpoint)
{
    return QStringLiteral("%1  (%2, %3)")
        .arg(QString::fromStdString(endpoint.name),
             QString::fromLatin1(to_string(endpoint.kind)),
             QString::fromLatin1(to_string(endpoint.state)));
}

QString mode_text(connect_mode mode)
{
    switch (mode)
    {
    case connect_mode::duplex: return QObject::tr("Two-way (connect)");
    case connect_mode::redirect: return QObject::tr("One-way redirect");
    case connect_mode::monitor: return QObject::tr("Monitor (one-way copy)");
    case connect_mode::split: return QObject::tr("Split one port to many");
    case connect_mode::join: return QObject::tr("Join many ports into one");
    }
    return {};
}

} // namespace

ConnectDialog::ConnectDialog(std::vector<vss::endpoint_info> endpoints,
                             QWidget* parent)
    : QDialog(parent), endpoints_(std::move(endpoints))
{
    setWindowTitle(tr("Connect ports"));
    setModal(true);

    mode_ = new QComboBox(this);
    mode_->addItem(mode_text(connect_mode::duplex),
                   static_cast<int>(connect_mode::duplex));
    mode_->addItem(mode_text(connect_mode::redirect),
                   static_cast<int>(connect_mode::redirect));
    mode_->addItem(mode_text(connect_mode::monitor),
                   static_cast<int>(connect_mode::monitor));
    mode_->addItem(mode_text(connect_mode::split),
                   static_cast<int>(connect_mode::split));
    mode_->addItem(mode_text(connect_mode::join),
                   static_cast<int>(connect_mode::join));

    source_ = new QComboBox(this);
    target_ = new QComboBox(this);
    sources_ = new QListWidget(this);
    targets_ = new QListWidget(this);
    sources_->setSelectionMode(QAbstractItemView::MultiSelection);
    targets_->setSelectionMode(QAbstractItemView::MultiSelection);
    sources_->setMaximumHeight(110);
    targets_->setMaximumHeight(110);

    for (const auto& endpoint : endpoints_)
    {
        const QString label = display_name(endpoint);
        source_->addItem(label, QString::fromStdString(endpoint.name));
        target_->addItem(label, QString::fromStdString(endpoint.name));
        auto* source_item = new QListWidgetItem(label, sources_);
        source_item->setData(Qt::UserRole,
                             QString::fromStdString(endpoint.name));
        auto* target_item = new QListWidgetItem(label, targets_);
        target_item->setData(Qt::UserRole,
                             QString::fromStdString(endpoint.name));
    }

    summary_ = new QLabel(this);
    summary_->setWordWrap(true);
    summary_->setStyleSheet(QStringLiteral("color: palette(mid);"));
    error_ = new QLabel(this);
    error_->setWordWrap(true);
    error_->setStyleSheet(QStringLiteral("color: #cf222e;"));
    error_->hide();

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(mode_);

    auto* single = new QGroupBox(tr("Ports"), this);
    auto* form = new QFormLayout(single);
    form->addRow(tr("Source:"), source_);
    form->addRow(tr("Target:"), target_);
    form->addRow(tr("Sources:"), sources_);
    form->addRow(tr("Targets:"), targets_);
    layout->addWidget(single);
    layout->addWidget(summary_);
    layout->addWidget(error_);

    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                             this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Connect"));
    connect(buttons, &QDialogButtonBox::accepted, this,
            &ConnectDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(mode_, &QComboBox::currentIndexChanged, this,
            &ConnectDialog::on_mode_changed);
    connect(source_, &QComboBox::currentIndexChanged, this,
            &ConnectDialog::update_summary);
    connect(target_, &QComboBox::currentIndexChanged, this,
            &ConnectDialog::update_summary);
    connect(sources_, &QListWidget::itemSelectionChanged, this,
            &ConnectDialog::update_summary);
    connect(targets_, &QListWidget::itemSelectionChanged, this,
            &ConnectDialog::update_summary);

    on_mode_changed();
}

void ConnectDialog::on_mode_changed()
{
    const auto mode =
        static_cast<connect_mode>(mode_->currentData().toInt());
    const bool many_targets = mode == connect_mode::split;
    const bool many_sources = mode == connect_mode::join;

    source_->setVisible(!many_sources);
    target_->setVisible(!many_targets);
    sources_->setVisible(many_sources);
    targets_->setVisible(many_targets);

    // Hide the form's labels as well, so the dialog does not show empty rows.
    if (auto* form = qobject_cast<QFormLayout*>(
            source_->parentWidget()->layout()))
    {
        for (int row = 0; row < form->rowCount(); ++row)
        {
            if (auto* item = form->itemAt(row, QFormLayout::LabelRole))
            {
                if (auto* label = qobject_cast<QLabel*>(item->widget()))
                {
                    const bool source_rows =
                        row == 0 || row == 2;
                    label->setVisible(source_rows ? !many_sources
                                                  : !many_targets);
                }
            }
        }
    }

    update_summary();
}

void ConnectDialog::update_summary()
{
    const auto mode =
        static_cast<connect_mode>(mode_->currentData().toInt());
    const QString source = source_->currentData().toString();
    const QString target = target_->currentData().toString();
    int selected_sources = 0;
    int selected_targets = 0;
    for (int index = 0; index < sources_->count(); ++index)
        selected_sources += sources_->item(index)->isSelected() ? 1 : 0;
    for (int index = 0; index < targets_->count(); ++index)
        selected_targets += targets_->item(index)->isSelected() ? 1 : 0;

    switch (mode)
    {
    case connect_mode::duplex:
        summary_->setText(tr("Data flows both ways. Either port may send, "
                             "and control lines cross like a null-modem "
                             "cable."));
        break;
    case connect_mode::redirect:
        summary_->setText(tr("Data from %1 is delivered to %2. %2 cannot "
                             "send back.")
                              .arg(source.isEmpty() ? tr("the source") : source,
                                   target.isEmpty() ? tr("the target")
                                                    : target));
        break;
    case connect_mode::monitor:
        summary_->setText(tr("A one-way copy of %1's traffic to %2, marked "
                             "as a monitor so it never drives modem lines.")
                              .arg(source.isEmpty() ? tr("the source") : source,
                                   target.isEmpty() ? tr("the target")
                                                    : target));
        break;
    case connect_mode::split:
        summary_->setText(tr("Everything received on %1 is copied to %2 "
                             "target(s).")
                              .arg(source.isEmpty() ? tr("the source")
                                                    : source)
                              .arg(selected_targets));
        break;
    case connect_mode::join:
        summary_->setText(tr("%1 publisher(s) are merged into %2; every "
                             "publisher talks to the same target.")
                              .arg(selected_sources)
                              .arg(target.isEmpty() ? tr("the target")
                                                    : target));
        break;
    }
}

void ConnectDialog::set_error(const QString& text)
{
    error_->setText(text);
    error_->setVisible(!text.isEmpty());
}

void ConnectDialog::accept()
{
    const auto mode =
        static_cast<connect_mode>(mode_->currentData().toInt());
    request_ = {};
    request_.mode = mode;

    const QString source = source_->currentData().toString();
    const QString target = target_->currentData().toString();

    switch (mode)
    {
    case connect_mode::duplex:
    case connect_mode::redirect:
    case connect_mode::monitor:
        if (source.isEmpty() || target.isEmpty())
        {
            set_error(tr("Choose a source and a target port."));
            return;
        }
        if (source == target)
        {
            set_error(tr("A port cannot be connected to itself."));
            return;
        }
        request_.sources = {source};
        request_.target = target;
        break;

    case connect_mode::split:
        if (source.isEmpty())
        {
            set_error(tr("Choose the port to split."));
            return;
        }
        for (int index = 0; index < targets_->count(); ++index)
        {
            auto* item = targets_->item(index);
            if (item->isSelected())
                request_.sources << item->data(Qt::UserRole).toString();
        }
        request_.sources.removeAll(source);
        if (request_.sources.size() < 2)
        {
            set_error(tr("Select at least two targets to split to."));
            return;
        }
        request_.target = source;
        break;

    case connect_mode::join:
        if (target.isEmpty())
        {
            set_error(tr("Choose the target port."));
            return;
        }
        for (int index = 0; index < sources_->count(); ++index)
        {
            auto* item = sources_->item(index);
            if (item->isSelected())
                request_.sources << item->data(Qt::UserRole).toString();
        }
        request_.sources.removeAll(target);
        if (request_.sources.size() < 2)
        {
            set_error(tr("Select at least two publishers to join."));
            return;
        }
        request_.target = target;
        break;
    }

    set_error({});
    QDialog::accept();
}

} // namespace vss::gui
