//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_ADVANCED_LINE_MATRIX_PANEL_HPP
#define VSS_GUI_ADVANCED_LINE_MATRIX_PANEL_HPP

#include "advanced/line_presets.hpp"
#include "backend/studio_backend.hpp"

#include <QString>
#include <QVector>
#include <QWidget>

#include <optional>

class QCheckBox;
class QLabel;
class QPushButton;
class QTableWidget;

namespace vss::gui
{

class CodeBlock;
class NoteBanner;
class SegmentedControl;
class StatusChip;
class Switch;

/// The left and right halves of the line matrix and fault dialog.
///
/// Wiring is read-only and shows the Standard null-modem preset unless
/// `Capabilities::line_matrix` is true, in which case edits go through
/// `StudioBackend::set_link_wiring`.  The real adapter reports the capability
/// false and implements neither side, so nothing here is ever simulated.
class LineMatrixPanel final : public QWidget
{
    Q_OBJECT

public:
    LineMatrixPanel(StudioBackend& backend, vss::endpoint_id endpoint,
                    QWidget* parent = nullptr);

    void set_endpoint(vss::endpoint_id endpoint);
    vss::endpoint_id endpoint() const noexcept { return endpoint_; }
    void refresh();

    /// Row 0 is DTR, row 1 is RTS.  Column 0 is DSR, then CTS, DCD and RI.
    QCheckBox* matrix_box(int row, int column) const;
    QCheckBox* box_named(const QString& name) const;
    SegmentedControl* preset_control() const noexcept { return preset_; }
    Switch* emulate_switch() const noexcept { return emulate_; }
    QPushButton* break_button() const noexcept { return break_; }
    QPushButton* inject_button(int index) const;
    int inject_count() const noexcept;
    QString wiring_text() const;
    QString caption_text() const;
    QString other_port_name() const { return other_name_; }
    QTableWidget* history_table() const noexcept { return history_; }

Q_SIGNALS:
    void message(const QString& text);

private:
    void build();
    QWidget* build_wiring_column();
    QWidget* build_fault_column();
    void apply_preset(wiring_preset preset);
    void write_boxes(const line_wiring& wiring);
    line_wiring current_wiring() const;
    void on_box_toggled();
    void on_preset_changed(int id);
    void apply_wiring(const line_wiring& wiring);
    void update_line_state();
    void update_caption();
    void set_line(vss::control_line line, bool state);
    void inject_fault(std::size_t index);
    QString port_name(vss::endpoint_id id) const;

    StudioBackend& backend_;
    vss::endpoint_id endpoint_ = 0;
    std::optional<link_view> link_;
    QString other_name_;

    SegmentedControl* preset_ = nullptr;
    Switch* emulate_ = nullptr;
    QCheckBox* boxes_[2][4] = {};
    QLabel* caption_ = nullptr;
    NoteBanner* sentence_ = nullptr;

    Switch* dtr_ = nullptr;
    Switch* rts_ = nullptr;
    QPushButton* break_ = nullptr;
    StatusChip* inputs_[4] = {};
    bool updating_ = false;

    QTableWidget* history_ = nullptr;
    CodeBlock* code_ = nullptr;
    QVector<QPushButton*> inject_;
};

} // namespace vss::gui

#endif // VSS_GUI_ADVANCED_LINE_MATRIX_PANEL_HPP
