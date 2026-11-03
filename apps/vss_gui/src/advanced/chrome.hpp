//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_ADVANCED_CHROME_HPP
#define VSS_GUI_ADVANCED_CHROME_HPP

#include "theme/theme.hpp"

#include <QString>
#include <QVector>

class QFrame;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QWidget;

namespace vss::gui
{

/// The exact tooltip every control this build cannot drive must carry.
///
/// The design requires degraded controls to say the same sentence so a user
/// can tell "this build cannot do it" from "this action is blocked right now".
QString not_available_text();

/// Disables a control and gives it the degraded tooltip.
///
/// `reason` defaults to #not_available_text.  The same text becomes the
/// accessible description so a screen reader hears why the control is off.
void degrade(QWidget* widget, const QString& reason = {});

/// Paints a label with the `ink2` role, which the shared QSS has no property
/// for.  Colours still come from Theme.
void set_ink2(QWidget* widget);

/// A dialog-width heading: title, ADVANCED badge and optional chips, then the
/// subtitle sentence.
QWidget* make_header(const QString& title, const QString& subtitle,
                     QWidget* parent, bool advanced_badge = true,
                     const QVector<QWidget*>& chips = {});

/// A plain push button with the shared roles: primary, quiet, danger or none.
QPushButton* make_button(const QString& text, const char* role,
                         QWidget* parent);

/// The sunken footer strip used by every advanced dialog.  The caller adds
/// buttons to `layout`; the stretch is already there.
QFrame* make_footer(QWidget* parent, QHBoxLayout*& layout);

/// A section heading at the Requested style with the design's weight.
QLabel* make_heading(const QString& text, text_style style, QWidget* parent);

} // namespace vss::gui

#endif // VSS_GUI_ADVANCED_CHROME_HPP
