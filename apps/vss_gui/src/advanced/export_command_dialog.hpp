//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_ADVANCED_EXPORT_COMMAND_DIALOG_HPP
#define VSS_GUI_ADVANCED_EXPORT_COMMAND_DIALOG_HPP

#include "backend/studio_backend.hpp"

#include <QDialog>
#include <QString>

class QLabel;

namespace vss::gui
{

class CommandPanel;

/// File, Export as command.
///
/// Shows the real `vss_cli run <file>` command for this setup, lets the user
/// copy it and, when the setup has never been saved, exports the file first so
/// the command refers to something that exists.
class ExportCommandDialog final : public QDialog
{
    Q_OBJECT

public:
    ExportCommandDialog(StudioBackend& backend, const QString& document_path,
                        QWidget* parent = nullptr);

    QString command() const;
    QString document_path() const { return path_; }
    CommandPanel* panel() const noexcept { return panel_; }

    /// Opens the save dialog and writes the current setup.  Returns true when
    /// a file was written.
    bool export_file();
    void set_document_path(const QString& path);

Q_SIGNALS:
    void message(const QString& text);

private:
    void build();
    void update_command();
    QString suggested_name() const;

    StudioBackend& backend_;
    QString path_;
    CommandPanel* panel_ = nullptr;
    QLabel* hint_ = nullptr;
};

} // namespace vss::gui

#endif // VSS_GUI_ADVANCED_EXPORT_COMMAND_DIALOG_HPP
