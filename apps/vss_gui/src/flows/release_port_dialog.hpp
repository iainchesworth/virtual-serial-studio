//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#pragma once

#include "backend/studio_backend.hpp"
#include "flows/flow_dialog.hpp"

#include <QString>

#include <chrono>
#include <optional>

namespace vss::gui
{

class NoteBanner;
class PickCard;

/// "Release port" (Advanced): makes the program that has a driver port open
/// let go of it.
///
/// The dialog names the program and says how long no data has passed on the
/// port, as a fact; it never says the program is stuck, because Studio cannot
/// tell a stuck program from a quiet one.  Two ways are offered, each
/// confirmed by the one button: take the port back (the program keeps running
/// but its handle stops working) and end the program.  Studio ends only the
/// user's own programs unless it was started as administrator, and says so.
class ReleasePortDialog final : public FlowDialog
{
    Q_OBJECT

public:
    ReleasePortDialog(StudioBackend& backend, vss::endpoint_id port,
                      QWidget* parent = nullptr);

    PickCard* take_back_card() const noexcept { return take_back_; }
    PickCard* end_program_card() const noexcept { return end_program_; }
    NoteBanner* holder_note() const noexcept { return holder_; }
    NoteBanner* idle_note() const noexcept { return idle_; }
    /// The way chosen now.
    release_method method() const;
    /// Whether the dialog released the port.
    bool released() const noexcept { return released_; }

    /// "5 seconds", "2 minutes" and so on, for a time without traffic.
    static QString idle_text(std::chrono::seconds idle);

Q_SIGNALS:
    void error_shown(const QString& title);

private:
    void release();
    void update_state();
    QString program_text() const;

    StudioBackend& backend_;
    vss::endpoint_id port_ = 0;
    QString port_name_;
    std::optional<holder_view> holder_view_;
    NoteBanner* holder_ = nullptr;
    NoteBanner* idle_ = nullptr;
    PickCard* take_back_ = nullptr;
    PickCard* end_program_ = nullptr;
    bool released_ = false;
};

} // namespace vss::gui

