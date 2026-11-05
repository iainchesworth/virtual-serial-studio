//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#pragma once

// Waiting for a condition in a Qt test, instead of waiting for a time.
//
// A test that sleeps for a fixed time and then asserts passes on a fast
// machine and fails on a loaded one, because it assumes how long another
// thread or the window system takes. Wait for the thing itself. The deadline
// is long on purpose: it only costs time when something is really wrong.

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>

namespace vss_test
{

/// Runs the event loop for about `milliseconds`. Use it only to give other
/// threads a turn between checks, never as a wait before an assert.
inline void spin(int milliseconds)
{
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

/// Runs the event loop until `condition` is true or `timeout_ms` has passed.
/// Returns the last answer of `condition`, so `REQUIRE(wait_until(...))` fails
/// at the line that was waiting.
template <class Condition>
bool wait_until(Condition&& condition, int timeout_ms = 15000)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!condition())
    {
        if (elapsed.hasExpired(timeout_ms))
            return condition();
        spin(1);
    }
    return true;
}

} // namespace vss_test
