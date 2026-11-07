//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#pragma once

#include <QApplication>

namespace vss::gui
{

/// A `QApplication` that makes slow work visible without hand-placed zones.
///
/// While the profiler records, every event the application dispatches gets a
/// `gui.event` zone whose text names the receiver class and the event type
/// (`QPushButton / MouseButtonPress`), so a Tracy timeline shows who spent
/// the time. A queued signal arrives as a `MetaCall` event, which is how the
/// cost of a cross-thread signal-to-slot delivery shows up; a direct
/// connection runs inside the zone of the event that emitted it.
///
/// An event that takes longer than its budget (16 ms for input and painting,
/// 50 ms for the rest) also writes a `gui.slow_event` message and a point on
/// the `gui.event_ms` plot. The top-level paint of a window marks a
/// `gui.paint` frame, and every pass of the event loop marks the main frame
/// plus a `gui.loop` busy period.
///
/// While the profiler is dormant the cost is one relaxed load per event.
class ProfiledApplication : public QApplication
{
public:
    using QApplication::QApplication;

    bool notify(QObject* receiver, QEvent* event) override;

    /// Marks the event-loop frames on the calling thread's dispatcher. Call
    /// it once the application exists, from the GUI thread.
    static void install_loop_marks();

    /// How long an event of this type may take before it is reported.
    static int budget_ms(int event_type) noexcept;
};

} // namespace vss::gui
