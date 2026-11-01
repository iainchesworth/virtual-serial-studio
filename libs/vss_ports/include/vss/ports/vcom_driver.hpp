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

/// True when the Virtual Serial Studio COM port driver is installed.
///
/// A Windows `COMx` is a kernel device: no user-mode process can create one,
/// so a real drop-in replacement for com0com needs this driver.  Detection is
/// a cheap registry probe and is safe to call whenever a dialog refreshes.
bool driver_available();

/// Version reported by the installed driver, or an empty string.
std::string driver_version();

/// Creates a COM port through the driver and returns the transport that
/// carries bytes between the hub and that port.
///
/// Returns `errc::unsupported` when the driver is absent.  The client side is
/// the next milestone; [docs/windows-com-driver.md](../../../docs/windows-com-driver.md)
/// defines the contract it will speak.
result<virtual_port_result> create_driver_port(
    const transport::executor_type& executor, const std::string& name);

} // namespace vss

#endif // VSS_PORTS_VCOM_DRIVER_HPP
