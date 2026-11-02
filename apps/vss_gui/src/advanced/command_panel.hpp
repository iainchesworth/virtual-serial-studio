//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_ADVANCED_COMMAND_PANEL_HPP
#define VSS_GUI_ADVANCED_COMMAND_PANEL_HPP

#include <QString>
#include <QWidget>

class QPushButton;

namespace vss::gui
{

class CodeBlock;

/// "Run it without the window": the command block and its two buttons.
///
/// The block owns the copy button inside itself; the panel adds "Copy as
/// command" and "Export file" and leaves writing the file to its owner.
class CommandPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit CommandPanel(const QString& heading, QWidget* parent = nullptr);

    void set_command(const QString& command);
    QString command() const;
    void set_export_enabled(bool enabled);
    QPushButton* copy_button() const noexcept { return copy_; }
    QPushButton* export_button() const noexcept { return export_; }
    CodeBlock* code_block() const noexcept { return code_; }

Q_SIGNALS:
    void copy_requested();
    void export_requested();
    void message(const QString& text);

private:
    void copy_command();

    CodeBlock* code_ = nullptr;
    QPushButton* copy_ = nullptr;
    QPushButton* export_ = nullptr;
};

} // namespace vss::gui

#endif // VSS_GUI_ADVANCED_COMMAND_PANEL_HPP
