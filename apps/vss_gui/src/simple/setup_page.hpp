//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#pragma once

#include "backend/studio_backend.hpp"
#include "simple/job_form.hpp"
#include "simple/setup_kinds.hpp"

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QVector>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QVBoxLayout;

namespace vss::gui
{
class Switch;
}

namespace vss::gui::simple
{

/// The page of one setup: what to do now, its ports, typing to it and the
/// data going through.
class SetupPage final : public QWidget
{
    Q_OBJECT

public:
    SetupPage(StudioBackend& backend, const setup_view& setup,
              const setup_prefs& prefs, QWidget* parent = nullptr);

    const setup_view& setup() const noexcept { return setup_; }
    const setup_prefs& prefs() const noexcept { return prefs_; }

    /// The setup changed in the engine (a port opened, a program connected).
    void refresh(const setup_view& setup);
    /// Traffic from the engine, on the GUI thread.
    void add_events(const event_batch& events);
    void set_show_data(bool on);

    /// Where typed text goes, or 0 when the page has no typing box.
    vss::endpoint_id type_target() const noexcept { return type_target_; }
    /// Sends `text` as typed, with the line ending when it is on.
    bool send_typed(const QString& text);

    QString steps_text() const;
    QString received_text() const;
    QString data_text() const;
    QStringList port_lines() const;

Q_SIGNALS:
    void change_requested();
    void remove_requested();
    void advanced_requested();
    void troubleshoot_requested();
    void add_program_requested();
    void close_port_requested(vss::endpoint_id port);
    void prefs_changed(const vss::gui::simple::setup_prefs& prefs);
    void message(const QString& text);

private:
    struct data_line
    {
        QDateTime when;
        QString from;
        QByteArray bytes;
        bool sent = false;
    };

    void build();
    void rebuild_top();
    QWidget* program_row(const port_view& port, QWidget* parent);
    QWidget* device_row(const port_view& port, QWidget* parent);
    QWidget* listener_row(const port_view& port, QWidget* parent);
    QString shown(vss::endpoint_id id) const;
    QString render(const QByteArray& bytes) const;
    void render_data();
    void render_received();
    bool can_add_program() const;

    StudioBackend& backend_;
    setup_view setup_;
    setup_prefs prefs_;
    vss::endpoint_id type_target_ = 0;

    QWidget* top_ = nullptr;
    QVBoxLayout* top_layout_ = nullptr;
    QLabel* steps_ = nullptr;
    QStringList port_lines_;

    QWidget* type_box_ = nullptr;
    QLabel* type_heading_ = nullptr;
    QLabel* type_sentence_ = nullptr;
    QPlainTextEdit* received_ = nullptr;
    QLineEdit* input_ = nullptr;
    Switch* line_ending_ = nullptr;
    QVector<data_line> conversation_;

    QPushButton* show_data_ = nullptr;
    QWidget* data_box_ = nullptr;
    QPlainTextEdit* data_ = nullptr;
    Switch* hex_ = nullptr;
    QPushButton* pause_ = nullptr;
    bool paused_ = false;
    QVector<data_line> lines_;
};

} // namespace vss::gui::simple
