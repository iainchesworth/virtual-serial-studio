//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <vss/core/control_lines.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <string>

namespace vss {
namespace {

struct line_name
{
    control_line line;
    const char* short_name;
    const char* long_name;
};

constexpr line_name line_names[] = {
    {control_line::dtr, "dtr", "dtr"},
    {control_line::rts, "rts", "rts"},
    {control_line::cts, "cts", "cts"},
    {control_line::dsr, "dsr", "dsr"},
    {control_line::dcd, "dcd", "dcd"},
    {control_line::ri, "ri", "ri"},
    {control_line::break_signal, "brk", "break"},
};

std::string lower(std::string_view text)
{
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return out;
}

} // namespace

std::string to_string(control_lines lines)
{
    if (lines.bits == 0)
        return "none";

    const auto append = [](std::string& out, const char* name) {
        if (!out.empty())
            out.push_back(' ');
        out.append(name);
    };

    std::string outputs;
    if (lines.dtr())
        append(outputs, "DTR");
    if (lines.rts())
        append(outputs, "RTS");
    if (lines.break_active())
        append(outputs, "BREAK");

    std::string inputs;
    if (lines.cts())
        append(inputs, "CTS");
    if (lines.dsr())
        append(inputs, "DSR");
    if (lines.dcd())
        append(inputs, "DCD");
    if (lines.ri())
        append(inputs, "RI");

    if (inputs.empty())
        return outputs;
    if (outputs.empty())
        return "| " + inputs;
    return outputs + " | " + inputs;
}

result<control_lines> parse_control_lines(std::string_view text)
{
    const std::string lowered = lower(text);
    std::string_view input = lowered;

    if (input == "none" || input.empty())
        return control_lines{};

    if (input.size() > 2 && input[0] == '0' && input[1] == 'x')
    {
        unsigned value = 0;
        const auto parsed = std::from_chars(input.data() + 2,
                                            input.data() + input.size(), value, 16);
        if (parsed.ec != std::errc{} || parsed.ptr != input.data() + input.size())
            return make_error_code(errc::invalid_argument);
        if (value >= (1u << static_cast<unsigned>(control_line::count)))
            return make_error_code(errc::invalid_argument);
        return control_lines::from_bits(value);
    }

    control_lines result;
    std::size_t position = 0;
    while (position < input.size())
    {
        while (position < input.size()
               && (input[position] == ',' || input[position] == '|'
                   || input[position] == ' ' || input[position] == '+'))
            ++position;
        const std::size_t start = position;
        while (position < input.size() && input[position] != ','
               && input[position] != '|' && input[position] != ' '
               && input[position] != '+')
            ++position;
        if (start == position)
            continue;

        const std::string_view token = input.substr(start, position - start);
        bool matched = false;
        for (const auto& entry : line_names)
        {
            if (token == entry.short_name || token == entry.long_name)
            {
                result.set(entry.line, true);
                matched = true;
                break;
            }
        }
        if (!matched)
            return make_error_code(errc::invalid_argument);
    }

    return result;
}

} // namespace vss
