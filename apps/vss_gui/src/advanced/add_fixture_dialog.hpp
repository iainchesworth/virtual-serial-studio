//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_ADVANCED_ADD_FIXTURE_DIALOG_HPP
#define VSS_GUI_ADVANCED_ADD_FIXTURE_DIALOG_HPP

#include "backend/studio_backend.hpp"

#include <QDialog>
#include <QString>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace vss::gui
{

class FormRow;
class NoteBanner;

/// "Add a mock fixture", the AddFixture.html dialog.
///
/// The fixture and its two-way link are real engine operations.  When the
/// backend lacks `Capabilities::fixtures` the primary action is disabled with
/// the degraded tooltip and nothing is created.
class AddFixtureDialog final : public QDialog
{
    Q_OBJECT

public:
    AddFixtureDialog(StudioBackend& backend, vss::endpoint_id selected_port,
                     QWidget* parent = nullptr);

    QString fixture_name() const;
    void set_fixture_name(const QString& name);
    int baud_rate() const;
    void set_baud_rate(int baud);
    vss::endpoint_id target_port() const;
    void set_target_port(vss::endpoint_id id);
    QPushButton* add_button() const noexcept { return add_; }
    QString result_text() const;

    /// Creates the fixture and, when chosen, the two-way link.  Returns false
    /// and leaves an inline error on failure; the dialog stays open.
    bool add_fixture();

Q_SIGNALS:
    void message(const QString& text);

private:
    void build();
    void update_result();
    void set_error(const QString& text);
    void clear_error();
    QString target_label(vss::endpoint_id id) const;

    StudioBackend& backend_;
    vss::endpoint_id selected_port_ = 0;
    QLineEdit* name_ = nullptr;
    QComboBox* baud_ = nullptr;
    QComboBox* target_ = nullptr;
    FormRow* name_row_ = nullptr;
    QLabel* error_ = nullptr;
    NoteBanner* result_ = nullptr;
    QPushButton* add_ = nullptr;
};

} // namespace vss::gui

#endif // VSS_GUI_ADVANCED_ADD_FIXTURE_DIALOG_HPP
