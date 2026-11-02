//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "settings_dialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

namespace vss::gui {
namespace {

const QStringList baud_rates = {QStringLiteral("9600"),
                                QStringLiteral("19200"),
                                QStringLiteral("38400"),
                                QStringLiteral("57600"),
                                QStringLiteral("115200"),
                                QStringLiteral("230400"),
                                QStringLiteral("460800"),
                                QStringLiteral("921600")};

} // namespace

ui_settings load_ui_settings()
{
    QSettings settings;
    ui_settings value;
    value.advanced =
        settings.value(QStringLiteral("ui/advanced"), false).toBool();
    value.refresh_ms =
        settings.value(QStringLiteral("ui/refresh_ms"), 250).toInt();
    value.confirm_exit =
        settings.value(QStringLiteral("ui/confirm_exit"), true).toBool();
    value.theme =
        settings.value(QStringLiteral("ui/theme"),
                       QStringLiteral("system")).toString();
    value.default_baud =
        settings.value(QStringLiteral("ui/default_baud"), 115200).toInt();
    return value;
}

void save_ui_settings(const ui_settings& value)
{
    QSettings settings;
    settings.setValue(QStringLiteral("ui/advanced"), value.advanced);
    settings.setValue(QStringLiteral("ui/refresh_ms"), value.refresh_ms);
    settings.setValue(QStringLiteral("ui/confirm_exit"), value.confirm_exit);
    settings.setValue(QStringLiteral("ui/theme"), value.theme);
    settings.setValue(QStringLiteral("ui/default_baud"), value.default_baud);
}

SettingsDialog::SettingsDialog(const ui_settings& current, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Settings"));
    setModal(true);
    result_ = current;

    advanced_ = new QCheckBox(
        tr("Show advanced tools (fixtures, topology, networking)"), this);
    advanced_->setChecked(current.advanced);
    advanced_->setToolTip(
        tr("Advanced mode adds the mock device, null-modem demo and the "
           "share/join/bridge tools to the menus."));

    refresh_ = new QSpinBox(this);
    refresh_->setRange(50, 5000);
    refresh_->setSingleStep(50);
    refresh_->setSuffix(tr(" ms"));
    refresh_->setValue(current.refresh_ms);
    refresh_->setToolTip(tr("How often the window reads counters and state."));

    confirm_ = new QCheckBox(tr("Ask before discarding an unsaved topology"),
                             this);
    confirm_->setChecked(current.confirm_exit);

    theme_ = new QComboBox(this);
    theme_->addItem(tr("Follow the desktop"), QStringLiteral("system"));
    theme_->addItem(tr("Light"), QStringLiteral("light"));
    theme_->addItem(tr("Dark"), QStringLiteral("dark"));
    const int theme_index = theme_->findData(current.theme);
    theme_->setCurrentIndex(theme_index >= 0 ? theme_index : 0);

    baud_ = new QComboBox(this);
    baud_->setEditable(true);
    baud_->addItems(baud_rates);
    baud_->setCurrentText(QString::number(current.default_baud));
    baud_->setToolTip(tr("Initial baud rate offered by the port dialogs."));

    auto* form = new QFormLayout;
    form->addRow(tr("Refresh interval:"), refresh_);
    form->addRow(tr("Theme:"), theme_);
    form->addRow(tr("Default baud rate:"), baud_);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(advanced_);
    layout->addLayout(form);
    layout->addWidget(confirm_);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto* defaults =
        buttons->addButton(tr("Restore defaults"), QDialogButtonBox::ResetRole);
    connect(defaults, &QPushButton::clicked, this,
            &SettingsDialog::restore_defaults);
    connect(buttons, &QDialogButtonBox::accepted, this,
            &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void SettingsDialog::restore_defaults()
{
    const ui_settings defaults;
    advanced_->setChecked(defaults.advanced);
    refresh_->setValue(defaults.refresh_ms);
    confirm_->setChecked(defaults.confirm_exit);
    theme_->setCurrentIndex(theme_->findData(defaults.theme));
    baud_->setCurrentText(QString::number(defaults.default_baud));
}

void SettingsDialog::accept()
{
    result_.advanced = advanced_->isChecked();
    result_.refresh_ms = refresh_->value();
    result_.confirm_exit = confirm_->isChecked();
    result_.theme = theme_->currentData().toString();
    bool ok = false;
    const int baud = baud_->currentText().toInt(&ok);
    if (ok && baud > 0)
        result_.default_baud = baud;

    save_ui_settings(result_);
    QDialog::accept();
}

} // namespace vss::gui
