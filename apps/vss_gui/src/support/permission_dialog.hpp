//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_SUPPORT_PERMISSION_DIALOG_HPP
#define VSS_GUI_SUPPORT_PERMISSION_DIALOG_HPP

#include <vss/ports/permissions.hpp>

#include <QDialog>

class QPushButton;

namespace vss::gui
{
class NoteBanner;
}

namespace vss::gui::support
{

/// Explains one permission fact that is not Windows Firewall (that has
/// `FirewallDialog`): a serial device the account may not open, a confinement
/// such as AppArmor, SELinux, Flatpak or Snap, or a Linux firewall.  It shows
/// the exact command or profile text to copy, never runs it, and, for the one
/// step Studio can take (the serial group, through polkit), offers a button
/// that says what will happen and that the desktop will ask for a password
/// first.
class PermissionDialog final : public QDialog
{
    Q_OBJECT

public:
    PermissionDialog(const vss::permissions::fact& fact, bool step_possible,
                     QWidget* parent = nullptr);

    bool step_done() const noexcept { return done_; }
    QPushButton* step_button() const noexcept { return step_; }
    NoteBanner* result_note() const noexcept { return result_; }

    /// Presses the step button.  Public so a test can reach it; it does nothing
    /// unless the build offers the step.
    void run_step();

private:
    void build();

    vss::permissions::fact fact_;
    bool can_run_ = false;
    QPushButton* step_ = nullptr;
    NoteBanner* result_ = nullptr;
    bool done_ = false;
};

} // namespace vss::gui::support

#endif // VSS_GUI_SUPPORT_PERMISSION_DIALOG_HPP
