//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_FLOWS_WATCH_DIALOG_HPP
#define VSS_GUI_FLOWS_WATCH_DIALOG_HPP

#include "backend/studio_backend.hpp"
#include "flows/common.hpp"
#include "flows/flow_dialog.hpp"

#include <QCheckBox>

class QComboBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QVBoxLayout;

namespace vss::gui
{

class FormRow;
class NoteBanner;

/// "Watch traffic": create a Studio-only loopback watcher and a read-only
/// monitor link from the chosen port.
class WatchDialog final : public FlowDialog
{
    Q_OBJECT

public:
    WatchDialog(StudioBackend& backend, vss::endpoint_id selected,
                platform_kind platform = host_platform(),
                QWidget* parent = nullptr);

    QComboBox* port_combo() const noexcept { return port_; }
    QLineEdit* watcher_name_edit() const noexcept { return name_; }
    QCheckBox* open_monitor_check() const noexcept { return open_monitor_; }
    NoteBanner* unavailable_note() const noexcept { return unavailable_; }
    vss::endpoint_id source_id() const noexcept { return source_; }
    vss::endpoint_id watcher_id() const noexcept { return watcher_; }
    vss::link_id link_id() const noexcept { return link_; }
    /// The note shown when the list of ports is empty, or null.
    NoteBanner* empty_note() const noexcept { return empty_; }
    /// True when the user chose "Create a port" from the empty note.
    bool create_port_requested() const noexcept
    {
        return create_port_requested_;
    }
    bool open_monitor_requested() const noexcept
    {
        return open_monitor_ != nullptr && open_monitor_->isChecked();
    }

Q_SIGNALS:
    void error_shown(const QString& title);
    void create_port_requested();

private:
    void build();
    void show_empty_state(const std::vector<device_view>& others,
                          QVBoxLayout* layout);
    void update_for_source();
    void update_primary_state();
    void start_watching();

    StudioBackend& backend_;
    platform_kind platform_;
    QComboBox* port_ = nullptr;
    QLineEdit* name_ = nullptr;
    FormRow* name_row_ = nullptr;
    QCheckBox* open_monitor_ = nullptr;
    QLabel* info_ = nullptr;
    NoteBanner* unavailable_ = nullptr;
    NoteBanner* empty_ = nullptr;
    bool create_port_requested_ = false;
    bool name_auto_ = true;
    vss::endpoint_id source_ = 0;
    vss::endpoint_id watcher_ = 0;
    vss::link_id link_ = 0;
};

} // namespace vss::gui

#endif // VSS_GUI_FLOWS_WATCH_DIALOG_HPP
