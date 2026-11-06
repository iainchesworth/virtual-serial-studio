//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The rules for COM numbers, with nothing of Windows in them, so that a test can
// run them anywhere: which numbers a port may be given, how a COM name reads, and
// what the friendly name of a port becomes when its number changes.
//

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace vss::helper
{

/// The numbers a port may have. The same range as protocol.h; the COM name
/// database holds 256 ports until somebody resizes it.
constexpr unsigned long min_com_number = 1;
constexpr unsigned long max_com_number = 255;

constexpr bool com_number_valid(unsigned long number) noexcept
{
    return number >= min_com_number && number <= max_com_number;
}

inline std::wstring com_name(unsigned long number)
{
    return L"COM" + std::to_wstring(number);
}

/// "COM7" is 7. Anything else (another prefix, no digits, a sign, trailing
/// text, a number too large to be a port) is nothing. Not case sensitive.
inline std::optional<unsigned long> parse_com_name(std::wstring_view name)
{
    if (name.size() < 4 || name.size() > 7)
        return std::nullopt;
    const wchar_t* prefix = L"COM";
    for (std::size_t i = 0; i < 3; ++i)
    {
        const wchar_t c = name[i];
        const wchar_t upper = (c >= L'a' && c <= L'z')
                                  ? static_cast<wchar_t>(c - L'a' + L'A')
                                  : c;
        if (upper != prefix[i])
            return std::nullopt;
    }

    unsigned long number = 0;
    for (std::size_t i = 3; i < name.size(); ++i)
    {
        if (name[i] < L'0' || name[i] > L'9')
            return std::nullopt;
        number = number * 10 + static_cast<unsigned long>(name[i] - L'0');
    }
    return number;
}

/// The friendly name of a port after its number changed: "Name (COM4)" becomes
/// "Name (COM9)". A name that does not end in a bracketed COM name gets one added,
/// which is what the Ports class installer would have made of it.
inline std::wstring friendly_name_for(std::wstring_view current,
                                      unsigned long number)
{
    const std::wstring tail = L"(" + com_name(number) + L")";

    while (!current.empty() && (current.back() == L' ' || current.back() == L'\0'))
        current.remove_suffix(1);

    if (!current.empty() && current.back() == L')')
    {
        const std::size_t open = current.rfind(L'(');
        if (open != std::wstring_view::npos)
        {
            const std::wstring_view inside =
                current.substr(open + 1, current.size() - open - 2);
            if (parse_com_name(inside))
            {
                std::wstring name(current.substr(0, open));
                name += tail;
                return name;
            }
        }
    }

    std::wstring name(current);
    if (!name.empty())
        name += L' ';
    name += tail;
    return name;
}

} // namespace vss::helper
