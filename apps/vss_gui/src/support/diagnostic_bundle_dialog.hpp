//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_SUPPORT_DIAGNOSTIC_BUNDLE_DIALOG_HPP
#define VSS_GUI_SUPPORT_DIAGNOSTIC_BUNDLE_DIALOG_HPP

#include "backend/studio_backend.hpp"
#include "support/bundle.hpp"
#include "support/event_log.hpp"

#include <QDialog>
#include <QString>

class QCheckBox;
class QLabel;

namespace vss::gui::support
{

/// "Create a diagnostic bundle": the opt-in include list, the honest warning
/// on traffic contents, and a local stored ZIP.  Nothing is sent anywhere.
class DiagnosticBundleDialog final : public QDialog
{
    Q_OBJECT

public:
    DiagnosticBundleDialog(StudioBackend& backend, const EventLog& log,
                           QWidget* parent = nullptr);

    bundle_selection selection() const;
    int selected_file_count() const;
    QString bundle_name() const;
    QString suggested_path() const;

    /// Writes the selected files as one ZIP.  No file dialog is involved, so
    /// the test can validate the archive itself.
    bool write_bundle(const QString& path, QString* error = nullptr);

    bool available() const noexcept { return available_; }

private:
    void build();
    void update_counts();
    void save();
    void copy_environment();

    StudioBackend& backend_;
    const EventLog& log_;
    bundle_selection selection_;
    bool available_ = true;

    QCheckBox* setup_ = nullptr;
    QCheckBox* event_log_ = nullptr;
    QCheckBox* environment_ = nullptr;
    QCheckBox* counters_ = nullptr;
    QCheckBox* traffic_ = nullptr;
    QLabel* count_ = nullptr;
};

} // namespace vss::gui::support

#endif // VSS_GUI_SUPPORT_DIAGNOSTIC_BUNDLE_DIALOG_HPP
