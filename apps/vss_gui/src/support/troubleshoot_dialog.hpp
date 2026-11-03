//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_SUPPORT_TROUBLESHOOT_DIALOG_HPP
#define VSS_GUI_SUPPORT_TROUBLESHOOT_DIALOG_HPP

#include "backend/studio_backend.hpp"

#include <QDialog>
#include <QVector>

class QLabel;
class QFrame;
class QPushButton;
class QVBoxLayout;

namespace vss::gui::support
{

class CheckRowWidget;

/// "Why is nothing arriving on COM7?": five checks between the device and the
/// program, honest about what the engine cannot see.
class TroubleshootDialog final : public QDialog
{
    Q_OBJECT

public:
    TroubleshootDialog(StudioBackend& backend, vss::endpoint_id port,
                       QWidget* parent = nullptr);

    /// Re-reads the engine snapshot and rebuilds the checks.
    void refresh_checks();

    int check_count() const { return static_cast<int>(rows_.size()); }
    CheckRowWidget* check_widget(int index) const;
    QString title_text() const;
    /// The port path the dialog is about (empty when there is no port).
    QString port_path() const { return path_; }

Q_SIGNALS:
    void bundle_requested();

private:
    void build();
    void rebuild();
    void copy_path();

    StudioBackend& backend_;
    vss::endpoint_id port_id_ = 0;
    QString path_;
    QLabel* title_ = nullptr;
    QLabel* sub_ = nullptr;
    QLabel* steps_ = nullptr;
    QFrame* try_card_ = nullptr;
    QWidget* checks_host_ = nullptr;
    QVBoxLayout* checks_layout_ = nullptr;
    QPushButton* copy_ = nullptr;
    QVector<CheckRowWidget*> rows_;
};

} // namespace vss::gui::support

#endif // VSS_GUI_SUPPORT_TROUBLESHOOT_DIALOG_HPP
