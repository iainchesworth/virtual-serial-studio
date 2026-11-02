//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "widgets/dialogs.hpp"

#include "widgets/common.hpp"

#include <QClipboard>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace vss::gui
{
namespace
{

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
    footer->setStyleSheet(
        QStringLiteral(
            "QFrame#dialogFooter { background: %1; border-top: 1px solid %2; }")
            .arg(Theme::instance()
                     .colour(colour_role::sunken)
                     .name(QColor::HexRgb),
                 Theme::instance()
                     .colour(colour_role::line)
                     .name(QColor::HexRgb)));
    layout = new QHBoxLayout(footer);
    layout->setContentsMargins(20, 10, 20, 10);
    layout->setSpacing(8);
    layout->addStretch(1);
    return footer;
}

void add_title_and_body(QVBoxLayout* layout, const QString& title,
                        const QString& body, QWidget* parent)
{
    auto* title_label = new QLabel(title, parent);
    title_label->setFont(Theme::instance().font(text_style::title));
    title_label->setWordWrap(true);
    layout->addWidget(title_label);

    auto* body_label = new QLabel(body, parent);
    body_label->setFont(Theme::instance().font(text_style::body));
    body_label->setWordWrap(true);
    body_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(body_label);
}

} // namespace

ConfirmDialog::ConfirmDialog(const QString& title, const QString& body,
                             const QString& confirm_text,
                             const QString& cancel_text, bool destructive,
                             QWidget* parent)
    : QDialog(parent)
{
    build(title, body, confirm_text, cancel_text, destructive);
}

bool ConfirmDialog::ask(const QString& title, const QString& body,
                        const QString& confirm_text, const QString& cancel_text,
                        bool destructive, QWidget* parent)
{
    ConfirmDialog dialog(title, body, confirm_text, cancel_text, destructive,
                         parent);
    return dialog.exec() == QDialog::Accepted;
}

void ConfirmDialog::build(const QString& title, const QString& body,
                          const QString& confirm_text,
                          const QString& cancel_text, bool destructive)
{
    setWindowTitle(title);
    setAccessibleName(title);
    setProperty("surface", QStringLiteral("panel"));
    polish(this);
    setMinimumWidth(440);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 0);
    layout->setSpacing(12);
    add_title_and_body(layout, title, body, this);
    layout->addStretch(1);

    QHBoxLayout* footer_layout = nullptr;
    QFrame* footer = make_footer(this, footer_layout);

    auto* cancel = make_button(cancel_text, nullptr, footer);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    footer_layout->addWidget(cancel);

    auto* confirm =
        make_button(confirm_text, destructive ? "danger" : "primary", footer);
    connect(confirm, &QPushButton::clicked, this, &QDialog::accept);
    footer_layout->addWidget(confirm);

    QPushButton* safe = destructive ? cancel : confirm;
    safe->setDefault(true);
    safe->setFocus(Qt::OtherFocusReason);
    QTimer::singleShot(0, safe,
                       [safe] { safe->setFocus(Qt::OtherFocusReason); });

    layout->addWidget(footer);
}

ErrorDialog::ErrorDialog(const QString& title, const QString& body,
                         const QString& details, QWidget* parent)
    : QDialog(parent), details_(details)
{
    setWindowTitle(title);
    setAccessibleName(title);
    setProperty("surface", QStringLiteral("panel"));
    polish(this);
    setMinimumWidth(460);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 0);
    layout->setSpacing(12);
    add_title_and_body(layout, title, body, this);
    layout->addStretch(1);

    QHBoxLayout* footer_layout = nullptr;
    QFrame* footer = make_footer(this, footer_layout);

    if (!details_.isEmpty())
    {
        auto* copy =
            make_button(QStringLiteral("Copy details"), "quiet", footer);
        connect(copy, &QPushButton::clicked, this,
                [this] { QGuiApplication::clipboard()->setText(details_); });
        footer_layout->addWidget(copy);
    }

    auto* close = make_button(QStringLiteral("Close"), "primary", footer);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    close->setDefault(true);
    close->setFocus(Qt::OtherFocusReason);
    QTimer::singleShot(0, close,
                       [close] { close->setFocus(Qt::OtherFocusReason); });
    footer_layout->addWidget(close);

    layout->addWidget(footer);
}

} // namespace vss::gui
