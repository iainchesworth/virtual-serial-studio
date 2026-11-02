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

#include <algorithm>

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
    const auto apply_style = [footer]
    {
        footer->setStyleSheet(
            QStringLiteral("QFrame#dialogFooter { background: %1; border-top: "
                           "1px solid %2; }")
                .arg(Theme::instance()
                         .colour(colour_role::sunken)
                         .name(QColor::HexRgb),
                     Theme::instance()
                         .colour(colour_role::line)
                         .name(QColor::HexRgb)));
    };
    apply_style();
    // A theme switch while a dialog is open must recolour its footer too.
    QObject::connect(&Theme::instance(), &Theme::themeChanged, footer,
                     [apply_style] { apply_style(); });
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

QFrame* make_dialog_footer(QDialog* dialog, QHBoxLayout*& layout)
{
    return make_footer(dialog, layout);
}

QPushButton* make_dialog_button(const QString& text, const char* role,
                                QWidget* parent)
{
    return make_button(text, role, parent);
}

void add_dialog_heading(QVBoxLayout* layout, const QString& title,
                        const QString& subtitle, QWidget* parent)
{
    auto* title_label = new QLabel(title, parent);
    title_label->setFont(Theme::instance().font(text_style::title));
    title_label->setWordWrap(true);
    layout->addWidget(title_label);

    if (subtitle.isEmpty())
        return;
    auto* subtitle_label = new QLabel(subtitle, parent);
    subtitle_label->setFont(Theme::instance().font(text_style::body));
    subtitle_label->setWordWrap(true);
    subtitle_label->setProperty("muted", true);
    polish(subtitle_label);
    layout->addWidget(subtitle_label);
}

ConfirmDialog::ConfirmDialog(const QString& title, const QString& body,
                             const QString& confirm_text,
                             const QString& cancel_text, bool destructive,
                             QWidget* parent)
    : ConfirmDialog(
          title, body,
          QVector<confirm_choice>{{cancel_text, QString(), true},
                                  {confirm_text,
                                   destructive ? QStringLiteral("danger")
                                               : QStringLiteral("primary"),
                                   false}},
          destructive ? 0 : 1, parent)
{
}

ConfirmDialog::ConfirmDialog(const QString& title, const QString& body,
                             const QVector<confirm_choice>& choices,
                             int default_index, QWidget* parent)
    : QDialog(parent)
{
    build(title, body, choices, default_index);
}

bool ConfirmDialog::ask(const QString& title, const QString& body,
                        const QString& confirm_text, const QString& cancel_text,
                        bool destructive, QWidget* parent)
{
    ConfirmDialog dialog(title, body, confirm_text, cancel_text, destructive,
                         parent);
    return dialog.exec() == QDialog::Accepted;
}

QPushButton* ConfirmDialog::choice_button(int index) const
{
    if (index < 0 || index >= buttons_.size())
        return nullptr;
    return buttons_.at(index);
}

void ConfirmDialog::build(const QString& title, const QString& body,
                          const QVector<confirm_choice>& choices,
                          int default_index)
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

    if (choices.isEmpty())
    {
        auto* close = make_button(QStringLiteral("Close"), "primary", footer);
        connect(close, &QPushButton::clicked, this, &QDialog::accept);
        buttons_.push_back(close);
        footer_layout->addWidget(close);
        close->setDefault(true);
        close->setFocus(Qt::OtherFocusReason);
        layout->addWidget(footer);
        return;
    }

    for (int index = 0; index < choices.size(); ++index)
    {
        const confirm_choice& choice = choices.at(index);
        const QByteArray role = choice.role.toLatin1();
        auto* button = make_button(
            choice.text, role.isEmpty() ? nullptr : role.constData(), footer);
        connect(button, &QPushButton::clicked, this,
                [this, index, cancel = choice.cancel]
                {
                    chosen_ = cancel ? -1 : index;
                    if (cancel)
                        reject();
                    else
                        accept();
                });
        buttons_.push_back(button);
        footer_layout->addWidget(button);
    }

    int safe =
        std::clamp(default_index, 0, static_cast<int>(choices.size()) - 1);
    if (choices.at(safe).role == QLatin1String("danger"))
    {
        for (int index = 0; index < choices.size(); ++index)
        {
            if (choices.at(index).role != QLatin1String("danger"))
            {
                safe = index;
                break;
            }
        }
    }
    QPushButton* safe_button = buttons_.at(safe);
    safe_button->setDefault(true);
    safe_button->setFocus(Qt::OtherFocusReason);
    QTimer::singleShot(0, safe_button, [safe_button]
                       { safe_button->setFocus(Qt::OtherFocusReason); });

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
