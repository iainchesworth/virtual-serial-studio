//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_CORE_BYTES_HPP
#define VSS_CORE_BYTES_HPP

#include <vss/core/error.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vss {

using byte = std::uint8_t;
using byte_buffer = std::vector<byte>;
using bytes_view = std::span<const byte>;
using mutable_bytes_view = std::span<byte>;

inline bytes_view as_bytes(std::string_view text) noexcept
{
    return {reinterpret_cast<const byte*>(text.data()), text.size()};
}

byte_buffer to_bytes(std::string_view text);
std::string to_string(bytes_view data);

/// `"48 65 6c"` style dump.  An empty separator produces `"48656c"`.
std::string to_hex(bytes_view data, char separator = ' ');

/// Parses hex with optional separators (`"48 65"`, `"48:65"`, `"0x48"`).
result<byte_buffer> from_hex(std::string_view text);

/// Replaces control bytes with `.` while leaving printable ASCII and UTF-8
/// continuation bytes intact.
std::string to_printable(bytes_view data, char placeholder = '.');

/// Parses `"64"`, `"4KB"`, `"10MB"`, `"1GiB"` (binary multiples, case
/// insensitive, optional space).
result<std::uint64_t> parse_byte_size(std::string_view text);

/// Renders a byte count as `"1.5 MB"`.
std::string format_byte_size(std::uint64_t bytes);

} // namespace vss

#endif // VSS_CORE_BYTES_HPP
