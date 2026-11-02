//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_SETTINGS_DIALOG_HPP
#define VSS_GUI_SETTINGS_DIALOG_HPP

#include <QDialog>
#include <QString>

class QCheckBox;
class QComboBox;
class QSpinBox;

namespace vss::gui {

/// Everything the interface remembers between runs.
struct ui_settings
{
    /// Advanced mode reveals the topology, fixtures and networking tools.
    bool advanced = false;
    /// How often the studio is polled, in milliseconds.
    int refresh_ms = 250;
    bool confirm_exit = true;
    /// "system", "light" or "dark".
    QString theme = QStringLiteral("system");
    int default_baud = 115200;
};

ui_settings load_ui_settings();
void save_ui_settings(const ui_settings& value);

/// Preferences with a sensible default and a restore button.
class SettingsDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(const ui_settings& current,
                            QWidget* parent = nullptr);

    ui_settings result() const { return result_; }

public Q_SLOTS:
    void accept() override;

private:
    void restore_defaults();

    ui_settings result_;
    QCheckBox* advanced_ = nullptr;
    QSpinBox* refresh_ = nullptr;
    QCheckBox* confirm_ = nullptr;
    QComboBox* theme_ = nullptr;
    QComboBox* baud_ = nullptr;
};

} // namespace vss::gui

#endif // VSS_GUI_SETTINGS_DIALOG_HPP
