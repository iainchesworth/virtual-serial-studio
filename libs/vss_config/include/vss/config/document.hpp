//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_CONFIG_DOCUMENT_HPP
#define VSS_CONFIG_DOCUMENT_HPP

#include <vss/core/error.hpp>
#include <vss/core/serial_settings.hpp>
#include <vss/core/types.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace vss::config {

/// One endpoint in a saved topology.
struct port_entry
{
    std::string name;
    /// `virtual`, `physical`, `tcp`, `udp`, `rfc2217` or `mock`.
    std::string kind = "virtual";
    /// Backend URI for everything except `virtual` and `mock`.
    std::string uri;
    /// For `virtual`: `auto`, `pty`, `named-pipe` or `loopback`.
    std::string virtual_kind = "auto";
    /// For `mock`: the fixture's name.
    std::string mock_target = "scripted";
    serial_settings serial;
    bool capture = true;
    /// For `virtual` ports of kind `auto` or `windows-driver`: the COM number the
    /// Windows driver gives the port, 1 to 255.  0 leaves it to Windows, which
    /// takes the lowest free number.  Other systems and other kinds of port have no
    /// COM numbers and ignore it.
    unsigned com_number = 0;

    friend bool operator==(const port_entry&, const port_entry&) = default;
};

/// One link between ports.
struct link_entry
{
    /// `duplex`, `split`, `join`, `redirect` or `monitor`.
    std::string mode = "duplex";
    std::vector<std::string> sources;
    std::vector<std::string> targets;
    bool emulate_modem_lines = true;
    merge_policy policy = merge_policy::interleave;

    friend bool operator==(const link_entry&, const link_entry&) = default;
};

/// A complete saved topology.
struct document
{
    std::string name = "untitled";
    std::string description;
    std::vector<port_entry> ports;
    std::vector<link_entry> links;

    friend bool operator==(const document&, const document&) = default;
};

/// Serialises to a stable, human editable JSON shape.
result<std::string> to_json(const document& value, bool pretty = true);

result<document> from_json(std::string_view text);

/// Checks names, kinds, URIs and link arity.
error_code validate(const document& value);

/// Reads a topology file. A missing path gives `not_found`; a path that is not
/// a regular file (a directory, a device, a pipe) gives `invalid_argument`.
result<document> load_file(const std::string& path);
result<void> save_file(const std::string& path, const document& value,
                       bool pretty = true);

/// Maps a document port kind to the endpoint kind it creates.
result<endpoint_kind> to_endpoint_kind(std::string_view kind);

/// The numbers a COM port may be given.
constexpr unsigned min_com_number = 1;
constexpr unsigned max_com_number = 255;

constexpr const char* port_kinds[] = {"virtual", "physical", "tcp", "udp",
                                      "rfc2217", "mock"};
constexpr const char* virtual_port_kinds[] = {"auto", "pty", "named-pipe",
                                              "loopback", "windows-driver"};
constexpr const char* link_modes[] = {"duplex", "split", "join", "redirect",
                                      "monitor"};

} // namespace vss::config

#endif // VSS_CONFIG_DOCUMENT_HPP
