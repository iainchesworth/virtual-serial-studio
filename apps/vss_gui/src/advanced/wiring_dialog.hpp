//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_ADVANCED_WIRING_DIALOG_HPP
#define VSS_GUI_ADVANCED_WIRING_DIALOG_HPP

#include "backend/studio_backend.hpp"

#include <QDialog>
#include <QString>

class QLabel;

namespace vss::gui
{

class LineMatrixPanel;
class StatusChip;

/// "Inspector: modem", the line matrix and fault dialog from Faults.html.
class WiringDialog final : public QDialog
{
    Q_OBJECT

public:
    WiringDialog(StudioBackend& backend, vss::endpoint_id endpoint,
                 QWidget* parent = nullptr);

    LineMatrixPanel* panel() const noexcept { return panel_; }
    QString subtitle_text() const;
    StatusChip* state_chip() const noexcept { return state_chip_; }
    StatusChip* fixture_chip() const noexcept { return fixture_chip_; }
    QString notice_text() const;

private:
    void show_notice(const QString& text);

    StudioBackend& backend_;
    vss::endpoint_id endpoint_ = 0;
    QString other_name_;
    bool linked_ = false;
    LineMatrixPanel* panel_ = nullptr;
    StatusChip* state_chip_ = nullptr;
    StatusChip* fixture_chip_ = nullptr;
    QLabel* subtitle_ = nullptr;
    QLabel* notice_ = nullptr;
};

} // namespace vss::gui

#endif // VSS_GUI_ADVANCED_WIRING_DIALOG_HPP
