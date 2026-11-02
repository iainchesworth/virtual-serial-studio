//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_WIDGETS_DIALOGS_HPP
#define VSS_GUI_WIDGETS_DIALOGS_HPP

#include <QDialog>
#include <QString>

class QLabel;

namespace vss::gui
{

/// A safe confirmation.  The destructive button is never the default and
/// Escape always cancels.
class ConfirmDialog final : public QDialog
{
    Q_OBJECT

public:
    ConfirmDialog(const QString& title, const QString& body,
                  const QString& confirm_text,
                  const QString& cancel_text = QStringLiteral("Cancel"),
                  bool destructive = false, QWidget* parent = nullptr);

    static bool ask(const QString& title, const QString& body,
                    const QString& confirm_text,
                    const QString& cancel_text = QStringLiteral("Cancel"),
                    bool destructive = false, QWidget* parent = nullptr);

private:
    void build(const QString& title, const QString& body,
               const QString& confirm_text, const QString& cancel_text,
               bool destructive);
};

/// An error with the engine text available behind Copy details.
class ErrorDialog final : public QDialog
{
    Q_OBJECT

public:
    ErrorDialog(const QString& title, const QString& body,
                const QString& details = {}, QWidget* parent = nullptr);

private:
    QString details_;
};

} // namespace vss::gui

#endif // VSS_GUI_WIDGETS_DIALOGS_HPP
