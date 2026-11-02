//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_FLOWS_FLOW_DIALOG_HPP
#define VSS_GUI_FLOWS_FLOW_DIALOG_HPP

#include <QDialog>
#include <QString>

class QHBoxLayout;
class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

namespace vss::gui
{

/// Shared chrome for the WP5 dialogs: title, one-sentence subtitle, a
/// scrollable body, and a sunken footer whose primary action is rightmost.
///
/// The body is a scroll area so 200 percent text never clips.  Escape is the
/// standard QDialog reject; callers set the primary button as the default.
class FlowDialog : public QDialog
{
    Q_OBJECT

public:
    FlowDialog(const QString& title, const QString& subtitle,
               QWidget* parent = nullptr);

    QVBoxLayout* body_layout() const noexcept { return body_layout_; }
    QWidget* body() const noexcept { return body_; }

    QPushButton* add_footer_button(const QString& text,
                                   const QString& role = {});
    QPushButton* primary_button() const noexcept { return primary_; }
    void set_primary_text(const QString& text);
    void set_primary_enabled(bool enabled);
    void set_subtitle_text(const QString& text);
    void set_title_text(const QString& text);
    void focus_first(QWidget* widget);
    void finish_layout(int preferred_height = 720);

private:
    void apply_theme();

    QLabel* title_ = nullptr;
    QLabel* subtitle_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    QWidget* body_ = nullptr;
    QVBoxLayout* body_layout_ = nullptr;
    QHBoxLayout* footer_layout_ = nullptr;
    QPushButton* primary_ = nullptr;
};

} // namespace vss::gui

#endif // VSS_GUI_FLOWS_FLOW_DIALOG_HPP
