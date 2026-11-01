//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_CORE_SERIAL_SETTINGS_HPP
#define VSS_CORE_SERIAL_SETTINGS_HPP

#include <vss/core/error.hpp>

#include <chrono>
#include <string>
#include <string_view>

namespace vss {

enum class parity_mode
{
    none,
    odd,
    even,
    mark,
    space
};

enum class stop_bits_mode
{
    one,
    one_point_five,
    two
};

enum class flow_control_mode
{
    none,
    software,
    hardware
};

/// Framing and timing for one serial endpoint.
///
/// The studio owns this description and the transport layer converts it to
/// whatever the backend understands: a real UART, an RFC 2217 SET-* sequence,
/// or the stored options of a virtual port.
struct serial_settings
{
    unsigned baud_rate = 115200;
    unsigned data_bits = 8;
    parity_mode parity = parity_mode::none;
    stop_bits_mode stop_bits = stop_bits_mode::one;
    flow_control_mode flow_control = flow_control_mode::none;
    std::chrono::milliseconds read_timeout{0};
    std::chrono::milliseconds write_timeout{0};
    bool exclusive = false;

    /// Bytes on the wire per second, including start, parity and stop bits.
    double characters_per_second() const noexcept;
    /// Time one character occupies at the configured line rate.
    std::chrono::nanoseconds character_duration() const noexcept;

    friend bool operator==(const serial_settings&, const serial_settings&) = default;
};

error_code validate(const serial_settings& settings) noexcept;

const char* to_string(parity_mode value) noexcept;
const char* to_string(stop_bits_mode value) noexcept;
const char* to_string(flow_control_mode value) noexcept;

result<parity_mode> parse_parity(std::string_view text);
result<stop_bits_mode> parse_stop_bits(std::string_view text);
result<flow_control_mode> parse_flow_control(std::string_view text);

/// Renders the classic `115200 8N1` label.
std::string to_label(const serial_settings& settings);

} // namespace vss

#endif // VSS_CORE_SERIAL_SETTINGS_HPP
