//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "flows/pair_dialog.hpp"

#include "widgets/dialogs.hpp"

#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace vss::gui
{

PairDialog::PairDialog(StudioBackend& backend, bool advanced,
                       platform_kind platform, QWidget* parent)
    : FlowDialog(tr("Connect two programs"),
                 tr("Create a linked pair of ports. Whatever one program "
                    "sends, the other receives."),
                 parent)
{
    panel_ = new PairPanel(backend, advanced, platform, this);
    body_layout()->addWidget(panel_);

    auto* cancel = add_footer_button(tr("Cancel"));
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    auto* create =
        add_footer_button(tr("Create pair"), QStringLiteral("primary"));
    connect(create, &QPushButton::clicked, this, &PairDialog::create_pair);

    connect(panel_, &PairPanel::state_changed, this,
            [this] { set_primary_enabled(panel_->can_create()); });
    connect(panel_, &PairPanel::advanced_requested, this,
            &PairDialog::advanced_requested);
    connect(panel_, &PairPanel::setup_check_requested, this,
            &PairDialog::setup_check_requested);
    set_primary_enabled(panel_->can_create());

    finish_layout(700);
    focus_first(panel_->pair_name_edit());
}

void PairDialog::create_pair()
{
    flow_error error;
    auto created = panel_->create_pair(&error);
    if (!created)
    {
        if (!error.title.isEmpty())
        {
            Q_EMIT error_shown(error.title);
            ErrorDialog dialog(error.title, error.body, error.details, this);
            dialog.exec();
        }
        return;
    }
    outcome_ = std::move(created);
    accept();
}

} // namespace vss::gui
