//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_SUPPORT_SHORTCUTS_DIALOG_HPP
#define VSS_GUI_SUPPORT_SHORTCUTS_DIALOG_HPP

#include <QDialog>
#include <QKeySequence>
#include <QString>
#include <QStringList>
#include <QVector>

class QLineEdit;
class QTableWidget;

namespace vss::gui::support
{

/// One row of the keyboard shortcuts list, built from the ActionRegistry by
/// the shell so this dialog does not depend on the shell library.
struct shortcut_entry
{
    QString action;
    QKeySequence shortcut;
    bool advanced = false;
};

/// Keyboard shortcuts: filtered, generated from the registry.
class ShortcutsDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit ShortcutsDialog(const QVector<shortcut_entry>& entries,
                             QWidget* parent = nullptr);

    /// Applies the filter field's text; exposed for tests.
    void set_filter(const QString& text);
    int total_row_count() const;
    int visible_row_count() const;
    QStringList action_texts() const;

    /// "Ctrl+N" / "Cmd+N" pair for one shortcut, independent of the host OS.
    static QString windows_linux_text(const QKeySequence& shortcut);
    static QString macos_text(const QKeySequence& shortcut);

protected:
    void showEvent(QShowEvent* event) override;

private:
    void build();
    void rebuild();
    void fit_key_columns();

    QVector<shortcut_entry> entries_;
    QLineEdit* filter_ = nullptr;
    QTableWidget* table_ = nullptr;
};

} // namespace vss::gui::support

#endif // VSS_GUI_SUPPORT_SHORTCUTS_DIALOG_HPP
