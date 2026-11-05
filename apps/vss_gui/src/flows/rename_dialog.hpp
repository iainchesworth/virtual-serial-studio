//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_FLOWS_RENAME_DIALOG_HPP
#define VSS_GUI_FLOWS_RENAME_DIALOG_HPP

#include "backend/studio_backend.hpp"
#include "flows/flow_dialog.hpp"

class QLineEdit;

namespace vss::gui
{

class FormRow;

/// "Rename port": one name field.  Link names, events and the saved setup all
/// follow the new name, so the dialog says so and refuses a name another port
/// already has before anything is changed.
class RenameDialog final : public FlowDialog
{
    Q_OBJECT

public:
    RenameDialog(StudioBackend& backend, vss::endpoint_id port,
                 QWidget* parent = nullptr);

    QLineEdit* name_edit() const noexcept { return name_; }
    FormRow* name_row() const noexcept { return name_row_; }
    /// The name the port had when the dialog opened.
    const QString& old_name() const noexcept { return old_name_; }
    /// The name the port has now: the new one after a successful rename.
    QString new_name() const;

Q_SIGNALS:
    void error_shown(const QString& title);

private:
    /// Why the text in the field cannot be used, or empty when it can.
    QString problem() const;
    void update_state();
    void rename();

    StudioBackend& backend_;
    vss::endpoint_id port_ = 0;
    QString old_name_;
    QLineEdit* name_ = nullptr;
    FormRow* name_row_ = nullptr;
};

} // namespace vss::gui

#endif // VSS_GUI_FLOWS_RENAME_DIALOG_HPP
