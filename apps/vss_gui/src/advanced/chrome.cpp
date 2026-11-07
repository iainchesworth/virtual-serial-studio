//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "advanced/chrome.hpp"

#include "widgets/chips.hpp"
#include "widgets/common.hpp"
#include "widgets/dialogs.hpp"

#include <QCoreApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QVBoxLayout>

namespace vss::gui
{

QString not_available_text()
{
    return QCoreApplication::translate("Advanced",
                                       "Not available in this build");
}

void degrade(QWidget* widget, const QString& reason)
{
    if (widget == nullptr)
        return;
    const QString text = reason.isEmpty() ? not_available_text() : reason;
    widget->setEnabled(false);
    widget->setToolTip(text);
    widget->setAccessibleDescription(text);
}

namespace
{

void apply_ink2(QWidget* widget)
{
    QPalette palette = widget->palette();
    palette.setColor(QPalette::WindowText,
                     Theme::instance().colour(colour_role::ink2));
    widget->setPalette(palette);
}

} // namespace

void set_ink2(QWidget* widget)
{
    if (widget == nullptr)
        return;
    apply_ink2(widget);
    QObject::connect(&Theme::instance(), &Theme::theme_changed, widget,
                     [widget] { apply_ink2(widget); });
}

QWidget* make_header(const QString& title, const QString& subtitle,
                     QWidget* parent, bool advanced_badge,
                     const QVector<QWidget*>& chips)
{
    auto* header = new QWidget(parent);
    auto* layout = new QVBoxLayout(header);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    auto* row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(10);
    auto* title_label = new QLabel(title, header);
    title_label->setFont(Theme::instance().font(text_style::title));
    // A word-wrapped title reported a narrow size hint and broke after the
    // first word ("Inspector:" over "console").  Titles are one line; the
    // dialog grows instead.
    title_label->setWordWrap(false);
    row->addWidget(title_label, 0, Qt::AlignVCenter);
    for (QWidget* chip : chips)
    {
        if (chip != nullptr)
            row->addWidget(chip, 0, Qt::AlignVCenter);
    }
    if (advanced_badge)
    {
        auto* badge = new Badge(QObject::tr("ADVANCED"), header);
        badge->setAccessibleName(
            QCoreApplication::translate("Advanced", "Advanced tool"));
        row->addWidget(badge, 0, Qt::AlignVCenter);
    }
    row->addStretch(1);
    layout->addLayout(row);

    if (!subtitle.isEmpty())
    {
        auto* subtitle_label = new QLabel(subtitle, header);
        subtitle_label->setFont(Theme::instance().font(text_style::body));
        subtitle_label->setWordWrap(true);
        set_ink2(subtitle_label);
        layout->addWidget(subtitle_label);
    }
    return header;
}

QPushButton* make_button(const QString& text, const char* role, QWidget* parent)
{
    auto* button = new QPushButton(text, parent);
    button->setCursor(Qt::PointingHandCursor);
    if (role != nullptr)
        set_role(button, role);
    return button;
}

QFrame* make_footer(QWidget* parent, QHBoxLayout*& layout)
{
    auto* footer = new QFrame(parent);
    footer->setObjectName(QStringLiteral("dialogFooter"));
    footer->setAttribute(Qt::WA_StyledBackground);
    footer->setFrameShape(QFrame::NoFrame);
    layout = new QHBoxLayout(footer);
    set_dialog_footer_margins(layout);
    // These dialogs already inset their whole layout by the dialog padding,
    // footer included, so the footer adds none of its own at the sides.
    const QMargins margins = layout->contentsMargins();
    layout->setContentsMargins(0, margins.top(), 0, margins.bottom());
    layout->addStretch(1);
    return footer;
}

QLabel* make_heading(const QString& text, text_style style, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    QFont font = Theme::instance().font(style);
    font.setWeight(QFont::DemiBold);
    label->setFont(font);
    label->setWordWrap(true);
    return label;
}

} // namespace vss::gui
