//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_WIDGETS_PICK_ROW_HPP
#define VSS_GUI_WIDGETS_PICK_ROW_HPP

#include <QFrame>

class QMouseEvent;
class QRadioButton;

namespace vss::gui
{

/// One row of a pick list: a radio button, some labels and a chip.
///
/// A click anywhere on an enabled row selects it, not only on the small radio
/// circle, and a double-click selects it and emits `activated()` so a dialog
/// can go to its next step.  Enter on the focused radio is left to the dialog's
/// default button.  A disabled row ignores the mouse.
class PickRow : public QFrame
{
    Q_OBJECT

public:
    explicit PickRow(QWidget* parent = nullptr);

    /// The radio button that holds the selection.  It must be a child of
    /// this row.
    void set_radio(QRadioButton* radio);
    QRadioButton* radio() const noexcept { return radio_; }

    /// Makes every label under the row pass the mouse to the row, so a click
    /// on the text selects it as well.  Call after the row is filled.
    void pass_clicks_through();

Q_SIGNALS:
    /// The row was double-clicked while enabled.
    void activated();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    bool selectable() const;
    void select();

    QRadioButton* radio_ = nullptr;
    bool pressed_ = false;
};

} // namespace vss::gui

#endif // VSS_GUI_WIDGETS_PICK_ROW_HPP
