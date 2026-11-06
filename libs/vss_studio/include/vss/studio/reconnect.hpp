//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#pragma once

#include <chrono>

namespace vss {

/// When and how often the studio tries again to open an endpoint that connects
/// out (a `tcp://` or `rfc2217://` client) after its connection was lost.
///
/// The studio waits `first_delay`, tries, and on a failure waits twice as long
/// as the last time, up to `max_delay`.  A try that succeeds ends the
/// sequence; the next loss starts a new one from `first_delay`.  It is off by
/// default: a connection that drops is reported and stays down until
/// `studio::reopen` is called.
struct reconnect_policy
{
    bool enabled = false;
    std::chrono::milliseconds first_delay{1000};
    std::chrono::milliseconds max_delay{30000};
    /// Give up after this many failed tries in a row; 0 keeps trying.
    unsigned max_attempts = 0;

    friend bool operator==(const reconnect_policy&,
                           const reconnect_policy&) = default;
};

/// How long to wait before try number `attempt` (0 for the first), for a
/// policy: `first_delay` doubled `attempt` times, never more than `max_delay`
/// and never less than a millisecond.
std::chrono::milliseconds backoff_delay(const reconnect_policy& policy,
                                        unsigned attempt) noexcept;

} // namespace vss
