//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_THEME_FOCUS_RING_STYLE_HPP
#define VSS_GUI_THEME_FOCUS_RING_STYLE_HPP

#include <QProxyStyle>

class QEvent;
class QStyle;

namespace vss::gui
{

/// Fusion plus the keyboard-only focus ring the design specifies.
///
/// QSS cannot distinguish keyboard focus from a mouse click, so the ring is
/// painted here for `PE_FrameFocusRect` only when the last focus change came
/// from Tab, Backtab or a shortcut.
class FocusRingStyle final : public QProxyStyle
{
public:
    explicit FocusRingStyle(QStyle* base = nullptr);

    void install();
    /// True when the last focus change came from the keyboard.
    static bool keyboard_focus();
    bool eventFilter(QObject* watched, QEvent* event) override;
    void drawPrimitive(PrimitiveElement element, const QStyleOption* option,
                       QPainter* painter,
                       const QWidget* widget = nullptr) const override;

private:
    static Qt::FocusReason last_reason_;
    bool installed_ = false;
};

} // namespace vss::gui

#endif // VSS_GUI_THEME_FOCUS_RING_STYLE_HPP
