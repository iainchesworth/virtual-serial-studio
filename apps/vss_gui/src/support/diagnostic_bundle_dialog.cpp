//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "support/diagnostic_bundle_dialog.hpp"

#include "support/build_info.hpp"
#include "support/zip_writer.hpp"
#include "theme/theme.hpp"
#include "widgets/chips.hpp"
#include "widgets/common.hpp"
#include "widgets/controls.hpp"
#include "widgets/dialogs.hpp"
#include "widgets/notes.hpp"

#include <QCheckBox>
#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSaveFile>
#include <QTimer>
#include <QVBoxLayout>

namespace vss::gui::support
{
namespace
{

QString t(const char* text)
{
    return QCoreApplication::translate("Bundle", text);
}

QFrame* include_row(QWidget* parent, QCheckBox*& check, const QString& title,
                    const QString& description, const QString& tag,
                    bool highlight = false, const QString& chip = {})
{
    auto* row = new QFrame(parent);
    row->setAttribute(Qt::WA_StyledBackground, true);
    if (highlight)
        row->setProperty("note", QStringLiteral("warn"));
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(12);

    check = new QCheckBox(row);
    check->setAccessibleName(title);
    layout->addWidget(check, 0, Qt::AlignTop);

    auto* column = new QVBoxLayout();
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(2);
    auto* heading = new QLabel(title, row);
    QFont font = Theme::instance().font(text_style::body);
    font.setWeight(QFont::DemiBold);
    heading->setFont(font);
    column->addWidget(heading);
    auto* detail = new QLabel(description, row);
    detail->setFont(Theme::instance().font(text_style::caption));
    detail->setWordWrap(true);
    if (!highlight)
    {
        detail->setProperty("muted", true);
        polish(detail);
    }
    column->addWidget(detail);
    layout->addLayout(column, 1);

    if (!tag.isEmpty())
    {
        auto* tag_label = new QLabel(tag, row);
        tag_label->setFont(Theme::instance().font(text_style::data));
        tag_label->setProperty("muted", true);
        polish(tag_label);
        layout->addWidget(tag_label, 0, Qt::AlignVCenter);
    }
    if (!chip.isEmpty())
    {
        auto* chip_label = new StatusChip(chip, StatusChip::kind::waiting, row);
        chip_label->set_small(true);
        chip_label->setAccessibleName(chip);
        layout->addWidget(chip_label, 0, Qt::AlignVCenter);
    }
    polish(row);
    return row;
}

} // namespace

DiagnosticBundleDialog::DiagnosticBundleDialog(StudioBackend& backend,
                                               const EventLog& log,
                                               QWidget* parent)
    : QDialog(parent), backend_(backend), log_(log)
{
    available_ = backend_.caps().diagnostics;
    build();
    update_counts();
}

void DiagnosticBundleDialog::build()
{
    setWindowTitle(t("Create a diagnostic bundle"));
    setAccessibleName(t("Create a diagnostic bundle"));
    setProperty("surface", QStringLiteral("panel"));
    polish(this);
    resize(Theme::instance().metric(
               QStringLiteral("layout.dialogWidths.standard")),
           700);
    setMinimumWidth(Theme::instance().metric(
        QStringLiteral("layout.dialogWidths.standard")));

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto* header = new QWidget(this);
    auto* header_layout = new QVBoxLayout(header);
    header_layout->setContentsMargins(24, 20, 24, 14);
    header_layout->setSpacing(4);
    add_dialog_heading(
        header_layout, t("Create a diagnostic bundle"),
        t("Gathers what a helper needs to understand your setup. Nothing is "
          "sent anywhere. You choose where to save it."),
        header);
    outer->addWidget(header);

    auto* body = new QWidget(this);
    auto* body_layout = new QVBoxLayout(body);
    body_layout->setContentsMargins(24, 4, 24, 16);
    body_layout->setSpacing(14);

    if (!available_)
    {
        auto* note = new NoteBanner(
            NoteBanner::kind::warn,
            t("Creating a diagnostic bundle is not available in this build."),
            body);
        body_layout->addWidget(note);
    }

    auto* include_label = new QLabel(t("Include"), body);
    include_label->setFont(Theme::instance().font(text_style::caption));
    QFont label_font = include_label->font();
    label_font.setWeight(QFont::DemiBold);
    include_label->setFont(label_font);
    body_layout->addWidget(include_label);

    auto* list = new QFrame(body);
    list->setProperty("card", true);
    polish(list);
    auto* list_layout = new QVBoxLayout(list);
    list_layout->setContentsMargins(0, 0, 0, 0);
    list_layout->setSpacing(0);

    list_layout->addWidget(
        include_row(list, setup_, t("Setup"),
                    t("Ports, links and their settings, as a topology file."),
                    QStringLiteral("setup.json")));
    list_layout->addWidget(
        include_row(list, event_log_, t("Event log"),
                    t("State changes and errors since the setup was opened."),
                    QStringLiteral("log.txt")));
    list_layout->addWidget(include_row(
        list, environment_, t("Environment"),
        t("Build, compiler, Qt version, operating system and architecture."),
        QStringLiteral("environment.txt")));
    list_layout->addWidget(include_row(
        list, counters_, t("Port counters"),
        t("State, bytes in and out, and bytes dropped for each port."),
        QStringLiteral("counters.csv")));
    list_layout->addWidget(include_row(
        list, traffic_, t("Traffic contents"),
        t("The actual bytes sent and received. They may include passwords or "
          "personal data."),
        {}, true, t("Off by default")));
    body_layout->addWidget(list);

    auto* privacy = new NoteBanner(
        NoteBanner::kind::info,
        t("Nothing leaves this PC until you share the file yourself. Look "
          "inside the bundle first if you want to check it."),
        body);
    body_layout->addWidget(privacy);

    auto* name_label = new QLabel(t("The bundle will be one file"), body);
    name_label->setFont(Theme::instance().font(text_style::caption));
    QFont name_font = name_label->font();
    name_font.setWeight(QFont::DemiBold);
    name_label->setFont(name_font);
    body_layout->addWidget(name_label);

    auto* file_row = new QFrame(body);
    file_row->setProperty("code", true);
    polish(file_row);
    auto* file_layout = new QHBoxLayout(file_row);
    file_layout->setContentsMargins(10, 6, 10, 6);
    auto* file_name = new QLabel(bundle_default_name(), file_row);
    file_name->setFont(Theme::instance().font(text_style::data));
    file_layout->addWidget(file_name, 1);
    count_ = new QLabel(file_row);
    count_->setFont(Theme::instance().font(text_style::caption));
    count_->setProperty("muted", true);
    polish(count_);
    file_layout->addWidget(count_, 0, Qt::AlignVCenter);
    body_layout->addWidget(file_row);
    body_layout->addStretch(1);
    outer->addWidget(body, 1);

    setup_->setObjectName(QStringLiteral("bundleSetup"));
    event_log_->setObjectName(QStringLiteral("bundleLog"));
    environment_->setObjectName(QStringLiteral("bundleEnvironment"));
    counters_->setObjectName(QStringLiteral("bundleCounters"));
    traffic_->setObjectName(QStringLiteral("bundleTraffic"));
    setup_->setChecked(selection_.setup);
    event_log_->setChecked(selection_.event_log);
    environment_->setChecked(selection_.environment);
    counters_->setChecked(selection_.counters);
    traffic_->setChecked(selection_.traffic);

    const auto connect_check =
        [this](QCheckBox* check, bool bundle_selection::* member)
    {
        connect(check, &QCheckBox::toggled, this,
                [this, member](bool on)
                {
                    selection_.*member = on;
                    update_counts();
                });
    };
    connect_check(setup_, &bundle_selection::setup);
    connect_check(event_log_, &bundle_selection::event_log);
    connect_check(environment_, &bundle_selection::environment);
    connect_check(counters_, &bundle_selection::counters);
    connect_check(traffic_, &bundle_selection::traffic);

    if (!available_)
    {
        for (QCheckBox* check :
             {setup_, event_log_, environment_, counters_, traffic_})
        {
            check->setEnabled(false);
            check->setToolTip(t("Not available in this build"));
        }
    }

    QHBoxLayout* footer_layout = nullptr;
    QFrame* footer = make_dialog_footer(this, footer_layout);
    auto* copy =
        make_dialog_button(t("Copy environment only"), "quiet", footer);
    copy->setAutoDefault(false);
    connect(copy, &QPushButton::clicked, this,
            &DiagnosticBundleDialog::copy_environment);
    footer_layout->addWidget(copy);
    auto* cancel = make_dialog_button(t("Cancel"), nullptr, footer);
    cancel->setAutoDefault(false);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    footer_layout->addWidget(cancel);
    auto* save = make_dialog_button(t("Save bundle"), "primary", footer);
    save->setObjectName(QStringLiteral("saveBundle"));
    save->setDefault(true);
    save->setEnabled(available_);
    if (!available_)
        save->setToolTip(t("Not available in this build"));
    connect(save, &QPushButton::clicked, this, &DiagnosticBundleDialog::save);
    footer_layout->addWidget(save);
    outer->addWidget(footer);

    save->setFocus(Qt::OtherFocusReason);
    QTimer::singleShot(0, save,
                       [save] { save->setFocus(Qt::OtherFocusReason); });
}

bundle_selection DiagnosticBundleDialog::selection() const
{
    return selection_;
}

int DiagnosticBundleDialog::selected_file_count() const
{
    return selection_.file_count();
}

QString DiagnosticBundleDialog::bundle_name() const
{
    return bundle_default_name();
}

QString DiagnosticBundleDialog::suggested_path() const
{
    return QDir(QDir::homePath()).filePath(bundle_name());
}

void DiagnosticBundleDialog::update_counts()
{
    if (count_ == nullptr)
        return;
    const int files = selected_file_count();
    count_->setText(files == 1 ? t("1 file") : t("%1 files").arg(files));
    count_->setAccessibleName(count_->text());
}

bool DiagnosticBundleDialog::write_bundle(const QString& path, QString* error)
{
    const QVector<bundle_file> files =
        build_bundle_files(backend_, log_, selection_);
    StoredZipWriter writer;
    for (const bundle_file& file : files)
    {
        if (!writer.add_file(file.name, file.data))
        {
            if (error != nullptr)
                *error = t("The bundle has too many files.");
            return false;
        }
    }

    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly))
    {
        if (error != nullptr)
            *error = output.errorString();
        return false;
    }
    const QByteArray archive = writer.data();
    if (output.write(archive) != archive.size())
    {
        if (error != nullptr)
            *error = output.errorString();
        output.cancelWriting();
        return false;
    }
    if (!output.commit())
    {
        if (error != nullptr)
            *error = output.errorString();
        return false;
    }
    return true;
}

void DiagnosticBundleDialog::save()
{
    QString path = QFileDialog::getSaveFileName(
        this, t("Save diagnostic bundle"), suggested_path(),
        t("ZIP archives (*.zip)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive))
        path += QStringLiteral(".zip");

    QString error;
    if (!write_bundle(path, &error))
    {
        ErrorDialog dialog(t("Could not save the bundle"), error, {}, this);
        dialog.exec();
        return;
    }
    ToastManager::instance().show(
        this, t("Bundle saved to %1.").arg(QFileInfo(path).fileName()));
    accept();
}

void DiagnosticBundleDialog::copy_environment()
{
    QGuiApplication::clipboard()->setText(environment_report());
    ToastManager::instance().show(this, t("Environment copied."));
}

} // namespace vss::gui::support
