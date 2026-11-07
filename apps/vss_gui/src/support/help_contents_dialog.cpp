//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "support/help_contents_dialog.hpp"

#include "theme/theme.hpp"
#include "widgets/common.hpp"
#include "widgets/dialogs.hpp"

#include <QCoreApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace vss::gui::support
{
namespace
{

QString t(const char* text)
{
    return QCoreApplication::translate("Help", text);
}

} // namespace

HelpContentsDialog::HelpContentsDialog(QWidget* parent) : QDialog(parent)
{
    build();
}

void HelpContentsDialog::build()
{
    setWindowTitle(QCoreApplication::translate("Help", "Help contents"));
    setAccessibleName(QCoreApplication::translate("Help", "Help contents"));
    setProperty("surface", QStringLiteral("panel"));
    polish(this);
    resize(
        Theme::instance().metric(QStringLiteral("layout.dialogWidths.small")),
        460);
    setMinimumWidth(
        Theme::instance().metric(QStringLiteral("layout.dialogWidths.small")));

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto* header = new QWidget(this);
    auto* header_layout = new QVBoxLayout(header);
    header_layout->setContentsMargins(24, 20, 24, 14);
    header_layout->setSpacing(4);
    add_dialog_heading(
        header_layout, QCoreApplication::translate("Help", "Help contents"),
        QCoreApplication::translate(
            "Help", "Every action is in the menus. Start with these."),
        header);
    outer->addWidget(header);

    auto* body = new QWidget(this);
    auto* body_layout = new QVBoxLayout(body);
    body_layout->setContentsMargins(24, 4, 24, 16);
    body_layout->setSpacing(8);

    struct topic
    {
        help_topic value{};
        const char* title = nullptr;
        const char* description = nullptr;
    };
    const topic topics[] = {
        {help_topic::setup_check, QT_TRANSLATE_NOOP("Help", "Setup check"),
         QT_TRANSLATE_NOOP(
             "Help",
             "Check the driver, permissions and devices this PC needs.")},
        {help_topic::troubleshoot,
         QT_TRANSLATE_NOOP("Help", "Why is nothing arriving?"),
         QT_TRANSLATE_NOOP(
             "Help",
             "Follow the five steps between the device and your program.")},
        {help_topic::shortcuts, QT_TRANSLATE_NOOP("Help", "Keyboard shortcuts"),
         QT_TRANSLATE_NOOP("Help", "See every action's shortcut in one list.")},
        {help_topic::about,
         QT_TRANSLATE_NOOP("Help", "About Virtual Serial Studio"),
         QT_TRANSLATE_NOOP("Help",
                           "Version, environment, licences and fonts.")},
    };

    for (const topic& item : topics)
    {
        auto* card = new QFrame(body);
        card->setProperty("card", true);
        polish(card);
        auto* card_layout = new QHBoxLayout(card);
        card_layout->setContentsMargins(12, 10, 12, 10);
        card_layout->setSpacing(12);
        auto* column = new QVBoxLayout();
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(2);
        auto* title = new QLabel(t(item.title), card);
        QFont font = Theme::instance().font(text_style::body);
        font.setWeight(QFont::DemiBold);
        title->setFont(font);
        column->addWidget(title);
        auto* description = new QLabel(t(item.description), card);
        description->setFont(Theme::instance().font(text_style::caption));
        description->setProperty("muted", true);
        description->setWordWrap(true);
        polish(description);
        column->addWidget(description);
        card_layout->addLayout(column, 1);
        auto* open = make_dialog_button(
            QCoreApplication::translate("Help", "Open"), nullptr, card);
        open->setAutoDefault(false);
        open->setAccessibleName(tr("%1: %2").arg(
            QCoreApplication::translate("Help", "Open"), t(item.title)));
        connect(open, &QPushButton::clicked, this,
                [this, value = item.value] { choose(value); });
        card_layout->addWidget(open, 0, Qt::AlignVCenter);
        body_layout->addWidget(card);
    }
    body_layout->addStretch(1);
    outer->addWidget(body, 1);

    QHBoxLayout* footer_layout = nullptr;
    QFrame* footer = make_dialog_footer(this, footer_layout);
    auto* close = make_dialog_button(
        QCoreApplication::translate("Help", "Close"), "primary", footer);
    close->setDefault(true);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    footer_layout->addWidget(close);
    outer->addWidget(footer);

    close->setFocus(Qt::OtherFocusReason);
    QTimer::singleShot(0, close,
                       [close] { close->setFocus(Qt::OtherFocusReason); });
}

void HelpContentsDialog::choose(help_topic topic)
{
    requested_ = topic;
    accept();
}

} // namespace vss::gui::support
