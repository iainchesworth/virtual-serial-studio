//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_SHELL_ACCESSIBILITY_HPP
#define VSS_GUI_SHELL_ACCESSIBILITY_HPP

#include <QStringList>

class QWidget;

namespace vss::gui
{

class ActionRegistry;

/// The WP9 accessibility walk used by `vss_gui --selftest`.
///
/// It builds every shipped dialog against a fake adapter, walks the main
/// window and every dialog in both themes, and optionally writes the review
/// PNG set when `VSS_QA_SHOTS` names a directory.  It returns one sentence per
/// failure and never opens a modal loop.
QStringList run_accessibility_checks(ActionRegistry& registry,
                                     QWidget* main_window);

} // namespace vss::gui

#endif // VSS_GUI_SHELL_ACCESSIBILITY_HPP
