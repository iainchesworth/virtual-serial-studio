//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QVector>

class QTimer;

namespace vss::gui::support
{

/// Measures how long the GUI thread goes without returning to its event loop.
///
/// A timer asks to fire every `interval_ms`. When a slot, a model reset or a
/// layout holds the thread, the timer fires late, and the lateness is the
/// stall that a person sees as a frozen window. The probe lives on the thread
/// it is created on and measures that thread.
///
/// Two numbers are kept for every stall. The wall-clock lateness is what the
/// person sees, and it grows when the computer is busy with other programs.
/// The thread's own CPU time over the same stretch is the work the window did
/// itself, which does not depend on what else the computer is doing; a test
/// that must give the same answer on a loaded build machine asks for that one
/// (`busy_over`).
///
/// Profiling builds also plot each stall, so a Tracy timeline shows which zone
/// was running under it.
class StallProbe : public QObject
{
    Q_OBJECT

public:
    explicit StallProbe(int interval_ms = 10, QObject* parent = nullptr);
    ~StallProbe() override;

    void start();
    void stop();
    /// Forgets what was measured; keeps running.
    void reset();

    /// The longest stall in wall-clock milliseconds.
    qint64 longest_ms() const noexcept { return longest_ms_; }
    /// The most CPU time, in milliseconds, the GUI thread used between two
    /// ticks of the probe.
    qint64 longest_busy_ms() const noexcept { return longest_busy_ms_; }
    /// How many stalls were longer than `budget_ms` on the wall clock.
    int stalls_over(int budget_ms) const;
    /// How many stretches between ticks used more than `budget_ms` of CPU.
    int busy_over(int budget_ms) const;
    /// How many times the timer fired.
    int ticks() const noexcept { return ticks_; }
    /// Every stall of at least `kMinRecorded` ms, in the order they happened.
    const QVector<qint64>& stalls() const noexcept { return stalls_; }

    static constexpr qint64 kMinRecorded = 5;

    /// CPU time the calling thread has used, in nanoseconds; zero where the
    /// platform cannot say. One file per platform in `support/platform`.
    static qint64 thread_cpu_ns();

private:
    void on_tick();

    QTimer* timer_ = nullptr;
    QElapsedTimer clock_;
    qint64 last_ms_ = 0;
    qint64 last_cpu_ns_ = 0;
    qint64 longest_ms_ = 0;
    qint64 longest_busy_ms_ = 0;
    int ticks_ = 0;
    int interval_ms_ = 10;
    QVector<qint64> stalls_;
    QVector<qint64> busy_;
};

} // namespace vss::gui::support
