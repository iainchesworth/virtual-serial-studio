//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "support/check_row.hpp"

#include "theme/icon_provider.hpp"
#include "theme/theme.hpp"
#include "widgets/chips.hpp"
#include "widgets/common.hpp"
#include "widgets/dialogs.hpp"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

namespace vss::gui::support
{
namespace
{

struct tone_colours
{
    colour_role fill;
    colour_role edge;
    colour_role glyph;
    StatusChip::kind chip;
};

tone_colours colours_for(row_tone tone)
{
    switch (tone)
    {
    case row_tone::ok:
        return {colour_role::okSoft, colour_role::okLine, colour_role::ok,
                StatusChip::kind::healthy};
    case row_tone::warn:
        return {colour_role::amberSoft, colour_role::amberLine,
                colour_role::amber, StatusChip::kind::waiting};
    case row_tone::info:
        return {colour_role::accentSoft, colour_role::accentLine,
                colour_role::accentStrong, StatusChip::kind::fixture};
    case row_tone::neutral:
        break;
    }
    return {colour_role::sunken, colour_role::line2, colour_role::ink3,
            StatusChip::kind::closed};
}

} // namespace

CheckRowWidget::CheckRowWidget(const spec& row, QWidget* parent)
    : QFrame(parent), row_(row)
{
    setFrameShape(QFrame::NoFrame);
    auto* layout = new QGridLayout(this);
    const int margin = row_.framed ? 14 : 11;
    layout->setContentsMargins(margin, margin, margin, margin);
    layout->setHorizontalSpacing(14);
    layout->setVerticalSpacing(2);
    layout->setColumnMinimumWidth(0, 36);
    layout->setColumnStretch(1, 1);

    icon_holder_ = new QLabel(this);
    icon_holder_->setFixedSize(36, 36);
    icon_holder_->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto* icon_layout = new QVBoxLayout(icon_holder_);
    icon_layout->setContentsMargins(0, 0, 0, 0);
    glyph_ = new QLabel(icon_holder_);
    glyph_->setAlignment(Qt::AlignCenter);
    icon_layout->addWidget(glyph_, 0, Qt::AlignCenter);
    layout->addWidget(icon_holder_, 0, 0, 2, 1, Qt::AlignTop);

    auto* heading = new QHBoxLayout();
    heading->setContentsMargins(0, 0, 0, 0);
    heading->setSpacing(8);
    title_ = new QLabel(row_.title, this);
    title_->setFont(Theme::instance().font(text_style::body));
    title_->setWordWrap(true);
    QFont bold = title_->font();
    bold.setWeight(QFont::DemiBold);
    title_->setFont(bold);
    heading->addWidget(title_, 0, Qt::AlignVCenter);
    if (!row_.chip.isEmpty())
    {
        auto* chip =
            new StatusChip(row_.chip, colours_for(row_.tone).chip, this);
        chip->setAccessibleName(row_.chip);
        heading->addWidget(chip, 0, Qt::AlignVCenter);
    }
    heading->addStretch(1);
    layout->addLayout(heading, 0, 1);

    auto* sentence = new QLabel(row_.sentence, this);
    sentence->setFont(Theme::instance().font(text_style::body));
    sentence->setProperty("muted", true);
    sentence->setWordWrap(true);
    sentence->setTextInteractionFlags(Qt::TextSelectableByMouse);
    polish(sentence);
    layout->addWidget(sentence, 1, 1);

    if (!row_.action_text.isEmpty())
    {
        action_ = make_dialog_button(
            row_.action_text, row_.primary_action ? "primary" : nullptr, this);
        action_->setAutoDefault(false);
        action_->setAccessibleName(row_.action_text);
        connect(action_, &QPushButton::clicked, this,
                &CheckRowWidget::action_triggered);
        layout->addWidget(action_, 0, 2, 2, 1, Qt::AlignVCenter);
    }

    apply_style();
    connect(&Theme::instance(), &Theme::themeChanged, this,
            [this] { apply_style(); });
}

QString CheckRowWidget::title_text() const
{
    return title_ != nullptr ? title_->text() : QString();
}

void CheckRowWidget::apply_style()
{
    if (row_.highlight)
        setProperty("note", QStringLiteral("warn"));
    else if (row_.framed)
        setProperty("card", true);
    setAttribute(Qt::WA_StyledBackground, true);
    polish(this);

    const tone_colours colours = colours_for(row_.tone);
    icon_holder_->setStyleSheet(
        QStringLiteral("QLabel { background: %1; border: 1px solid %2; "
                       "border-radius: 18px; }")
            .arg(Theme::instance().colour(colours.fill).name(QColor::HexRgb),
                 Theme::instance().colour(colours.edge).name(QColor::HexRgb)));
    glyph_->setPixmap(
        IconProvider::instance()
            .icon(row_.icon.isEmpty() ? QStringLiteral("info") : row_.icon,
                  colours.glyph, 18)
            .pixmap(18, 18));
}

} // namespace vss::gui::support
