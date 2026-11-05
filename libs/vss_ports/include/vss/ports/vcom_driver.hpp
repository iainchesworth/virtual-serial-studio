//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_PORTS_VCOM_DRIVER_HPP
#define VSS_PORTS_VCOM_DRIVER_HPP

#include <vss/core/error.hpp>
#include <vss/ports/virtual_port.hpp>

#include <string>

namespace vss {

/// True when the Virtual Serial Studio COM port driver is installed: its
/// control device is there, or at least one port is.
///
/// A Windows `COMx` is a kernel device: no user-mode process can create one,
/// so a real drop-in replacement for com0com needs this driver.  Detection
/// lists the driver's device interfaces without opening any of them, so it is
/// safe to call whenever a dialog refreshes.
bool driver_available();

/// The driver's port interface version, or an empty string.  With the control
/// device installed it asks that; otherwise it opens a free port for a moment,
/// so it is empty when every port is taken.
std::string driver_version();

/// Makes a port and returns the transport that carries bytes between the hub
/// and that port.  `path` is the COM name an application opens.
///
/// With the control device installed this asks it for a new port and waits,
/// usually a second or two, while Windows builds it.  The port is the caller's
/// alone and is removed when the transport closes, or when the process ends.
/// Only the user who made it can open its hub end.  Without the control device
/// it takes a free port of the ones that exist, which only an administrator can
/// open.
///
/// Returns `errc::unsupported` when the driver is absent, `errc::device_busy`
/// when there is no control device and every port is in use,
/// `errc::port_exhausted` when the control device has no slot left or the user
/// already holds 16 ports,
/// `errc::permission_denied` when the caller may not open it and
/// `errc::timeout` when the port did not appear.  See
/// [drivers/windows/README.md](../../../../drivers/windows/README.md) for the
/// two ends of a port.
result<virtual_port_result> create_driver_port(
    const transport::executor_type& executor, const std::string& name);

} // namespace vss

#endif // VSS_PORTS_VCOM_DRIVER_HPP
