//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "panes/send_panel.hpp"

#include "theme/theme.hpp"
#include "widgets/common.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>

namespace vss::gui
{
namespace
{

bool parse_hex(const QString& text, QByteArray& out)
{
    QString digits;
    for (const QChar ch : text)
    {
        if (ch.isSpace())
            continue;
        if (!ch.isDigit() && (ch.toLower() < QLatin1Char('a') ||
                              ch.toLower() > QLatin1Char('f')))
            return false;
        digits.append(ch);
    }
    if (digits.size() % 2 != 0)
        return false;
    for (qsizetype index = 0; index < digits.size(); index += 2)
    {
        bool ok = false;
        const int value = digits.mid(index, 2).toInt(&ok, 16);
        if (!ok)
            return false;
        out.append(static_cast<char>(value));
    }
    return true;
}

} // namespace

SendPanel::SendPanel(StudioBackend& backend, QWidget* parent)
    : QWidget(parent), backend_(backend)
{
    build();
    connect(&backend_, &StudioBackend::ports_changed, this,
            [this] { refresh_ports(); });
    refresh_ports();
    load_saved_sends();
    validate_hex();
}

void SendPanel::build()
{
    setAccessibleName(tr("Send"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto* top = new QHBoxLayout();
    top->setSpacing(8);
    ports_ = new QComboBox(this);
    ports_->setAccessibleName(tr("Send to port"));
    ports_->setSizeAdjustPolicy(
        QComboBox::AdjustToMinimumContentsLengthWithIcon);
    ports_->setMinimumContentsLength(8);
    top->addWidget(ports_, 1);
    send_ = new QPushButton(tr("Send"), this);
    send_->setProperty("role", QStringLiteral("primary"));
    polish(send_);
    top->addWidget(send_);
    layout->addLayout(top);

    input_ = new QLineEdit(this);
    input_->setAccessibleName(tr("Bytes to send"));
    input_->setPlaceholderText(tr("Type text, or hex if Hex is ticked"));
    input_->setFont(Theme::instance().font(text_style::data));
    layout->addWidget(input_);

    error_ = new QLabel(this);
    error_->setFont(Theme::instance().font(text_style::caption));
    error_->setWordWrap(true);
    error_->setStyleSheet(QStringLiteral("color: %1;")
                              .arg(Theme::instance()
                                       .colour(colour_role::bad)
                                       .name(QColor::HexRgb)));
    error_->setVisible(false);
    layout->addWidget(error_);

    auto* options = new QHBoxLayout();
    options->setSpacing(16);
    hex_ = new QCheckBox(tr("Hex"), this);
    crlf_ = new QCheckBox(tr("Add CRLF"), this);
    crlf_->setChecked(true);
    options->addWidget(hex_);
    options->addWidget(crlf_);
    repeat_ = new QCheckBox(tr("Repeat"), this);
    interval_ = new QSpinBox(this);
    interval_->setRange(50, 5000);
    interval_->setValue(1000);
    interval_->setSuffix(tr(" ms"));
    interval_->setAccessibleName(tr("Repeat interval"));
    interval_->setVisible(false);
    options->addWidget(repeat_);
    options->addWidget(interval_);
    options->addStretch(1);
    layout->addLayout(options);

    auto* saved_row = new QHBoxLayout();
    saved_row->setSpacing(8);
    saved_label_ = new QLabel(tr("Saved sends"), this);
    saved_label_->setFont(Theme::instance().font(text_style::caption));
    saved_label_->setProperty("muted", true);
    polish(saved_label_);
    saved_ = new QComboBox(this);
    saved_->setAccessibleName(tr("Saved sends"));
    saved_row->addWidget(saved_label_);
    saved_row->addWidget(saved_, 1);
    layout->addLayout(saved_row);
    layout->addStretch(1);

    repeat_timer_ = new QTimer(this);
    repeat_timer_->setInterval(interval_->value());
    connect(repeat_timer_, &QTimer::timeout, this, [this] { send_now(); });

    connect(send_, &QPushButton::clicked, this, [this] { send_now(); });
    connect(input_, &QLineEdit::textChanged, this,
            [this]
            {
                validate_hex();
                update_send_enabled();
            });
    connect(input_, &QLineEdit::returnPressed, this, [this] { send_now(); });
    connect(hex_, &QCheckBox::toggled, this,
            [this]
            {
                validate_hex();
                update_send_enabled();
            });
    connect(repeat_, &QCheckBox::toggled, this,
            [this](bool on)
            {
                interval_->setVisible(advanced_ && on);
                if (on)
                    start_repeat();
                else
                    stop_repeat();
            });
    connect(interval_, &QSpinBox::valueChanged, this,
            [this](int value) { repeat_timer_->setInterval(value); });
    connect(saved_, qOverload<int>(&QComboBox::activated), this,
            [this](int index)
            {
                if (index >= 0)
                    input_->setText(saved_->itemData(index).toString());
            });
    set_advanced(false);
}

void SendPanel::set_advanced(bool advanced)
{
    advanced_ = advanced;
    repeat_->setVisible(advanced);
    interval_->setVisible(advanced && repeat_->isChecked());
    saved_label_->setVisible(advanced);
    saved_->setVisible(advanced);
    if (!advanced)
    {
        repeat_->setChecked(false);
        stop_repeat();
    }
}

void SendPanel::refresh_ports()
{
    QStringList signature;
    for (const auto& port : backend_.ports())
        signature << QStringLiteral("%1|%2|%3")
                         .arg(static_cast<qulonglong>(port.id))
                         .arg(port.name, port.path);
    if (signature == port_signature_)
    {
        update_send_enabled();
        return;
    }
    port_signature_ = signature;

    const auto previous = selected_port();
    refreshing_ = true;
    ports_->clear();
    for (const auto& port : backend_.ports())
    {
        ports_->addItem(port.path.isEmpty()
                            ? port.name
                            : tr("%1 (%2)").arg(port.name, port.path),
                        QVariant::fromValue<qulonglong>(port.id));
    }
    if (previous != 0)
    {
        const int index =
            ports_->findData(QVariant::fromValue<qulonglong>(previous));
        if (index >= 0)
            ports_->setCurrentIndex(index);
    }
    refreshing_ = false;
    update_send_enabled();
}

vss::endpoint_id SendPanel::selected_port() const
{
    if (ports_ == nullptr || ports_->currentIndex() < 0)
        return 0;
    return static_cast<vss::endpoint_id>(ports_->currentData().toULongLong());
}

void SendPanel::set_selected_port(vss::endpoint_id id)
{
    const int index = ports_->findData(QVariant::fromValue<qulonglong>(id));
    if (index >= 0)
    {
        const QSignalBlocker blocker(ports_);
        ports_->setCurrentIndex(index);
    }
}

QString SendPanel::text() const
{
    return input_->text();
}

void SendPanel::validate_hex()
{
    if (!hex_->isChecked())
    {
        error_->setVisible(false);
        input_->setProperty("invalid", false);
        polish(input_);
        return;
    }
    QByteArray bytes;
    if (input_->text().trimmed().isEmpty() || parse_hex(input_->text(), bytes))
    {
        error_->setVisible(false);
        input_->setProperty("invalid", false);
        polish(input_);
        return;
    }
    error_->setText(tr("Hex needs pairs of digits, such as 48 65 6C 6C 6F."));
    error_->setVisible(true);
    input_->setProperty("invalid", true);
    polish(input_);
}

void SendPanel::update_send_enabled()
{
    const bool has_port = selected_port() != 0;
    const bool has_text = !input_->text().isEmpty();
    send_->setEnabled(has_port && has_text && error_->isHidden());
    send_->setToolTip(has_port ? QString() : tr("Select a port to send to."));
}

QByteArray SendPanel::payload(bool& ok) const
{
    QByteArray bytes;
    if (hex_->isChecked())
    {
        ok = parse_hex(input_->text(), bytes);
    }
    else
    {
        bytes = input_->text().toUtf8();
        ok = true;
    }
    if (crlf_->isChecked())
        bytes.append("\r\n");
    return bytes;
}

void SendPanel::send_now()
{
    if (refreshing_)
        return;
    const vss::endpoint_id id = selected_port();
    if (id == 0)
        return;
    bool ok = false;
    const QByteArray bytes = payload(ok);
    if (!ok)
    {
        validate_hex();
        return;
    }
    if (bytes.isEmpty())
        return;
    const auto sent_result = backend_.send(id, bytes);
    if (!sent_result)
    {
        Q_EMIT error_message(error_text(sent_result.error()));
        return;
    }
    const QString name = ports_->currentText();
    remember_send(input_->text());
    Q_EMIT sent(static_cast<int>(bytes.size()), name);
    Q_EMIT status_message(
        tr("Queued %n bytes to %1.", "", static_cast<int>(bytes.size()))
            .arg(name));
}

void SendPanel::start_repeat()
{
    if (repeat_->isChecked())
        repeat_timer_->start(interval_->value());
}

void SendPanel::stop_repeat()
{
    repeat_timer_->stop();
}

void SendPanel::remember_send(const QString& text)
{
    if (text.isEmpty())
        return;
    QSettings settings;
    QStringList saved =
        settings.value(QStringLiteral("ui/savedSends")).toStringList();
    if (!saved.isEmpty() && saved.first() == text)
        return;
    saved.removeAll(text);
    saved.prepend(text);
    while (saved.size() > 12)
        saved.removeLast();
    settings.setValue(QStringLiteral("ui/savedSends"), saved);
    load_saved_sends();
}

void SendPanel::load_saved_sends()
{
    const QSignalBlocker blocker(saved_);
    saved_->clear();
    const QStringList saved =
        QSettings().value(QStringLiteral("ui/savedSends")).toStringList();
    for (const QString& text : saved)
    {
        const QString label =
            text.size() > 48 ? text.left(45) + QStringLiteral("...") : text;
        saved_->addItem(label, text);
    }
    saved_->setEnabled(!saved.isEmpty());
}

} // namespace vss::gui
