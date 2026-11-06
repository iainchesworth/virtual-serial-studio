//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#pragma once

#include <vss/ports/permissions.hpp>

#include <QDialog>

class QPushButton;

namespace vss::gui
{
class NoteBanner;
}

namespace vss::gui::support
{

/// Explains what Windows Firewall does to a listener and, when Studio can,
/// offers to add the one rule that fixes it.  The dialog is the user's
/// agreement: the rule is shown exactly (program, protocol, port, who, which
/// networks) with the command an administrator could run instead, and the
/// button says that Windows will ask for permission.  Nothing is changed until
/// it is pressed.
class FirewallDialog final : public QDialog
{
    Q_OBJECT

public:
    /// `fact` is the `windows_firewall` fact for the listener.  `step_possible`
    /// is whether this build can make the rule (a capability the caller reads);
    /// when false the dialog only explains and shows the command.
    FirewallDialog(const vss::permissions::fact& fact, bool step_possible,
                   QWidget* parent = nullptr);

    /// True when a rule was added while the dialog was open.
    bool rule_added() const noexcept { return added_; }

    QPushButton* allow_button() const noexcept { return allow_; }
    NoteBanner* result_note() const noexcept { return result_; }

    /// Presses "Allow": runs the step.  Public so tests can reach it without
    /// the operating system's prompt; they inject nothing, so they only call it
    /// when the step is not available.
    void allow();

Q_SIGNALS:
    void rule_added_signal();

private:
    void build(bool step_possible);

    vss::permissions::fact fact_;
    QPushButton* allow_ = nullptr;
    NoteBanner* result_ = nullptr;
    bool added_ = false;
    bool can_run_ = false;
};

} // namespace vss::gui::support
