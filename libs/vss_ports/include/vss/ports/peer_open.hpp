//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_PORTS_PEER_OPEN_HPP
#define VSS_PORTS_PEER_OPEN_HPP

#include <string>
#include <vector>

namespace vss
{

/// Whether a program other than this one has a device open.
enum class peer_open_state
{
    /// The platform cannot tell.
    unknown,
    /// No other process this one can inspect has the device open.
    not_open,
    /// Another process has the device open.
    open
};

/// True when this platform can tell whether another program has a pseudo
/// terminal or a driver port open.  Linux can, by looking at the descriptors of
/// other processes.  Windows can for the ports of the Virtual Serial Studio
/// driver, which tells the studio what program has the COM port open (a path
/// is the COM name, such as `COM7`; any other COM port reads as `unknown`) and
/// is true when that driver is installed.  macOS cannot yet, and answers
/// `unknown`.
bool peer_open_supported() noexcept;

/// For each device path, whether another process has it open.  The process
/// table is read once for the whole list, so ask for every port together.  The
/// result has one entry per path, in order; every entry is `unknown` where
/// `peer_open_supported()` is false or the process table cannot be read.
///
/// "Another process" means a process whose descriptors this one may read, so a
/// program run by another user, or a privileged one, can be missed.  A
/// pseudo terminal slave is owned by the user who made it, so the programs that
/// open it are almost always that user's.
std::vector<peer_open_state> peer_open(const std::vector<std::string>& paths);

} // namespace vss

#endif // VSS_PORTS_PEER_OPEN_HPP
