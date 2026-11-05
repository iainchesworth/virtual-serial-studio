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

/// True when the Virtual Serial Studio COM port driver is installed and has at
/// least one port.
///
/// A Windows `COMx` is a kernel device: no user-mode process can create one,
/// so a real drop-in replacement for com0com needs this driver.  Detection
/// lists the driver's device interfaces without opening any of them, so it is
/// safe to call whenever a dialog refreshes.
bool driver_available();

/// The driver's interface version, or an empty string.  It opens a free port
/// for a moment to ask, so it is empty when every port is taken.
std::string driver_version();

/// Takes a free port of the driver and returns the transport that carries
/// bytes between the hub and that port.  `path` is the COM name an application
/// opens.
///
/// The ports are the devices that exist; this does not create one.  Returns
/// `errc::unsupported` when the driver is absent and `errc::device_busy` when
/// every port is in use.  See
/// [drivers/windows/README.md](../../../../drivers/windows/README.md) for the
/// two ends of a port.
result<virtual_port_result> create_driver_port(
    const transport::executor_type& executor, const std::string& name);

} // namespace vss

#endif // VSS_PORTS_VCOM_DRIVER_HPP
