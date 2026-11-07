//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "advanced/export_command_dialog.hpp"

#include "advanced/chrome.hpp"
#include "advanced/command_panel.hpp"
#include "widgets/common.hpp"
#include "widgets/dialogs.hpp"

#include <vss/config/document.hpp>
#include <vss/core/profile.hpp>

#include <QCoreApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace vss::gui
{
ExportCommandDialog::ExportCommandDialog(StudioBackend& backend,
                                         const QString& document_path,
                                         QWidget* parent)
    : QDialog(parent), backend_(backend), path_(document_path)
{
    build();
    update_command();
}

void ExportCommandDialog::build()
{
    VSS_PROFILE_ZONE("gui.advanced.export_command.build");
    VSS_PROFILE_MESSAGE("gui.dialog.export_command.opened");
    const QString title = tr("Export as command");
    setWindowTitle(title);
    setAccessibleName(title);
    setProperty("surface", QStringLiteral("panel"));
    polish(this);
    resize(
        Theme::instance().metric(QStringLiteral("layout.dialogWidths.small")),
        320);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 0);
    layout->setSpacing(14);
    layout->addWidget(make_header(
        title, tr("Copy the command, or save the setup as a file first."), this,
        true, {}));

    panel_ = new CommandPanel(tr("Run it without the window"), this);
    connect(panel_, &CommandPanel::export_requested, this,
            [this] { static_cast<void>(export_file()); });
    connect(panel_, &CommandPanel::message, this,
            &ExportCommandDialog::message);
    layout->addWidget(panel_);

    hint_ = new QLabel(this);
    hint_->setFont(Theme::instance().font(text_style::caption));
    hint_->setProperty("muted", true);
    hint_->setWordWrap(true);
    set_ink2(hint_);
    layout->addWidget(hint_);
    layout->addStretch(1);

    QHBoxLayout* footer_layout = nullptr;
    auto* footer = make_footer(this, footer_layout);
    auto* close = make_button(tr("Close"), "primary", footer);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    footer_layout->addWidget(close);
    layout->addWidget(footer);

    close->setDefault(true);
    close->setFocus(Qt::OtherFocusReason);
    QTimer::singleShot(0, close,
                       [close] { close->setFocus(Qt::OtherFocusReason); });
}

QString ExportCommandDialog::command() const
{
    return panel_ == nullptr ? QString() : panel_->command();
}

QString ExportCommandDialog::suggested_name() const
{
    const vss::config::document document = backend_.snapshot();
    QString name = QString::fromStdString(document.name);
    if (name.isEmpty())
        name = QStringLiteral("lab");
    if (!name.endsWith(QLatin1String(".json"), Qt::CaseInsensitive))
        name += QStringLiteral(".json");
    return name;
}

void ExportCommandDialog::set_document_path(const QString& path)
{
    path_ = path;
    update_command();
}

void ExportCommandDialog::update_command()
{
    VSS_PROFILE_ZONE("gui.advanced.export_command.update");
    const QString file =
        path_.isEmpty() ? suggested_name() : QFileInfo(path_).fileName();
    panel_->set_command(
        QStringLiteral("vss_cli run %1 --seconds=60").arg(file));
    if (path_.isEmpty())
    {
        hint_->setText(
            tr("This setup has not been saved yet. Export the file first so "
               "the command can find it."));
    }
    else
    {
        hint_->setText(tr("The command uses the saved file name, %1.")
                           .arg(QFileInfo(path_).fileName()));
    }
    hint_->setAccessibleName(hint_->text());
}

bool ExportCommandDialog::export_file()
{
    VSS_PROFILE_ZONE("gui.advanced.export_command.export_file");
    const QString start = path_.isEmpty() ? suggested_name() : path_;
    QString path = QFileDialog::getSaveFileName(
        this, tr("Export the setup"), start,
        tr("Virtual Serial Studio setups (*.json)"));
    if (path.isEmpty())
        return false;
    if (!path.endsWith(QLatin1String(".json"), Qt::CaseInsensitive))
        path += QStringLiteral(".json");

    const auto saved =
        vss::config::save_file(path.toStdString(), backend_.snapshot());
    if (!saved)
    {
        ErrorDialog dialog(
            tr("Could not export the setup"),
            tr("The file could not be written. Choose another folder and try "
               "again."),
            error_text(saved.error()), this);
        dialog.exec();
        return false;
    }
    path_ = path;
    update_command();
    panel_->set_export_enabled(true);
    Q_EMIT message(tr("Saved %1.").arg(QFileInfo(path).fileName()));
    return true;
}

} // namespace vss::gui
