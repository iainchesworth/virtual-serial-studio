//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "flows/rename_dialog.hpp"

#include "flows/common.hpp"
#include "widgets/controls.hpp"
#include "widgets/dialogs.hpp"

#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace vss::gui
{

RenameDialog::RenameDialog(StudioBackend& backend, vss::endpoint_id port,
                           QWidget* parent)
    : FlowDialog(tr("Rename port"),
                 tr("Links and the saved setup use the new name."), parent),
      backend_(backend), port_(port)
{
    const auto view = backend_.port(port);
    old_name_ = view ? view->name : QString();

    auto* layout = new QVBoxLayout();
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);

    name_ = new QLineEdit(old_name_, this);
    name_->setObjectName(QStringLiteral("renameName"));
    name_->setAccessibleName(tr("New name"));
    name_->selectAll();
    name_row_ = new FormRow(
        tr("New name"), name_,
        tr("A program that opens this port by its path may need the new "
           "path."),
        this);
    layout->addWidget(name_row_);
    layout->addStretch(1);
    body_layout()->addLayout(layout);

    auto* cancel = add_footer_button(tr("Cancel"));
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    auto* apply =
        add_footer_button(tr("Rename"), QStringLiteral("primary"));
    connect(apply, &QPushButton::clicked, this, &RenameDialog::rename);

    connect(name_, &QLineEdit::textChanged, this,
            [this] { update_state(); });
    connect(name_, &QLineEdit::returnPressed, this,
            [this]
            {
                if (primary_button() != nullptr &&
                    primary_button()->isEnabled())
                    rename();
            });

    update_state();
    finish_layout(320);
    focus_first(name_);

    QWidget::setTabOrder(name_, cancel);
    QWidget::setTabOrder(cancel, apply);
}

QString RenameDialog::new_name() const
{
    return name_ != nullptr ? name_->text().trimmed() : QString();
}

QString RenameDialog::problem() const
{
    const QString text = new_name();
    if (text.isEmpty())
        return tr("Enter a name for the port.");
    if (text == old_name_)
        return {};
    const auto ports = backend_.ports();
    const bool taken =
        std::any_of(ports.begin(), ports.end(), [&](const port_view& view)
                    { return view.id != port_ && view.name == text; });
    if (taken)
        return tr("Another port is already called %1.").arg(text);
    return {};
}

void RenameDialog::update_state()
{
    const QString reason = problem();
    if (reason.isEmpty())
        name_row_->clear_error();
    else
        name_row_->set_error(reason);
    set_primary_enabled(reason.isEmpty() && new_name() != old_name_ &&
                        backend_.caps().rename_port);
}

void RenameDialog::rename()
{
    const QString reason = problem();
    if (!reason.isEmpty())
    {
        name_row_->set_error(reason);
        name_->setFocus(Qt::OtherFocusReason);
        return;
    }

    const auto renamed = backend_.rename_port(port_, new_name());
    if (!renamed)
    {
        flow_error error;
        error.title = error_title(tr("rename"), old_name_);
        error.body = error_body(
            renamed.error(),
            tr("Studio could not rename the port. Nothing was changed."),
            tr(" Try again."));
        error.details = error_text(renamed.error());
        Q_EMIT error_shown(error.title);
        ErrorDialog dialog(error.title, error.body, error.details, this);
        dialog.exec();
        return;
    }
    backend_.refresh();
    accept();
}

} // namespace vss::gui
