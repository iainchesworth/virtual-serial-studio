//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "advanced/benchmark_dialog.hpp"

#include "advanced/chrome.hpp"
#include "widgets/chips.hpp"
#include "widgets/common.hpp"
#include "widgets/controls.hpp"
#include "widgets/notes.hpp"

#include <QClipboard>
#include <QComboBox>
#include <QCoreApplication>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

namespace vss::gui
{
namespace
{

QString port_label(const port_view& port)
{
    if (port.path.isEmpty())
        return port.name;
    return QCoreApplication::translate("Benchmark", "%1 (%2)")
        .arg(port.name, port.path);
}

} // namespace

BenchmarkDialog::BenchmarkDialog(StudioBackend& backend, QWidget* parent)
    : QDialog(parent), backend_(backend)
{
    build();
    refresh();
}

void BenchmarkDialog::build()
{
    const QString title = tr("Benchmark a connection");
    setWindowTitle(title);
    setAccessibleName(title);
    setProperty("surface", QStringLiteral("panel"));
    polish(this);
    resize(Theme::instance().metric(
               QStringLiteral("layout.dialogWidths.standard")),
           720);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 0);
    layout->setSpacing(14);
    layout->addWidget(make_header(
        title,
        tr("Sends data from one port to another and counts what arrives."),
        this, true, {}));

    auto* fields = new QGridLayout();
    fields->setContentsMargins(0, 0, 0, 0);
    fields->setHorizontalSpacing(12);
    fields->setVerticalSpacing(10);

    from_ = new QComboBox(this);
    from_->setAccessibleName(tr("Send from"));
    fields->addWidget(new FormRow(tr("Send from"), from_, {}, this), 0, 0);

    to_ = new QComboBox(this);
    to_->setAccessibleName(tr("Receive on"));
    fields->addWidget(new FormRow(tr("Receive on"), to_, {}, this), 0, 1);

    amount_ = new QComboBox(this);
    amount_->setAccessibleName(tr("Amount of data"));
    amount_->addItem(QStringLiteral("1 MB"), QStringLiteral("1MB"));
    amount_->addItem(QStringLiteral("10 MB"), QStringLiteral("10MB"));
    amount_->addItem(QStringLiteral("100 MB"), QStringLiteral("100MB"));
    amount_->setCurrentIndex(1);
    fields->addWidget(new FormRow(tr("Amount of data"), amount_, {}, this), 1,
                      0);

    chunk_ = new QComboBox(this);
    chunk_->setAccessibleName(tr("Chunk size"));
    chunk_->addItem(QStringLiteral("1 KB"), QStringLiteral("1KB"));
    chunk_->addItem(QStringLiteral("4 KB"), QStringLiteral("4KB"));
    chunk_->addItem(QStringLiteral("64 KB"), QStringLiteral("64KB"));
    chunk_->setCurrentIndex(1);
    fields->addWidget(new FormRow(tr("Chunk size"), chunk_, {}, this), 1, 1);
    layout->addLayout(fields);

    layout->addWidget(new NoteBanner(
        NoteBanner::kind::info,
        tr("Do not link these two ports in Studio. The data has to travel "
           "through the cable or driver pair, so the count means something."),
        this));

    linked_note_ = new NoteBanner(
        NoteBanner::kind::warn,
        tr("These two ports are linked in Studio. Disconnect them first."),
        this);
    linked_note_->hide();
    layout->addWidget(linked_note_);

    auto* result_card = new QFrame(this);
    result_card->setProperty("card", true);
    polish(result_card);
    auto* result_layout = new QVBoxLayout(result_card);
    result_layout->setContentsMargins(16, 16, 16, 16);
    result_layout->setSpacing(12);

    auto* result_row = new QHBoxLayout();
    result_row->setContentsMargins(0, 0, 0, 0);
    result_row->setSpacing(10);
    result_ = new StatusChip(tr("Not run yet"), StatusChip::kind::waiting,
                             result_card);
    result_->setAccessibleName(tr("Benchmark result: not run yet"));
    result_row->addWidget(result_, 0, Qt::AlignVCenter);
    result_row->addStretch(1);
    finished_ = new QLabel(tr("Not started"), result_card);
    finished_->setFont(Theme::instance().font(text_style::caption));
    finished_->setProperty("muted", true);
    result_row->addWidget(finished_);
    result_layout->addLayout(result_row);

