//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_PORTS_VIRTUAL_PORT_HPP
#define VSS_PORTS_VIRTUAL_PORT_HPP

#include <vss/core/error.hpp>
#include <vss/core/transport.hpp>

#include <memory>
#include <string>
#include <vector>

namespace vss {

/// The kinds of OS visible virtual port this build can create.
enum class virtual_port_kind
{
    /// A pseudo terminal pair (`/dev/pts/N`).  POSIX only.
    pty,
    /// A real `COMx` created by the Virtual Serial Studio filter driver.
    /// The only kind that can replace com0com on Windows.
    windows_driver,
    /// A Windows named pipe `\\.\pipe\...`.  Windows only.
    named_pipe,
    /// An in-process loopback device.  Every platform.
    loopback
};

struct virtual_port_result
{
    std::shared_ptr<transport> port;
    /// What other applications open, e.g. `/dev/pts/7`, or a loopback label.
    std::string path;
    virtual_port_kind kind = virtual_port_kind::loopback;

    friend bool operator==(const virtual_port_result&,
                           const virtual_port_result&) = default;
};

/// True when the platform can create this kind of port.
bool supports(virtual_port_kind kind) noexcept;

/// Kinds this build can create, best first.
std::vector<virtual_port_kind> available_kinds();

const char* to_string(virtual_port_kind kind) noexcept;

/// Creates a virtual port.
///
/// The returned transport is already open and ready for reads.  For `pty`
/// the slave side is held open by the studio so that the master never sees
/// spurious end-of-file before an application connects.
/// `com_number` is for `windows_driver` only: the COM number the port should have
/// (see `create_driver_port`), 0 for whichever Windows picks.  Other kinds have no
/// COM numbers and ignore it.
result<virtual_port_result> create_virtual_port(
    const transport::executor_type& executor, virtual_port_kind kind,
    const std::string& name, unsigned com_number = 0);

/// Creates the best available kind for this platform.
result<virtual_port_result> create_best_virtual_port(
    const transport::executor_type& executor, const std::string& name,
    unsigned com_number = 0);

} // namespace vss

#endif // VSS_PORTS_VIRTUAL_PORT_HPP
