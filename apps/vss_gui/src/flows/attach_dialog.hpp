//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_FLOWS_ATTACH_DIALOG_HPP
#define VSS_GUI_FLOWS_ATTACH_DIALOG_HPP

#include "backend/studio_backend.hpp"
#include "flows/common.hpp"
#include "flows/flow_dialog.hpp"

#include <QVector>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QVBoxLayout;

namespace vss::gui
{

class CodeBlock;
class Disclosure;
class FormRow;
class NoteBanner;
class PickRow;
class ResultSentence;

/// "Attach a real device": enumerate, refresh, show unavailable reasons,
/// optionally edit the address, then open the device once and create the
/// virtual port the user's programs open.  A click anywhere on a device row
/// selects it, and a double-click moves on to the Name field.
class AttachDialog final : public FlowDialog
{
    Q_OBJECT

public:
    AttachDialog(StudioBackend& backend, bool advanced,
                 platform_kind platform = host_platform(),
                 QWidget* parent = nullptr);

    QLineEdit* name_edit() const noexcept { return name_; }
    QLineEdit* manual_path_edit() const noexcept { return manual_path_; }
    QLineEdit* address_edit() const noexcept { return address_edit_; }
    QPushButton* refresh_button() const noexcept { return refresh_; }
    QCheckBox* capture_check() const noexcept { return capture_; }
    QComboBox* baud_combo() const noexcept { return baud_; }
    ResultSentence* result_sentence() const noexcept { return result_; }
    CodeBlock* address_block() const noexcept { return address_; }
    Disclosure* settings_disclosure() const noexcept { return settings_; }
    NoteBanner* empty_note() const noexcept { return empty_note_; }
    int device_row_count() const { return static_cast<int>(rows_.size()); }
    QRadioButton* device_radio(int index) const;
    vss::endpoint_id logical_port() const noexcept { return logical_port_; }
    vss::endpoint_id physical_port() const noexcept { return physical_port_; }

    void refresh_devices();

Q_SIGNALS:
    void error_shown(const QString& title);

private:
    struct device_row
    {
        PickRow* frame = nullptr;
        QRadioButton* radio = nullptr;
        QString path;
        bool available = false;
    };

    void build();
    void build_device_section();
    void build_settings_section();
    void rebuild_rows();
    void refresh_row_styles();
    void update_result();
    void update_address();
    void update_summary();
    void update_primary_state();
    void select_device(int index);
    void clear_device_selection();
    QString current_device_path() const;
    QString current_uri() const;
    vss::serial_settings current_settings() const;
    void attach();
    void show_error(const flow_error& error, const QString& device_path,
                    bool offer_another);
    void focus_devices();

    StudioBackend& backend_;
    platform_kind platform_;
    bool advanced_ = false;
    bool selecting_ = false;
    int selected_ = -1;

    QButtonGroup* group_ = nullptr;
    QVBoxLayout* rows_layout_ = nullptr;
    QWidget* device_card_ = nullptr;
    NoteBanner* empty_note_ = nullptr;
    QPushButton* refresh_ = nullptr;
    QPushButton* manual_toggle_ = nullptr;
    FormRow* manual_row_ = nullptr;
    QLineEdit* manual_path_ = nullptr;
    Disclosure* settings_ = nullptr;
    QLabel* settings_summary_ = nullptr;
    QComboBox* baud_ = nullptr;
    QComboBox* data_bits_ = nullptr;
    QComboBox* parity_ = nullptr;
    QComboBox* stop_bits_ = nullptr;
    QComboBox* flow_ = nullptr;
    QLineEdit* name_ = nullptr;
    FormRow* name_row_ = nullptr;
    QCheckBox* capture_ = nullptr;
    ResultSentence* result_ = nullptr;
    CodeBlock* address_ = nullptr;
    QPushButton* edit_address_ = nullptr;
    FormRow* address_row_ = nullptr;
    QLineEdit* address_edit_ = nullptr;

    QVector<device_row> rows_;
    vss::endpoint_id physical_port_ = 0;
    vss::endpoint_id logical_port_ = 0;
};

} // namespace vss::gui

#endif // VSS_GUI_FLOWS_ATTACH_DIALOG_HPP