    progress_ = new QProgressBar(result_card);
    progress_->setAccessibleName(tr("Benchmark progress"));
    progress_->setRange(0, 100);
    progress_->setValue(0);
    progress_->setTextVisible(false);
    result_layout->addWidget(progress_);

    auto* metrics = new QGridLayout();
    metrics->setContentsMargins(0, 0, 0, 0);
    metrics->setHorizontalSpacing(10);
    const char* names[4] = {"Sent", "Received", "Lost", "Throughput"};
    for (int index = 0; index < 4; ++index)
    {
        auto* cell = new QFrame(result_card);
        cell->setProperty("card", true);
        polish(cell);
        auto* cell_layout = new QVBoxLayout(cell);
        cell_layout->setContentsMargins(14, 10, 14, 10);
        cell_layout->setSpacing(2);
        auto* key = new QLabel(tr(names[index]), cell);
        key->setFont(Theme::instance().font(text_style::caption));
        key->setProperty("muted", true);
        cell_layout->addWidget(key);
        auto* value = new QLabel(tr("Not run"), cell);
        value->setFont(Theme::instance().font(text_style::title));
        value->setAccessibleName(tr("%1: not run yet").arg(tr(names[index])));
        cell_layout->addWidget(value);
        metrics_[index] = value;
        metrics->addWidget(cell, 0, index);
    }
    result_layout->addLayout(metrics);

    auto* result_caption = new QLabel(
        tr("A run fails when the received count is short, and the result says "
           "by how much."),
        result_card);
    result_caption->setFont(Theme::instance().font(text_style::caption));
    result_caption->setProperty("muted", true);
    result_caption->setWordWrap(true);
    result_layout->addWidget(result_caption);
    layout->addWidget(result_card);

    auto* command_title =
        new QLabel(tr("Same thing from the command line"), this);
    command_title->setFont(Theme::instance().font(text_style::caption));
    command_title->setProperty("muted", true);
    layout->addWidget(command_title);
    command_ = new CodeBlock(this);
    command_->set_text(QString(), tr("Command"));
    layout->addWidget(command_);
    layout->addStretch(1);

    QHBoxLayout* footer_layout = nullptr;
    auto* footer = make_footer(this, footer_layout);
    copy_result_ = make_button(tr("Copy result"), "quiet", footer);
    copy_result_->setAccessibleName(tr("Copy result"));
    copy_result_->setEnabled(false);
    connect(copy_result_, &QPushButton::clicked, this,
            [this]
            {
                if (!has_result_)
                    return;
                QGuiApplication::clipboard()->setText(result_summary());
                Q_EMIT message(tr("Copied the result."));
            });
    footer_layout->addWidget(copy_result_);
    auto* close = make_button(tr("Close"), nullptr, footer);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    footer_layout->addWidget(close);
    run_ = make_button(tr("Run"), "primary", footer);
    run_->setAccessibleName(tr("Run"));
    connect(run_, &QPushButton::clicked, this, &BenchmarkDialog::run);
    footer_layout->addWidget(run_);
    layout->addWidget(footer);

    connect(from_, &QComboBox::currentIndexChanged, this,
            [this] { update_state(); });
    connect(to_, &QComboBox::currentIndexChanged, this,
            [this] { update_state(); });
    connect(amount_, &QComboBox::currentIndexChanged, this,
            [this] { update_command(); });
    connect(chunk_, &QComboBox::currentIndexChanged, this,
            [this] { update_command(); });
    connect(&backend_, &StudioBackend::ports_changed, this,
            &BenchmarkDialog::refresh);
    connect(&backend_, &StudioBackend::links_changed, this,
            &BenchmarkDialog::refresh);

    from_->setFocus(Qt::OtherFocusReason);
    QTimer::singleShot(0, from_,
                       [this] { from_->setFocus(Qt::OtherFocusReason); });
}

