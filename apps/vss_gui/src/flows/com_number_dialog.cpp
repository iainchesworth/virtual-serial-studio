//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "flows/com_number_dialog.hpp"

#include "flows/common.hpp"
#include "widgets/controls.hpp"

#include <vss/core/profile.hpp>

#include <QMetaObject>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace vss::gui
{

ComNumberDialog::ComNumberDialog(StudioBackend& backend, vss::endpoint_id port,
                                 QWidget* parent)
    : FlowDialog(tr("Change COM number"),
                 tr("Programs open the port by this number."), parent),
      backend_(backend), port_(port)
{
    const auto view = backend_.port(port);
    old_number_ = view ? view->com_number : 0U;

    auto* layout = new QVBoxLayout();
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);

    number_ = new QSpinBox(this);
    number_->setObjectName(QStringLiteral("comNumber"));
    number_->setRange(1, 255);
    number_->setPrefix(QStringLiteral("COM"));
    number_->setValue(old_number_ == 0 ? 1 : static_cast<int>(old_number_));
    number_->setMinimumWidth(150);
    number_->setAccessibleName(tr("COM number"));
    number_row_ = new FormRow(
        tr("New COM number"), number_,
        tr("Windows restarts the port to change its number, so no program "
           "may have it open. A number that another device has is refused."),
        this);
    layout->addWidget(number_row_);
    layout->addStretch(1);
    body_layout()->addLayout(layout);

    auto* cancel = add_footer_button(tr("Cancel"));
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    auto* apply =
        add_footer_button(tr("Change number"), QStringLiteral("primary"));
    connect(apply, &QPushButton::clicked, this, &ComNumberDialog::change);

    connect(number_, &QSpinBox::valueChanged, this, [this] { update_state(); });

    update_state();
    finish_layout(320);
    focus_first(number_);

    QWidget::setTabOrder(number_, cancel);
    QWidget::setTabOrder(cancel, apply);
}

ComNumberDialog::~ComNumberDialog()
{
    if (worker_.joinable())
        worker_.join();
}

QString ComNumberDialog::com_name() const
{
    const auto view = backend_.port(port_);
    return view ? view->path : QString();
}

void ComNumberDialog::done(int result)
{
    // The restart cannot be taken back half way, so the dialog stays until the
    // answer is in.
    if (busy_)
        return;
    FlowDialog::done(result);
}

QString ComNumberDialog::problem() const
{
    const auto view = backend_.port(port_);
    if (!view || !view->driver_port || !backend_.caps().com_numbers)
        return tr("Not available in this build");
    if (view->peer == peer_state::open)
    {
        return com_number_refusal(vss::make_error_code(vss::errc::device_busy),
                                  static_cast<unsigned>(number_->value()),
                                  true);
    }
    return {};
}

void ComNumberDialog::update_state()
{
    number_row_->clear_error();
    const QString reason = problem();
    if (!reason.isEmpty())
        number_row_->set_error(reason);
    set_primary_enabled(reason.isEmpty() && !busy_ &&
                        static_cast<unsigned>(number_->value()) != old_number_);
}

void ComNumberDialog::change()
{
    VSS_PROFILE_ZONE("gui.flows.com_number.change");
    if (busy_)
        return;
    const unsigned number = static_cast<unsigned>(number_->value());
    const QString reason = problem();
    if (!reason.isEmpty() || number == old_number_)
        return;

    if (worker_.joinable())
        worker_.join();
    busy_ = true;
    number_->setEnabled(false);
    set_primary_text(tr("Changing"));
    set_primary_enabled(false);
    number_row_->clear_error();

    // The worker calls the backend and posts a plain value back; it touches no
    // widget.  The destructor joins it before the dialog goes.
    worker_ = std::thread(
        [this, number]
        {
            const auto outcome = backend_.set_com_number(port_, number);
            QMetaObject::invokeMethod(
                this, [this, number, outcome] { on_finished(number, outcome); },
                Qt::QueuedConnection);
        });
}

void ComNumberDialog::on_finished(unsigned number,
                                  const vss::result<QString>& outcome)
{
    VSS_PROFILE_ZONE("gui.flows.com_number.finished");
    if (worker_.joinable())
        worker_.join();
    busy_ = false;
    number_->setEnabled(true);
    set_primary_text(tr("Change number"));

    if (!outcome)
    {
        number_row_->set_error(com_number_refusal(outcome.error(), number, true));
        number_->setFocus(Qt::OtherFocusReason);
        set_primary_enabled(true);
        Q_EMIT error_shown(tr("Could not change the COM number"));
        return;
    }
    backend_.refresh();
    accept();
}

} // namespace vss::gui
