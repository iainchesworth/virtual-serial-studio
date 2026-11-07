//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "profiling/profiling_controller.hpp"

#include <QCoreApplication>

namespace vss::gui
{
namespace
{

bool same(const vss::profiling::status& a, const vss::profiling::status& b)
{
    return a.current == b.current && a.port == b.port && a.remote == b.remote &&
           a.profiler_connected == b.profiler_connected;
}

} // namespace

ProfilingController& ProfilingController::instance()
{
    static ProfilingController controller;
    return controller;
}

ProfilingController::ProfilingController() : status_(vss::profiling::current())
{
    // The connection state changes without the GUI being told, so look now and
    // then, but only while there is a listener to look at.
    poll_.setInterval(1000);
    connect(&poll_, &QTimer::timeout, this, [this] { poll(); });
    if (status_.current != vss::profiling::state::dormant &&
        status_.current != vss::profiling::state::not_compiled)
        poll_.start();
}

bool ProfilingController::available() const noexcept
{
    return vss::profiling::compiled_in;
}

bool ProfilingController::enable(bool allow_remote)
{
    vss::profiling::options wanted;
    wanted.allow_remote = allow_remote;
    // After the first start the port and the interface are fixed.
    if (status_.port != 0)
    {
        wanted.port = status_.port;
        wanted.allow_remote = status_.remote;
    }
    const auto result = vss::profiling::enable(wanted);
    if (!result)
    {
        const auto code = result.error();
        if (code == vss::errc::unsupported)
            error_ = QCoreApplication::translate(
                "Profiling", "This build does not include the profiler.");
        else if (code == vss::errc::device_busy ||
                 code == vss::errc::port_exhausted)
            error_ = QCoreApplication::translate(
                "Profiling",
                "No port is free for the profiler. Close the program that "
                "uses port 8086 and try again.");
        else
            error_ = QString::fromStdString(code.message());
        Q_EMIT changed();
        return false;
    }
    error_.clear();
    set_status(*result);
    poll_.start();
    return true;
}

void ProfilingController::disable()
{
    vss::profiling::disable();
    error_.clear();
    set_status(vss::profiling::current());
}

void ProfilingController::poll()
{
    set_status(vss::profiling::current());
}

void ProfilingController::set_status(const vss::profiling::status& next)
{
    if (same(status_, next))
        return;
    status_ = next;
    Q_EMIT changed();
}

QString ProfilingController::address() const
{
    if (status_.port == 0)
        return {};
    return status_.remote
               ? QCoreApplication::translate("Profiling", "all interfaces:%1")
                     .arg(status_.port)
               : QStringLiteral("127.0.0.1:%1").arg(status_.port);
}

QString ProfilingController::summary() const
{
    switch (status_.current)
    {
    case vss::profiling::state::not_compiled:
        return QCoreApplication::translate(
            "Profiling", "This build does not include the profiler.");
    case vss::profiling::state::dormant:
        return QCoreApplication::translate(
            "Profiling", "Profiling is off. Nothing is recorded and no port is "
                         "open.");
    case vss::profiling::state::recording:
        if (status_.profiler_connected)
            return QCoreApplication::translate(
                       "Profiling", "Recording. A profiler is connected to %1.")
                .arg(address());
        return status_.remote
                   ? QCoreApplication::translate(
                         "Profiling",
                         "Recording. Waiting for a profiler on %1. "
                         "Other computers can connect.")
                         .arg(address())
                   : QCoreApplication::translate(
                         "Profiling",
                         "Recording. Waiting for a profiler on %1. Only "
                         "this computer can connect.")
                         .arg(address());
    case vss::profiling::state::paused:
        return QCoreApplication::translate(
                   "Profiling",
                   "Recording is paused. %1 stays open until Studio "
                   "quits.")
            .arg(address());
    }
    return {};
}

QString ProfilingController::chip_text() const
{
    switch (status_.current)
    {
    case vss::profiling::state::recording:
        return status_.profiler_connected
                   ? QCoreApplication::translate("Profiling",
                                                 "Profiling, connected")
                   : QCoreApplication::translate("Profiling", "Profiling :%1")
                         .arg(status_.port);
    case vss::profiling::state::paused:
        return QCoreApplication::translate("Profiling", "Profiling paused");
    case vss::profiling::state::not_compiled:
    case vss::profiling::state::dormant:
        break;
    }
    return {};
}

} // namespace vss::gui