void BenchmarkDialog::refresh()
{
    const vss::endpoint_id previous_from =
        from_->count() == 0
            ? 0
            : static_cast<vss::endpoint_id>(from_->currentData().toULongLong());
    const vss::endpoint_id previous_to =
        to_->count() == 0
            ? 0
            : static_cast<vss::endpoint_id>(to_->currentData().toULongLong());

    const QSignalBlocker from_blocker(from_);
    const QSignalBlocker to_blocker(to_);
    from_->clear();
    to_->clear();
    for (const auto& port : backend_.ports())
    {
        from_->addItem(port_label(port), static_cast<qulonglong>(port.id));
        to_->addItem(port_label(port), static_cast<qulonglong>(port.id));
    }
    const auto select = [](QComboBox* combo, vss::endpoint_id id, int fallback)
    {
        int index =
            id == 0 ? fallback : combo->findData(static_cast<qulonglong>(id));
        if (index < 0)
            index = fallback;
        if (index >= 0 && index < combo->count())
            combo->setCurrentIndex(index);
    };
    select(from_, previous_from, 0);
    select(to_, previous_to, to_->count() > 1 ? 1 : 0);
    if (from_->currentIndex() == to_->currentIndex() && from_->count() > 1)
        to_->setCurrentIndex((from_->currentIndex() + 1) % from_->count());
    update_command();
    update_state();
}

bool BenchmarkDialog::ports_linked() const
{
    const vss::endpoint_id from =
        static_cast<vss::endpoint_id>(from_->currentData().toULongLong());
    const vss::endpoint_id to =
        static_cast<vss::endpoint_id>(to_->currentData().toULongLong());
    if (from == 0 || to == 0)
        return false;
    for (const auto& link : backend_.links())
    {
        const bool forward =
            link.sources.contains(from) && link.targets.contains(to);
        const bool backward =
            link.sources.contains(to) && link.targets.contains(from);
        if (forward || backward)
            return true;
    }
    return false;
}

QString BenchmarkDialog::block_reason() const
{
    if (backend_.ports().size() < 2)
        return tr("Add two ports to benchmark a connection.");
    const vss::endpoint_id from =
        static_cast<vss::endpoint_id>(from_->currentData().toULongLong());
    const vss::endpoint_id to =
        static_cast<vss::endpoint_id>(to_->currentData().toULongLong());
    if (from == 0 || to == 0 || from == to)
        return tr("Choose two different ports.");
    if (ports_linked())
        return tr("These two ports are linked in Studio. Disconnect them "
                  "first.");
    // The shared benchmark core has not been extracted from vss_cli (it
    // needs maintainer approval), so even a test double that reports the
    // capability cannot start a run in this build.
    return not_available_text();
}

bool BenchmarkDialog::can_run() const
{
    return block_reason().isEmpty();
}

QString BenchmarkDialog::result_summary() const
{
    return result_->text();
}

QString BenchmarkDialog::command() const
{
    return command_->text();
}

QString BenchmarkDialog::block_note_text() const
{
    return linked_note_->text();
}

void BenchmarkDialog::update_command()
{
    const auto path_of = [this](QComboBox* combo)
    {
        // An angle-bracket placeholder is copy-deck style: the command is not
        // runnable until there are two ports, so it must not name a fake one.
        if (combo->count() == 0)
            return QStringLiteral("<port>");
        const auto id =
            static_cast<vss::endpoint_id>(combo->currentData().toULongLong());
        const auto port = backend_.port(id);
        if (!port)
            return QStringLiteral("<port>");
        const QString path = port->path.isEmpty() ? port->name : port->path;
        // The CLI takes a URI; add the serial scheme only when the port does
        // not already carry one.
        return path.contains(QStringLiteral("://"))
                   ? path
                   : QStringLiteral("serial://") + path;
    };
    const QString from = path_of(from_);
    const QString to = path_of(to_);
    const QString bytes = amount_->currentData().toString();
    const QString chunk = chunk_->currentData().toString();
    command_->set_text(QStringLiteral("vss_cli bench --from=%1 --to=%2 "
                                      "--bytes=%3 --chunk=%4")
                           .arg(from, to, bytes, chunk),
                       tr("Command"));
}

void BenchmarkDialog::update_state()
{
    const bool linked = ports_linked();
    linked_note_->setVisible(linked);
    const QString reason = block_reason();
    run_->setEnabled(can_run());
    run_->setToolTip(reason);
    run_->setAccessibleDescription(reason);
    copy_result_->setEnabled(has_result_);
}

void BenchmarkDialog::run()
{
    if (!can_run())
    {
        Q_EMIT message(block_reason());
        return;
    }
    // Unreachable in this build: no benchmark core is linked in.
    Q_EMIT message(tr("The benchmark command is not available in this build."));
}

} // namespace vss::gui
