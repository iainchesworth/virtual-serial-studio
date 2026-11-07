//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "support/stall_probe.hpp"

#include <vss/core/profile.hpp>

#include <QTimer>

#include <algorithm>

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <ctime>
#endif

namespace vss::gui::support
{

StallProbe::StallProbe(int interval_ms, QObject* parent)
    : QObject(parent), interval_ms_(std::max(interval_ms, 1))
{
    timer_ = new QTimer(this);
    timer_->setTimerType(Qt::PreciseTimer);
    timer_->setInterval(interval_ms_);
    connect(timer_, &QTimer::timeout, this, &StallProbe::on_tick);
}

StallProbe::~StallProbe() = default;

qint64 StallProbe::thread_cpu_ns()
{
#if defined(Q_OS_WIN)
    FILETIME created{};
    FILETIME exited{};
    FILETIME kernel{};
    FILETIME user{};
    if (GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user) ==
        0)
        return 0;
    const auto ticks = [](const FILETIME& time)
    {
        ULARGE_INTEGER value;
        value.LowPart = time.dwLowDateTime;
        value.HighPart = time.dwHighDateTime;
        return static_cast<qint64>(value.QuadPart);
    };
    // Units of 100 ns. The system counts them in scheduler ticks, so the
    // answer is good to about 16 ms.
    return (ticks(kernel) + ticks(user)) * 100;
#else
    timespec now{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now) != 0)
        return 0;
    return static_cast<qint64>(now.tv_sec) * 1'000'000'000 +
           static_cast<qint64>(now.tv_nsec);
#endif
}

void StallProbe::start()
{
    clock_.start();
    last_ms_ = 0;
    last_cpu_ns_ = thread_cpu_ns();
    timer_->start();
}

void StallProbe::stop()
{
    timer_->stop();
}

void StallProbe::reset()
{
    longest_ms_ = 0;
    longest_busy_ms_ = 0;
    ticks_ = 0;
    stalls_.clear();
    busy_.clear();
    if (timer_->isActive())
    {
        clock_.restart();
        last_ms_ = 0;
        last_cpu_ns_ = thread_cpu_ns();
    }
}

int StallProbe::stalls_over(int budget_ms) const
{
    return static_cast<int>(std::count_if(stalls_.begin(), stalls_.end(),
                                          [budget_ms](qint64 value)
                                          { return value > budget_ms; }));
}

int StallProbe::busy_over(int budget_ms) const
{
    return static_cast<int>(std::count_if(busy_.begin(), busy_.end(),
                                          [budget_ms](qint64 value)
                                          { return value > budget_ms; }));
}

void StallProbe::on_tick()
{
    const qint64 now = clock_.elapsed();
    const qint64 late = std::max<qint64>(now - last_ms_ - interval_ms_, 0);
    last_ms_ = now;
    ++ticks_;
    longest_ms_ = std::max(longest_ms_, late);
    if (late >= kMinRecorded)
        stalls_.push_back(late);

    const qint64 cpu = thread_cpu_ns();
    const qint64 busy = std::max<qint64>((cpu - last_cpu_ns_) / 1'000'000, 0);
    last_cpu_ns_ = cpu;
    longest_busy_ms_ = std::max(longest_busy_ms_, busy);
    if (busy >= kMinRecorded)
        busy_.push_back(busy);

    VSS_PROFILE_PLOT("gui.stall_ms", late);
    VSS_PROFILE_PLOT("gui.busy_ms", busy);
}

} // namespace vss::gui::support
