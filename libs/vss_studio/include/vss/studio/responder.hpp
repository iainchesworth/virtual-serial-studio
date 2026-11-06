//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#pragma once

#include <vss/core/bytes.hpp>
#include <vss/core/endpoint.hpp>
#include <vss/core/error.hpp>
#include <vss/core/hub.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vss {

/// When bytes like `match` arrive, send `reply` after `delay` (plus or minus
/// up to `jitter`).
struct responder_rule
{
    std::string name;
    byte_buffer match;
    /// Which bits of `match` count: a 1 bit must be equal, a 0 bit is ignored.
    /// Empty means every bit counts (an exact match).  Otherwise the same size
    /// as `match`.
    byte_buffer mask;
    byte_buffer reply;
    std::chrono::milliseconds delay{0};
    std::chrono::milliseconds jitter{0};
};

/// One thing the master role sends in a cycle.
struct responder_poll_step
{
    byte_buffer send;
    /// From the start of the cycle.
    std::chrono::milliseconds at{0};
    std::chrono::milliseconds jitter{0};
};

/// A timed list a master sends, once or over and over.
struct responder_poll
{
    /// From the start of the responder to the first cycle.
    std::chrono::milliseconds start{0};
    /// From the start of one cycle to the start of the next.  Zero means one
    /// cycle only.
    std::chrono::milliseconds interval{0};
    /// How many cycles; zero means until stopped (or one, with no interval).
    std::uint64_t cycles = 0;
    std::vector<responder_poll_step> steps;
};

/// The behaviour of a scripted device or master.  A script lives in its own
/// JSON file, apart from the topology.
///
/// ```json
/// {
///   "version": 1,
///   "seed": 7,
///   "rules": [
///     { "name": "status", "match": "02 31", "mask": "ff f0",
///       "reply": "02 31 41 03", "delay_ms": 150, "jitter_ms": 10 }
///   ],
///   "poll": {
///     "start_ms": 100, "interval_ms": 1000, "cycles": 0,
///     "steps": [ { "send": "02 31 03", "at_ms": 0 },
///                { "send": "02 32 03", "at_ms": 250, "jitter_ms": 5 } ]
///   }
/// }
/// ```
///
/// A script that has a `rules` list makes the port a device, one with `poll`
/// makes it a master, and one with both does both.
struct responder_script
{
    std::vector<responder_rule> rules;
    std::optional<responder_poll> poll;
    /// Seed for the jitter, so a run can be repeated.  Unset picks one.
    std::optional<std::uint64_t> seed;
};

/// `errc::config_error` for text that is not a valid script, with the reason in
/// `detail` when it is given.  Unknown keys are errors, so a misspelt
/// `delay_ms` is found and not ignored.
result<responder_script> parse_responder_script(std::string_view text,
                                                std::string* detail = nullptr);

result<responder_script> load_responder_script(const std::string& path,
                                               std::string* detail = nullptr);

/// Makes an endpoint act as a device (and, with a poll list, as a master).
///
/// The bytes that arrive on the endpoint are matched against the rules, and the
/// replies are written to it with `endpoint::write`.  So, like the replay
/// player, it stands in for a program on the far end of a port: use it on the
/// end that a program would open, or on a mock or other endpoint a test owns.
///
/// Bytes are matched as a stream, so a pattern that arrives in two reads is
/// found.  After a match the bytes up to its end are used up.  When several
/// rules match, the one whose match starts first wins, and then the one listed
/// first.  A reply is timed from the moment its trigger arrived, not from when
/// the responder got to look at it.
class responder
{
public:
    /// `errc::invalid_argument` for a missing endpoint or a script with no rules
    /// and no poll list.
    static result<std::shared_ptr<responder>> start(
        hub& engine, std::shared_ptr<endpoint> target,
        const responder_script& script);

    ~responder();

    responder(const responder&) = delete;
    responder& operator=(const responder&) = delete;

    void stop();

    /// Triggers found.
    std::uint64_t matches() const;
    /// Replies written (a reply that is still waiting for its time is not
    /// counted).
    std::uint64_t replies_sent() const;
    /// Poll steps written.
    std::uint64_t polls_sent() const;
    /// True when a poll list with a limited number of cycles has finished.
    bool poll_finished() const;

private:
    struct state;
    explicit responder(std::shared_ptr<state> shared);

    std::shared_ptr<state> state_;
};

} // namespace vss

