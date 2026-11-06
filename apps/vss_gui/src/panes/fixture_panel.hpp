//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_PANES_FIXTURE_PANEL_HPP
#define VSS_GUI_PANES_FIXTURE_PANEL_HPP

#include "panes/fixture_behaviour.hpp"

#include <QFrame>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace vss::gui
{

/// "What this fixture does" in the Inspector: echo, and a byte generator.
class FixturePanel final : public QFrame
{
    Q_OBJECT

public:
    explicit FixturePanel(FixtureBehaviours& behaviours,
                          QWidget* parent = nullptr);

    void set_port(vss::endpoint_id id);

    // Test hooks; the widgets remain private.
    QPushButton* generator_button() const noexcept { return generator_; }
    QCheckBox* echo_box() const noexcept { return echo_; }
    QComboBox* kind_box() const noexcept { return kind_; }
    QLineEdit* text_edit() const noexcept { return text_; }
    QSpinBox* rate_box() const noexcept { return rate_; }
    QString status_text() const;

private:
    void build();
    void sync();
    void apply_edits();
    void update_field_visibility(generator_kind kind);

    FixtureBehaviours& behaviours_;
    vss::endpoint_id port_ = 0;
    bool syncing_ = false;
    QCheckBox* echo_ = nullptr;
    QComboBox* kind_ = nullptr;
    QLineEdit* text_ = nullptr;
    QLabel* text_label_ = nullptr;
    QSpinBox* rate_ = nullptr;
    QSpinBox* chunk_ = nullptr;
    QLabel* chunk_label_ = nullptr;
    QPushButton* generator_ = nullptr;
    QLabel* status_ = nullptr;
};

} // namespace vss::gui

#endif // VSS_GUI_PANES_FIXTURE_PANEL_HPP
