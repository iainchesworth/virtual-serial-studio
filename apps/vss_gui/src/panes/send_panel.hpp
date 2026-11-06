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
    /// The selection moved: new sends follow it unless the target is pinned.
    /// A running repeat never follows; it stays on the port it started on.
    void set_selected_port(vss::endpoint_id id);
    void refresh_ports();
    vss::endpoint_id selected_port() const;

    /// An explicit request from a menu: make `id` the target of new sends
    /// (even when pinned) and put the cursor in the input.
    void send_from(vss::endpoint_id id);
    /// Starts a repeat from `id`, or stops it when it already runs from `id`.
    /// With nothing typed yet it asks for the bytes first.
    void toggle_repeat(vss::endpoint_id id);
    void focus_input();
    /// Stops a running repeat.
    void stop_repeat();
    /// The port a running repeat sends from, or 0.
    vss::endpoint_id repeat_port() const noexcept { return repeat_port_; }
    bool repeating() const noexcept { return repeat_port_ != 0; }
    bool pinned() const;
    void set_pinned(bool pinned);

    // Test hooks; the widgets remain private.
    void set_text(const QString& text) { input_->setText(text); }
    QString text() const;
    void send_now();
    /// "Sending from: modem (COM2)", or the hint when nothing is chosen.
    QString target_text() const;
    /// "Repeating every 1000 ms from modem (COM2)", or empty when idle.
    QString repeat_text() const;
    void set_repeat(bool on, int interval_ms);
    /// What the repeat timer does on each tick.
    void repeat_tick() { send_repeat(); }

Q_SIGNALS:
    void sent(int bytes, const QString& port_name);
    void status_message(const QString& message);
    void error_message(const QString& message);
    /// A repeat started (the port) or stopped (0).
    void repeat_changed(vss::endpoint_id id);

private:
    void build();
    void validate_hex();
    void update_send_enabled();
    void start_repeat();
    void send_repeat();
    void update_target();
    void update_repeat_status();
    QString port_label(vss::endpoint_id id) const;
    enum class send_outcome
    {
        sent,
        skipped,
        failed
    };
    /// Sends the typed bytes to `id`.
    send_outcome send_to(vss::endpoint_id id);
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
    QLabel* target_ = nullptr;
    QCheckBox* pin_ = nullptr;
    QWidget* repeat_row_ = nullptr;
    QLabel* repeat_status_ = nullptr;
    QPushButton* repeat_stop_ = nullptr;
    QTimer* repeat_timer_ = nullptr;
    /// The port a running repeat is bound to, fixed when it started.
    vss::endpoint_id repeat_port_ = 0;
    QString repeat_name_;
    bool advanced_ = false;
    bool refreshing_ = false;
    QStringList port_signature_;
};

} // namespace vss::gui

#endif // VSS_GUI_PANES_SEND_PANEL_HPP
