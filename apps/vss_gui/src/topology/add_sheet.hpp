//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_TOPOLOGY_ADD_SHEET_HPP
#define VSS_GUI_TOPOLOGY_ADD_SHEET_HPP

#include "backend/canvas_edit.hpp"

#include <QDialog>
#include <QString>
#include <QStringList>
#include <QVector>

#include <vector>

class QComboBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QToolButton;
class QWidget;

namespace vss::gui::topology
{

/// The label of a bundle's slot: "End A", "Source", "Target 2", ...
QString slot_label(canvas_item item, int index, int slots);

/// The Add sheet: what a toolbox tile will add, before it is added.
///
/// One row per slot with its kind (New port, New fixture, Loopback or a port
/// already on the canvas) and a name; "How many" for the bundles with a count;
/// "More settings" for the serial settings of the new ports.  Enter adds, Esc
/// cancels.  The sheet builds a `bundle_request` and does not call the
/// backend.
class AddSheet final : public QDialog
{
    Q_OBJECT

public:
    AddSheet(canvas_item item, const std::vector<port_view>& ports,
             QWidget* parent = nullptr);

    bundle_request request() const;
    int count() const;
    void set_count(int count);
    /// Sets slot `index` to an existing port, or back to a new one.
    void set_slot_kind(int index, slot_kind kind, const QString& existing = {});
    void set_slot_name(int index, const QString& name);
    int slot_rows() const;
    QString slot_row_label(int index) const;
    QPushButton* add_button() const noexcept { return add_; }
    /// Shows the reason the last Add failed, under the sheet.
    void show_error(const QString& text);
    QString error_text() const;

private:
    struct row
    {
        QLabel* label = nullptr;
        QComboBox* kind = nullptr;
        QLineEdit* name = nullptr;
    };

    void rebuild_rows(int slots);
    void update_state();
    void sync_name_visibility(int index);
    void rename_default(int index);

    canvas_item item_;
    std::vector<port_view> ports_;
    QVector<row> rows_;
    QGridLayout* grid_ = nullptr;
    QWidget* rows_host_ = nullptr;
    QSpinBox* count_ = nullptr;
    QToolButton* more_ = nullptr;
    QWidget* settings_ = nullptr;
    QComboBox* baud_ = nullptr;
    QComboBox* framing_ = nullptr;
    QLabel* error_ = nullptr;
    QPushButton* add_ = nullptr;
};

} // namespace vss::gui::topology

#endif // VSS_GUI_TOPOLOGY_ADD_SHEET_HPP
