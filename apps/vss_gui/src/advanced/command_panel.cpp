//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "advanced/command_panel.hpp"

#include "advanced/chrome.hpp"
#include "theme/icon_provider.hpp"
#include "widgets/notes.hpp"

#include <vss/core/profile.hpp>

#include <QClipboard>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace vss::gui
{
CommandPanel::CommandPanel(const QString& heading, QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto* title = new QLabel(heading, this);
    title->setFont(Theme::instance().font(text_style::caption));
    title->setProperty("muted", true);
    layout->addWidget(title);

    code_ = new CodeBlock(this);
    code_->set_text(QString(), tr("Command"));
    layout->addWidget(code_);

    auto* row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);
    copy_ = make_button(tr("Copy as command"), nullptr, this);
    copy_->setProperty("size", QStringLiteral("small"));
    polish(copy_);
    copy_->setAccessibleName(tr("Copy as command"));
    connect(copy_, &QPushButton::clicked, this, &CommandPanel::copy_command);
    row->addWidget(copy_);
    export_ = make_button(tr("Export file"), nullptr, this);
    export_->setProperty("size", QStringLiteral("small"));
    polish(export_);
    export_->setIcon(IconProvider::instance().icon(QStringLiteral("export"),
                                                   colour_role::ink, 14));
    export_->setAccessibleName(tr("Export file"));
    connect(export_, &QPushButton::clicked, this,
            &CommandPanel::export_requested);
    row->addWidget(export_);
    row->addStretch(1);
    layout->addLayout(row);

    connect(code_, &CodeBlock::copied, this,
            [this]
            {
                Q_EMIT copy_requested();
                Q_EMIT message(tr("Copied the command."));
            });
}

void CommandPanel::set_command(const QString& command)
{
    code_->set_text(command, tr("Command"));
}

QString CommandPanel::command() const
{
    return code_->text();
}

void CommandPanel::set_export_enabled(bool enabled)
{
    export_->setEnabled(enabled);
}

void CommandPanel::copy_command()
{
    VSS_PROFILE_ZONE("gui.advanced.command.copy");
    QGuiApplication::clipboard()->setText(command());
    Q_EMIT copy_requested();
    Q_EMIT message(tr("Copied the command."));
}

} // namespace vss::gui
