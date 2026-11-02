//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_WIDGETS_GALLERY_HPP
#define VSS_GUI_WIDGETS_GALLERY_HPP

#include "theme/theme.hpp"
#include "widgets/controls.hpp"

#include <QDialog>

namespace vss::gui
{

/// Developer gallery: every widget, every state, both themes.
///
/// Opened with `VSS_WIDGET_GALLERY=1`; the offscreen test instantiates it and
/// writes one PNG per theme for human review.
class WidgetGallery final : public QDialog
{
    Q_OBJECT

public:
    explicit WidgetGallery(QWidget* parent = nullptr);

    void set_theme(theme_mode mode);
    void set_density(density_mode density);

private:
    void build();
    void add_status_section();
    void add_controls_section();
    void add_fields_section();
    void add_feedback_section();
    void add_dialogs_section();

    SegmentedControl* theme_segment_ = nullptr;
    SegmentedControl* density_segment_ = nullptr;
    QWidget* content_ = nullptr;
    QVBoxLayout* content_layout_ = nullptr;
};

} // namespace vss::gui

#endif // VSS_GUI_WIDGETS_GALLERY_HPP
