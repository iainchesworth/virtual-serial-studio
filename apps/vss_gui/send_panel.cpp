//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "send_panel.hpp"

#include "theme.hpp"

#include <vss/core/bytes.hpp>
#include <vss/studio/studio.hpp>

#include <QByteArray>
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QShortcut>
#include <QVBoxLayout>
#include <QVariant>

#include <algorithm>
#include <utility>

namespace vss::gui {

SendPanel::SendPanel(vss::studio& studio, QWidget* parent)
    : QWidget(parent)
    , studio_(studio)
{
    setObjectName(QStringLiteral("send_panel"));
    setMinimumWidth(300);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    auto* target_row = new QHBoxLayout();
    target_row->addWidget(new QLabel(tr("Port:"), this));
    endpoint_combo_ = new QComboBox(this);
    endpoint_combo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    endpoint_combo_->setMinimumContentsLength(18);
    endpoint_combo_->setToolTip(tr("Only open endpoints can be written to."));
    target_row->addWidget(endpoint_combo_, 1);
    layout->addLayout(target_row);

    editor_ = new QPlainTextEdit(this);
    editor_->setPlaceholderText(tr("Type text to send…"));
    editor_->setTabChangesFocus(true);
    editor_->setFixedHeight(84);
    layout->addWidget(editor_);

    auto* options_row = new QHBoxLayout();
    hex_check_ = new QCheckBox(tr("Hex"), this);
    hex_check_->setToolTip(
        tr("Interpret the input as hexadecimal bytes, e.g. \"48 65 6c 6c 6f\"."));
    crlf_check_ = new QCheckBox(tr("Append CRLF"), this);
    crlf_check_->setToolTip(tr("Append 0D 0A after the payload."));
    options_row->addWidget(hex_check_);
    options_row->addWidget(crlf_check_);
    options_row->addStretch(1);
    layout->addLayout(options_row);

    auto* button_row = new QHBoxLayout();
    auto* hint = new QLabel(tr("Ctrl+Enter sends"), this);
    hint->setStyleSheet(QStringLiteral("color: #8b949e;"));
    button_row->addWidget(hint, 1);
    send_button_ = new QPushButton(tr("Send"), this);
    send_button_->setDefault(false);
    send_button_->setAutoDefault(false);
    button_row->addWidget(send_button_);
    layout->addLayout(button_row);

    layout->addStretch(1);

    connect(send_button_, &QPushButton::clicked, this, &SendPanel::send);
    connect(hex_check_, &QCheckBox::toggled, this, [this](bool) {
        update_placeholder();
    });
    connect(endpoint_combo_, &QComboBox::currentIndexChanged, this,
            [this](int) { update_enabled_state(); });

    auto* shortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Return")),
                                   this);
    shortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(shortcut, &QShortcut::activated, this, &SendPanel::send);

    update_placeholder();
    update_enabled_state();
}

void SendPanel::set_endpoints(
    const std::vector<vss::endpoint_info>& endpoints)
{
    std::vector<std::pair<vss::endpoint_id, QString>> open;
    for (const auto& info : endpoints)
    {
        if (!info.open)
            continue;
        open.emplace_back(
            info.id,
            QStringLiteral("%1  (%2)")
                .arg(QString::fromStdString(info.name), kind_text(info.kind)));
    }

    const vss::endpoint_id current = endpoint_combo_->currentData().toULongLong();
    open_count_ = static_cast<int>(open.size());

    if (open == present_)
    {
        update_enabled_state();
        return;
    }

    present_ = open;
    const QSignalBlocker blocker(endpoint_combo_);
    endpoint_combo_->clear();

    if (open.empty())
    {
        endpoint_combo_->addItem(tr("(no open ports)"),
                                 QVariant::fromValue<qulonglong>(0));
        endpoint_combo_->setCurrentIndex(0);
        update_enabled_state();
        return;
    }

    for (const auto& [id, label] : open)
    {
        endpoint_combo_->addItem(label,
                                 QVariant::fromValue<qulonglong>(
                                     static_cast<qulonglong>(id)));
    }

    int restored = 0;
    if (current != 0)
    {
        const int index = endpoint_combo_->findData(
            QVariant::fromValue<qulonglong>(static_cast<qulonglong>(current)));
        if (index >= 0)
            restored = index;
    }
    endpoint_combo_->setCurrentIndex(restored);
    update_enabled_state();
}

void SendPanel::set_selected_endpoint(vss::endpoint_id id)
{
    if (id == 0 || open_count_ == 0)
        return;

    const int index = endpoint_combo_->findData(
        QVariant::fromValue<qulonglong>(static_cast<qulonglong>(id)));
    if (index >= 0)
        endpoint_combo_->setCurrentIndex(index);
}

void SendPanel::update_enabled_state()
{
    const bool has_target =
        open_count_ > 0 && endpoint_combo_->currentData().toULongLong() != 0;
    editor_->setEnabled(has_target);
    hex_check_->setEnabled(has_target);
    crlf_check_->setEnabled(has_target);
    send_button_->setEnabled(has_target);

    if (!has_target)
        send_button_->setToolTip(tr("Create and open a port first."));
    else
        send_button_->setToolTip(tr("Queue the payload on the selected port."));
}

void SendPanel::update_placeholder()
{
    if (hex_check_->isChecked())
        editor_->setPlaceholderText(
            tr("Hex bytes, e.g. 48 65 6c 6c 6f 0d 0a …"));
    else
        editor_->setPlaceholderText(tr("Type text to send…"));
}

void SendPanel::send()
{
    if (open_count_ == 0)
        return;

    const auto id =
        static_cast<vss::endpoint_id>(endpoint_combo_->currentData().toULongLong());
    if (id == 0)
        return;

    const auto endpoint = studio_.engine().find_endpoint(id);
    if (!endpoint)
    {
        Q_EMIT status_message(tr("The selected port no longer exists."), true);
        return;
    }
    if (!endpoint->is_open())
    {
        Q_EMIT status_message(
            tr("Port \"%1\" is not open.").arg(QString::fromStdString(
                endpoint->name())),
            true);
        return;
    }

    vss::byte_buffer payload;
    const QString text = editor_->toPlainText();

    if (hex_check_->isChecked())
    {
        const auto parsed = vss::from_hex(text.toStdString());
        if (!parsed)
        {
            Q_EMIT status_message(
                tr("Invalid hexadecimal input: use pairs such as \"48 65\"."),
                true);
            return;
        }
        payload = *parsed;
    }
    else
    {
        const QByteArray utf8 = text.toUtf8();
        payload.assign(reinterpret_cast<const vss::byte*>(utf8.constData()),
                       reinterpret_cast<const vss::byte*>(utf8.constData())
                           + utf8.size());
    }

    if (crlf_check_->isChecked())
    {
        payload.push_back(0x0d);
        payload.push_back(0x0a);
    }

    if (payload.empty())
    {
        Q_EMIT status_message(tr("Nothing to send."), false);
        return;
    }

    endpoint->write(vss::bytes_view(payload.data(), payload.size()));
    Q_EMIT status_message(
        tr("Queued %1 bytes for \"%2\".")
            .arg(format_count(payload.size()),
                 QString::fromStdString(endpoint->name())),
        false);
}

} // namespace vss::gui
