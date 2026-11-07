//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <vss/core/bytes.hpp>
#include <vss/core/profile.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iterator>

namespace vss {
namespace {

int hex_digit(char value) noexcept
{
    if (value >= '0' && value <= '9')
        return value - '0';
    if (value >= 'a' && value <= 'f')
        return value - 'a' + 10;
    if (value >= 'A' && value <= 'F')
        return value - 'A' + 10;
    return -1;
}

bool is_separator(char value) noexcept
{
    switch (value)
    {
    case ' ':
    case '\t':
    case '\n':
    case '\r':
    case '\v':
    case '\f':
    case ',':
    case ':':
    case '-':
    case '_':
        return true;
    default:
        return false;
    }
}

std::string_view trim(std::string_view text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.remove_suffix(1);
    return text;
}

} // namespace

byte_buffer to_bytes(std::string_view text)
{
    return {reinterpret_cast<const byte*>(text.data()),
            reinterpret_cast<const byte*>(text.data()) + text.size()};
}

std::string to_string(bytes_view data)
{
    return {reinterpret_cast<const char*>(data.data()), data.size()};
}

std::string to_hex(bytes_view data, char separator)
{
    VSS_PROFILE_ZONE("core.to_hex");
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(data.size() * (separator == '\0' ? 2 : 3));
    for (std::size_t i = 0; i < data.size(); ++i)
    {
        if (separator != '\0' && i != 0)
            out.push_back(separator);
        out.push_back(digits[(data[i] >> 4) & 0x0f]);
        out.push_back(digits[data[i] & 0x0f]);
    }
    return out;
}

result<byte_buffer> from_hex(std::string_view text)
{
    VSS_PROFILE_ZONE("core.from_hex");
    byte_buffer out;
    int high = -1;

    std::string_view input = trim(text);
    if (input.size() >= 2 && input[0] == '0' && (input[1] == 'x' || input[1] == 'X'))
    {
        input.remove_prefix(2);
        if (input.empty())
            return make_error_code(errc::invalid_argument);
    }

    for (char value : input)
    {
        if (is_separator(value))
        {
            if (high >= 0)
                return make_error_code(errc::invalid_argument);
            continue;
        }
        const int digit = hex_digit(value);
        if (digit < 0)
            return make_error_code(errc::invalid_argument);
        if (high < 0)
        {
            high = digit;
        }
        else
        {
            out.push_back(static_cast<byte>((high << 4) | digit));
            high = -1;
        }
    }

    if (high >= 0)
        return make_error_code(errc::invalid_argument);
    return out;
}

std::string to_printable(bytes_view data, char placeholder)
{
    VSS_PROFILE_ZONE("core.to_printable");
    std::string out;
    out.reserve(data.size());
    for (byte value : data)
    {
        if (value == '\r')
            out.append("\\r");
        else if (value == '\n')
            out.append("\\n");
        else if (value == '\t')
            out.append("\\t");
        else if ((value >= 0x20 && value < 0x7f) || value >= 0x80)
            out.push_back(static_cast<char>(value)); // or assume UTF-8
        else
            out.push_back(placeholder);
    }
    return out;
}

result<std::uint64_t> parse_byte_size(std::string_view text)
{
    VSS_PROFILE_ZONE("core.parse_byte_size");
    std::string_view input = trim(text);
    if (input.empty())
        return make_error_code(errc::invalid_argument);

    std::size_t digits = 0;
    bool seen_dot = false;
    while (digits < input.size())
    {
        const char value = input[digits];
        if (value == '.' && !seen_dot)
        {
            seen_dot = true;
            ++digits;
            continue;
        }
        if (!std::isdigit(static_cast<unsigned char>(value)))
            break;
        ++digits;
    }

    if (digits == 0)
        return make_error_code(errc::invalid_argument);

    // strtod rather than floating point from_chars: the latter is still
    // missing from some AppleClang standard libraries.
    const std::string number_text(input.substr(0, digits));
    char* end = nullptr;
    const double number = std::strtod(number_text.c_str(), &end);
    if (end == number_text.c_str() || number < 0.0)
        return make_error_code(errc::invalid_argument);

    std::string suffix;
    for (char value : input.substr(digits))
    {
        if (std::isspace(static_cast<unsigned char>(value)))
            continue;
        suffix.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(value))));
    }

    std::uint64_t multiplier = 1;
    if (suffix.empty() || suffix == "b")
        multiplier = 1;
    else if (suffix == "k" || suffix == "kb" || suffix == "kib")
        multiplier = 1024ull;
    else if (suffix == "m" || suffix == "mb" || suffix == "mib")
        multiplier = 1024ull * 1024ull;
    else if (suffix == "g" || suffix == "gb" || suffix == "gib")
        multiplier = 1024ull * 1024ull * 1024ull;
    else
        return make_error_code(errc::invalid_argument);

    const double scaled = number * static_cast<double>(multiplier);
    if (!std::isfinite(scaled) || scaled > 1.8e19)
        return make_error_code(errc::invalid_argument);
    return static_cast<std::uint64_t>(scaled);
}

std::string format_byte_size(std::uint64_t bytes)
{
    static constexpr const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < std::size(units))
    {
        value /= 1024.0;
        ++unit;
    }

    char buffer[64];
    if (unit == 0)
        std::snprintf(buffer, sizeof(buffer), "%llu B",
                      static_cast<unsigned long long>(bytes));
    else
        std::snprintf(buffer, sizeof(buffer), "%.1f %s", value, units[unit]);
    return buffer;
}

} // namespace vss
