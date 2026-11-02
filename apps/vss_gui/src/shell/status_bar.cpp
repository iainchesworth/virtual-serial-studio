//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "shell/status_bar.hpp"

#include "shell/format.hpp"

#include <QSignalBlocker>

#include <algorithm>

namespace vss::gui
{
namespace
{

QString platform_name()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#else
    return QStringLiteral("linux");
#endif
}

} // namespace

StatusBar::StatusBar(StudioBackend& backend, QWidget* parent)
    : QStatusBar(parent), backend_(backend)
{
    setSizeGripEnabled(false);
    setAccessibleName(QStringLiteral("Status bar"));
    rate_timer_.start();
    last_ns_ = rate_timer_.nsecsElapsed();

    ports_ = new QLabel(this);
    links_ = new QLabel(this);
    rates_ = new QLabel(this);
    hub_ = new QLabel(this);
    for (QLabel* label : {ports_, links_, rates_, hub_})
    {
        label->setFont(Theme::instance().font(text_style::caption));
        label->setContentsMargins(0, 0, 12, 0);
        addWidget(label, 0);
    }

    dropped_ = new StatusChip(QStringLiteral("0 dropped"),
                              StatusChip::kind::waiting, this);
    dropped_->set_small(true);
    dropped_->hide();
    addWidget(dropped_, 0);

    readiness_ = new StatusChip(QStringLiteral("Setup ready"),
                                StatusChip::kind::healthy, this);
    addPermanentWidget(readiness_, 0);

    level_ = new SegmentedControl(this);
    level_->set_compact(true);
    level_->setAccessibleName(QStringLiteral("Interface mode"));
    level_->add_item(QStringLiteral("Simple"));
    level_->add_item(QStringLiteral("Advanced"), 1);
    connect(level_, &SegmentedControl::current_changed, this,
            [this](int id) { Q_EMIT level_requested(id == 1); });
    addPermanentWidget(level_, 0);

    refresh();
}

void StatusBar::set_advanced(bool advanced)
{
    advanced_ = advanced;
    const QSignalBlocker blocker(level_);
    level_->set_current(advanced ? 1 : 0);
    dropped_->setVisible(advanced &&
                         dropped_->get_kind() == StatusChip::kind::waiting &&
                         !dropped_->text().isEmpty());
    refresh();
}

void StatusBar::refresh()
{
    const auto ports = backend_.ports();
    const auto links = backend_.links();

    std::uint64_t rx = 0;
    std::uint64_t tx = 0;
    std::uint64_t dropped = 0;
    for (const auto& port : ports)
    {
        rx += port.stats.bytes_rx;
        tx += port.stats.bytes_tx;
        dropped += port.stats.dropped;
    }
    for (const auto& link : links)
        dropped += link.stats.dropped;

    const qint64 now = rate_timer_.nsecsElapsed();
    const double seconds =
        static_cast<double>(now - last_ns_) / 1'000'000'000.0;
    last_ns_ = now;
    double rx_rate = 0.0;
    double tx_rate = 0.0;
    if (seconds > 0.0)
    {
        rx_rate = static_cast<double>(rx - previous_rx_) / seconds;
        tx_rate = static_cast<double>(tx - previous_tx_) / seconds;
    }
    previous_rx_ = rx;
    previous_tx_ = tx;

    const bool empty = ports.empty();
    if (empty)
        ports_->setText(QStringLiteral("No ports yet"));
    else
        ports_->setText(QStringLiteral("Ports %1")
                            .arg(static_cast<qulonglong>(ports.size())));
    links_->setText(
        QStringLiteral("Links %1").arg(static_cast<qulonglong>(links.size())));
    rates_->setText(QStringLiteral("RX %1   TX %2")
                        .arg(format_rate(rx_rate), format_rate(tx_rate)));
    hub_->setText(backend_.hub_running() ? QStringLiteral("Hub running")
                                         : QStringLiteral("Hub stopped"));
    links_->setVisible(!empty);
    rates_->setVisible(!empty);
    hub_->setVisible(!empty);

    if (advanced_ && dropped > 0)
    {
        dropped_->set_text(
            QStringLiteral("%1 dropped").arg(format_bytes(dropped)));
        dropped_->show();
    }
    else
    {
        dropped_->hide();
    }

    update_readiness();
}

void StatusBar::update_readiness()
{
    const setup_facts facts = backend_.facts();
    const bool windows = platform_name() == QLatin1String("windows");
    if (!windows || facts.com0com_found)
    {
        readiness_->set_kind(StatusChip::kind::healthy);
        readiness_->set_text(QStringLiteral("Setup ready"));
    }
    else
    {
        readiness_->set_kind(StatusChip::kind::waiting);
        readiness_->set_text(QStringLiteral("Setup needs attention"));
    }
    readiness_->setToolTip(
        QStringLiteral("Open Tools, Setup check for the details."));
}

} // namespace vss::gui
