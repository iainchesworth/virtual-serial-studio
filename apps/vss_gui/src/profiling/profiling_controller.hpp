//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#pragma once

#include <vss/core/profiling.hpp>

#include <QObject>
#include <QString>
#include <QTimer>

namespace vss::gui
{

/// The GUI's handle on `vss::profiling`: the one place that turns the Tracy
/// listener on and off, and the one source for what the status bar, the
/// Settings page and About say about it.
///
/// Profiling is off until a person turns it on (Settings, Performance, the
/// `--profile` option or `VSS_PROFILE=1`; Debug builds start it themselves).
/// It listens on this computer only unless the person allows other computers.
/// Nothing is sent anywhere until they connect a Tracy profiler.
class ProfilingController final : public QObject
{
    Q_OBJECT

public:
    static ProfilingController& instance();

    /// False when this build does not include the profiler.
    bool available() const noexcept;

    vss::profiling::status status() const noexcept { return status_; }

    /// Starts or resumes recording. Returns false and sets `last_error()`
    /// when it cannot.
    bool enable(bool allow_remote);

    /// Stops recording. The listener stays open until the program exits.
    void disable();

    QString last_error() const { return error_; }

    /// "127.0.0.1:8086", "all interfaces:8086", or empty before the first
    /// start.
    QString address() const;

    /// One sentence for the Settings page and About.
    QString summary() const;

    /// A short phrase for the status bar chip, empty when nothing is running.
    QString chip_text() const;

Q_SIGNALS:
    void changed();

private:
    ProfilingController();
    void poll();
    void set_status(const vss::profiling::status& next);

    vss::profiling::status status_{};
    QString error_;
    QTimer poll_;
};

} // namespace vss::gui
