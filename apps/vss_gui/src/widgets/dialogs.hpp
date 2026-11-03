//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_WIDGETS_DIALOGS_HPP
#define VSS_GUI_WIDGETS_DIALOGS_HPP

#include <QDialog>
#include <QString>
#include <QVector>

#include <functional>

class QFrame;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QVBoxLayout;

namespace vss::gui
{

/// The sunken footer bar shared by every dialog.  `layout` receives the
/// right-aligned stretch the caller appends its buttons to.
QFrame* make_dialog_footer(QDialog* dialog, QHBoxLayout*& layout);

/// A button with the shared cursor and an optional token role
/// ("primary", "quiet" or "danger").
QPushButton* make_dialog_button(const QString& text, const char* role,
                                QWidget* parent);

/// Title (title style) and one-sentence subtitle (muted body) at the top of
/// a dialog body layout.
void add_dialog_heading(QVBoxLayout* layout, const QString& title,
                        const QString& subtitle, QWidget* parent);

/// One button on a `ConfirmDialog`.
struct confirm_choice
{
    QString text;
    /// "primary", "quiet", "danger" or empty for the default role.
    QString role;
    /// Cancel leaves everything unchanged: the dialog is rejected.
    bool cancel = false;
};

/// A safe confirmation.  The destructive button is never the default and
/// Escape always cancels.  The three-or-more variant exists for the unsaved
/// changes prompt (Don't save, Cancel, Save).
class ConfirmDialog final : public QDialog
{
    Q_OBJECT

public:
    ConfirmDialog(const QString& title, const QString& body,
                  const QString& confirm_text,
                  const QString& cancel_text = QStringLiteral("Cancel"),
                  bool destructive = false, QWidget* parent = nullptr);

    ConfirmDialog(const QString& title, const QString& body,
                  const QVector<confirm_choice>& choices, int default_index,
                  QWidget* parent = nullptr);

    static bool ask(const QString& title, const QString& body,
                    const QString& confirm_text,
                    const QString& cancel_text = QStringLiteral("Cancel"),
                    bool destructive = false, QWidget* parent = nullptr);

    /// The index of the clicked choice, or -1 when the dialog was cancelled.
    int chosen() const noexcept { return chosen_; }
    QPushButton* choice_button(int index) const;
    int choice_count() const { return static_cast<int>(buttons_.size()); }

private:
    void build(const QString& title, const QString& body,
               const QVector<confirm_choice>& choices, int default_index);

    QVector<QPushButton*> buttons_;
    int chosen_ = -1;
};

/// An error with the engine text available behind Copy details.
class ErrorDialog final : public QDialog
{
    Q_OBJECT

public:
    ErrorDialog(const QString& title, const QString& body,
                const QString& details = {}, QWidget* parent = nullptr);

    /// Adds a recovery action to the footer, left of Close.  The caller owns
    /// what the action does; pressing it does not close the dialog by itself.
    QPushButton* add_action(const QString& text,
                            std::function<void()> callback = {},
                            const QString& role = QStringLiteral("quiet"));

private:
    QString details_;
    QHBoxLayout* footer_layout_ = nullptr;
    QPushButton* close_ = nullptr;
};

} // namespace vss::gui

#endif // VSS_GUI_WIDGETS_DIALOGS_HPP
