//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_SUPPORT_HELP_CONTENTS_DIALOG_HPP
#define VSS_GUI_SUPPORT_HELP_CONTENTS_DIALOG_HPP

#include <QDialog>

namespace vss::gui::support
{

enum class help_topic
{
    none,
    setup_check,
    troubleshoot,
    shortcuts,
    about
};

/// Help contents: a short index of the support screens, because the design
/// bundle specifies the Help menu item but no help browser.  The choice is
/// returned after the dialog closes so the shell can open the next dialog
/// without nesting modal loops.
class HelpContentsDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit HelpContentsDialog(QWidget* parent = nullptr);

    help_topic requested() const noexcept { return requested_; }

private:
    void build();
    void choose(help_topic topic);

    help_topic requested_ = help_topic::none;
};

} // namespace vss::gui::support

#endif // VSS_GUI_SUPPORT_HELP_CONTENTS_DIALOG_HPP
