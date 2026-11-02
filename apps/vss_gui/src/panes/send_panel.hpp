//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_PANES_SEND_PANEL_HPP
#define VSS_GUI_PANES_SEND_PANEL_HPP

#include "backend/studio_backend.hpp"

#include <QLineEdit>
#include <QStringList>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTimer;

namespace vss::gui
{

/// The Send dock: text or hex, CRLF, and the Advanced repeat/saved sends.
class SendPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit SendPanel(StudioBackend& backend, QWidget* parent = nullptr);

    void set_advanced(bool advanced);
    void set_selected_port(vss::endpoint_id id);
    void refresh_ports();
    vss::endpoint_id selected_port() const;

    // Test hooks; the widgets remain private.
    void set_text(const QString& text) { input_->setText(text); }
    QString text() const;
    void send_now();

Q_SIGNALS:
    void sent(int bytes, const QString& port_name);
    void status_message(const QString& message);
    void error_message(const QString& message);

private:
    void build();
    void validate_hex();
    void update_send_enabled();
    void start_repeat();
    void stop_repeat();
    void remember_send(const QString& text);
    void load_saved_sends();
    QByteArray payload(bool& ok) const;

    StudioBackend& backend_;
    QComboBox* ports_ = nullptr;
    QPushButton* send_ = nullptr;
    QLineEdit* input_ = nullptr;
    QCheckBox* hex_ = nullptr;
    QCheckBox* crlf_ = nullptr;
    QCheckBox* repeat_ = nullptr;
    QSpinBox* interval_ = nullptr;
    QComboBox* saved_ = nullptr;
    QLabel* saved_label_ = nullptr;
    QLabel* error_ = nullptr;
    QTimer* repeat_timer_ = nullptr;
    bool advanced_ = false;
    bool refreshing_ = false;
    QStringList port_signature_;
};

} // namespace vss::gui

#endif // VSS_GUI_PANES_SEND_PANEL_HPP
