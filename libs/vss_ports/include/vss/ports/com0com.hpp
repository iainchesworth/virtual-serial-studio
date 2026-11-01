//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_PORTS_COM0COM_HPP
#define VSS_PORTS_COM0COM_HPP

#include <vss/core/error.hpp>
#include <vss/ports/serial_enumeration.hpp>

#include <string>
#include <vector>

namespace vss {

/// One com0com null-modem pair: two real COM ports joined by the driver.
struct com0com_pair
{
    int index = 0;
    std::string a;
    std::string b;
};

struct com0com_installation
{
    bool found = false;
    std::string install_dir;
    std::string setupc_path;
    std::vector<com0com_pair> pairs;
};

/// Detects a com0com installation and the pairs it already exposes.
///
/// Detection uses the registry and the serial port list, so it needs no
/// administrator rights.  Creating or removing a pair does, because
/// `setupc.exe` carries an elevation manifest.
com0com_installation detect_com0com();

/// Groups the com0com ends found by #enumerate_serial_devices into pairs.
std::vector<com0com_pair> group_com0com_pairs(
    const std::vector<serial_device_info>& devices);

/// Creates a pair.  With `request_elevation` the user gets the platform's
/// elevation prompt; without it the call fails with `errc::permission_denied`
/// when the process is not already elevated.
result<void> create_com0com_pair(const com0com_installation& installation,
                                 std::string_view port_a, std::string_view port_b,
                                 bool request_elevation);

/// Removes the pair with `index`.
result<void> remove_com0com_pair(const com0com_installation& installation,
                                 int index, bool request_elevation);

} // namespace vss

#endif // VSS_PORTS_COM0COM_HPP
