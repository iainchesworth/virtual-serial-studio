//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_PORTS_SERIAL_ENUMERATION_HPP
#define VSS_PORTS_SERIAL_ENUMERATION_HPP

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vss {

/// One end of a com0com pair, parsed from a PnP instance id.
struct com0com_end
{
    int pair = 0;
    /// `"A"` or `"B"`.
    std::string side;
};

/// `"USB Serial Device (COM13)"` -> `"COM13"`; empty when the description
/// does not end in a COM port name.
std::string parse_com_port_name(std::string_view description);

/// `"COM0COM\\PORT\\CNCA1"` -> pair 1, side `"A"`; `nullopt` otherwise.
std::optional<com0com_end> parse_com0com_instance_id(
    std::string_view instance_id);

/// A serial device present on this machine.
struct serial_device_info
{
    /// `COM3` on Windows, `/dev/ttyUSB0` on POSIX.
    std::string path;
    /// Friendly name, e.g. `com0com - serial port emulator (COM3)`.
    std::string description;
    /// PnP hardware id, when the platform exposes one.
    std::string hardware_id;
    /// True for ports that do not correspond to a physical UART.
    bool virtual_device = false;
    /// True when the device is one end of a com0com pair.
    bool com0com = false;
    /// com0com pair index (CNCA1 / CNCB1 -> 1), or -1.
    int com0com_pair = -1;
    /// `"A"` or `"B"` for com0com ports.
    std::string com0com_side;
};

/// Enumerates serial devices, sorted by path.
std::vector<serial_device_info> enumerate_serial_devices();

/// Convenience view used by pickers and command completion.
std::vector<std::string> enumerate_serial_port_names();

/// Removes duplicate and empty entries, keeping the first occurrence.
std::vector<serial_device_info> normalise(std::vector<serial_device_info> devices);

} // namespace vss

#endif // VSS_PORTS_SERIAL_ENUMERATION_HPP
