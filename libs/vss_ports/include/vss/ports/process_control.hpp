//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#pragma once

#include <vss/core/error.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace vss {

/// What can be said about a process by its number.
struct process_summary
{
    std::uint32_t id = 0;
    /// The name of the program's file, such as `pwrcntr.exe`; empty when
    /// the system will not say.
    std::string name;
    /// The process runs as the user who runs this one.  False for another
    /// user's process, and for one whose owner cannot be read.
    bool own_user = false;
    /// `terminate_process` would try to end it: the process is the user's own,
    /// or this process runs elevated.
    bool can_end = false;

    friend bool operator==(const process_summary&,
                           const process_summary&) = default;
};

/// Looks a process up.  Empty when there is no such process, or this system
/// has nothing to ask (only Windows, where the driver names the process that
/// has a port open).
std::optional<process_summary> describe_process(std::uint32_t id);

/// Ends a process, as Task Manager's End task does: the program gets no chance
/// to save anything.
///
/// Only a process of the user who runs this one is ended.  Another user's
/// process is ended only when this process runs elevated, so a request never
/// does more than the user could do alone: `errc::permission_denied`
/// otherwise, and nothing is tried.  `errc::not_found` when there is no such
/// process, `errc::invalid_argument` for process 0 and for this process
/// itself, and `errc::unsupported` where the system has nothing to ask.
result<void> terminate_process(std::uint32_t id);

} // namespace vss

