//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "widgets/pick_row.hpp"

#include <QLabel>
#include <QMouseEvent>
#include <QRadioButton>

namespace vss::gui
{

PickRow::PickRow(QWidget* parent) : QFrame(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void PickRow::set_radio(QRadioButton* radio)
{
    radio_ = radio;
}

void PickRow::pass_clicks_through()
{
    for (auto* label : findChildren<QLabel*>())
    {
        // A selectable label takes the press for itself, so the row never
        // sees it.
        label->setTextInteractionFlags(Qt::NoTextInteraction);
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
    }
}

bool PickRow::selectable() const
{
    return radio_ != nullptr && radio_->isEnabled() && isEnabled();
}

void PickRow::select()
{
    radio_->setChecked(true);
    radio_->setFocus(Qt::MouseFocusReason);
}

void PickRow::mousePressEvent(QMouseEvent* event)
{
    // Accepting the press is what makes the release arrive here.
    if (event->button() == Qt::LeftButton && selectable())
    {
        pressed_ = true;
        event->accept();
        return;
    }
    QFrame::mousePressEvent(event);
}

void PickRow::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && pressed_)
    {
        pressed_ = false;
        if (selectable() && rect().contains(event->position().toPoint()))
            select();
        event->accept();
        return;
    }
    QFrame::mouseReleaseEvent(event);
}

void PickRow::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && selectable())
    {
        select();
        event->accept();
        Q_EMIT activated();
        return;
    }
    QFrame::mouseDoubleClickEvent(event);
}

} // namespace vss::gui
