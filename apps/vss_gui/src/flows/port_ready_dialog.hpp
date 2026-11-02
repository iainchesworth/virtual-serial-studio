//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_FLOWS_PORT_READY_DIALOG_HPP
#define VSS_GUI_FLOWS_PORT_READY_DIALOG_HPP

#include "backend/studio_backend.hpp"
#include "flows/common.hpp"
#include "flows/flow_dialog.hpp"

#include <QString>

class QFrame;
class QLabel;
class QPushButton;

namespace vss::gui
{

class Disclosure;
class NoteBanner;
class StatusChip;

/// "COM7 is ready": the copyable path, the live program-open note, and the
/// saved-setup details.
class PortReadyDialog final : public FlowDialog
{
    Q_OBJECT

public:
    PortReadyDialog(StudioBackend& backend, vss::endpoint_id port,
                    const QString& virtual_kind, bool advanced,
                    platform_kind platform = host_platform(),
                    QWidget* parent = nullptr);

    QString path() const;
    vss::endpoint_id port_id() const noexcept { return port_id_; }
    QPushButton* copy_button() const noexcept { return copy_; }
    StatusChip* peer_chip() const noexcept { return peer_chip_; }
    QLabel* peer_text() const noexcept { return peer_text_; }
    Disclosure* details() const noexcept { return details_; }
    bool is_studio_only() const noexcept { return studio_only_; }

Q_SIGNALS:
    void create_another_requested();
    void connect_requested(vss::endpoint_id id);
    void setup_check_requested();

private:
    void build(bool advanced);
    void refresh_peer();
    void update_details_titles();
    QString kind_description() const;
    QString virtual_kind_token() const;
    QString setup_json() const;
    QString setup_command() const;

    StudioBackend& backend_;
    vss::endpoint_id port_id_ = 0;
    QString requested_kind_;
    platform_kind platform_;
    port_view port_;
    bool studio_only_ = false;

    QLabel* path_label_ = nullptr;
    QFrame* path_block_ = nullptr;
    QPushButton* copy_ = nullptr;
    QLabel* paragraph_ = nullptr;
    QFrame* peer_card_ = nullptr;
    StatusChip* peer_chip_ = nullptr;
    QLabel* peer_text_ = nullptr;
    NoteBanner* driver_note_ = nullptr;
    Disclosure* details_ = nullptr;
};

} // namespace vss::gui

#endif // VSS_GUI_FLOWS_PORT_READY_DIALOG_HPP
