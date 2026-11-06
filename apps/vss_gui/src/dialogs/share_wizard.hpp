//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_DIALOGS_SHARE_WIZARD_HPP
#define VSS_GUI_DIALOGS_SHARE_WIZARD_HPP

#include "backend/studio_backend.hpp"
#include "dialogs/share_plan.hpp"

#include <QDialog>
#include <QString>
#include <QStringList>
#include <QVector>

#include <vector>

class QButtonGroup;
class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QTableWidget;
class QVBoxLayout;

namespace vss::gui
{

class FormRow;
class InlineError;
class NoteBanner;
class Stepper;

/// Topology, Share a device.  A custom three step wizard, not QWizard.
class ShareWizard final : public QDialog
{
    Q_OBJECT

public:
    explicit ShareWizard(StudioBackend& backend, bool advanced = false,
                         QWidget* parent = nullptr);

    void set_advanced(bool advanced);
    bool advanced() const noexcept { return advanced_; }

    void refresh_devices();
    bool select_device(const QString& path);
    QString selected_device_path() const { return selected_device_; }
    void set_serial_settings(const vss::serial_settings& settings);
    vss::serial_settings serial_settings() const { return settings_; }
    QString device_settings_summary() const;
    /// Uses a network URI as the source.  Turns Advanced mode on.
    void set_network_source(const QString& uri);
    bool network_source() const noexcept { return network_mode_; }

    int current_step() const;
    bool next();
    bool back();
    bool share();
    bool accept_share();
    bool add_another_program();
    void set_programs(int programs);
    int programs() const { return programs_; }
    bool primary_enabled() const;

    QString source_name() const;
    QString source_uri() const;
    share_plan plan() const { return plan_; }
    QStringList free_pair_paths() const { return free_pair_paths_; }
    QStringList planned_paths() const;

    QString warning_text() const;
    bool warning_visible() const;
    bool acknowledge_studio_only();
    bool create_another_pair();
    QString share_info_text() const;
    QString step1_info_text() const;

    int listener_count() const;
    QVector<vss::endpoint_id> listener_ids() const { return listener_ids_; }
    QString row_program(int index) const;
    QString row_path(int index) const;
    QString row_state(int index) const;
    QString done_note_text() const;
    QString step3_title() const;

    QString last_error() const { return last_error_; }
    bool error_visible() const;

    QPushButton* primary_button() const { return primary_; }

Q_SIGNALS:
    void advanced_requested();
    void open_monitor_requested();

private:
    void build();
    QWidget* build_device_page();
    QWidget* build_programs_page();
    QWidget* build_ready_page();
    void build_footer();
    void add_device_row(const device_view& device, QWidget* parent,
                        QVBoxLayout* layout, int index);
    void update_device_list();
    void update_network_row();
    void update_settings_summary();
    void update_step1_info();
    void update_step();
    void update_footer();
    void update_programs_table();
    void update_warning();
    void update_ready_table();
    void update_ready_note();
    void refresh_step3_states();
    bool validate_source();
    QString program_default(int index) const;
    QString shared_title(int programs) const;
    QStringList program_names() const;
    void undo_share();
    void set_step(int step);
    void fit_to_screen(int width, int height);
    void clear_error();

    StudioBackend& backend_;
    bool advanced_ = false;
    bool network_mode_ = false;
    bool acknowledged_ = false;
    bool sharing_ = false;
    int step_ = 0;
    int programs_ = 3;
    vss::serial_settings settings_;
    QString selected_device_;
    std::vector<device_view> devices_;
    share_plan plan_;
    QStringList free_pair_paths_;
    QString step1_info_plain_;
    QString share_info_plain_;
    QString done_note_plain_;
    QString step3_title_;
    QString last_error_;

    QLabel* title_ = nullptr;
    QLabel* subtitle_ = nullptr;
    Stepper* stepper_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    InlineError* error_ = nullptr;

    QWidget* device_card_ = nullptr;
    QVBoxLayout* device_layout_ = nullptr;
    QButtonGroup* device_group_ = nullptr;
    QPushButton* refresh_ = nullptr;
    NoteBanner* no_devices_ = nullptr;
    QLabel* settings_summary_ = nullptr;
    QPushButton* change_settings_ = nullptr;
    QWidget* settings_panel_ = nullptr;
    QComboBox* baud_ = nullptr;
    QComboBox* data_bits_ = nullptr;
    QComboBox* parity_ = nullptr;
    QComboBox* stop_bits_ = nullptr;
    QComboBox* flow_ = nullptr;
    QPushButton* network_toggle_ = nullptr;
    QWidget* network_panel_ = nullptr;
    QLineEdit* network_address_ = nullptr;
    FormRow* network_row_ = nullptr;

    QLabel* programs_value_ = nullptr;
    QTableWidget* programs_table_ = nullptr;
    QFrame* warn_note_ = nullptr;
    QLabel* warn_text_ = nullptr;
    QPushButton* create_pair_ = nullptr;
    QPushButton* keep_only_ = nullptr;
    NoteBanner* share_info_ = nullptr;

    QTableWidget* ready_table_ = nullptr;
    NoteBanner* ready_note_ = nullptr;
    share_view shared_;
    QVector<vss::endpoint_id> listener_ids_;
    QStringList shared_programs_;

    QPushButton* cancel_ = nullptr;
    QPushButton* back_ = nullptr;
    QPushButton* next_ = nullptr;
    QPushButton* primary_ = nullptr;
    QPushButton* add_program_ = nullptr;
    QPushButton* open_monitor_ = nullptr;
    QPushButton* done_ = nullptr;
};

} // namespace vss::gui

#endif // VSS_GUI_DIALOGS_SHARE_WIZARD_HPP
