//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "backend/com_numbers.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>

namespace vss::gui
{
namespace
{

bool taken(unsigned number, const std::vector<unsigned>& in_use)
{
    return std::find(in_use.begin(), in_use.end(), number) != in_use.end();
}

/// The first number from `from` to `last_gui_com_number` that is free, or 0.
unsigned first_free_from(unsigned from, const std::vector<unsigned>& in_use)
{
    for (unsigned number = std::max(from, first_gui_com_number);
         number <= last_gui_com_number; ++number)
    {
        if (!taken(number, in_use))
            return number;
    }
    return 0;
}

} // namespace

std::vector<unsigned> com_numbers_in_use(const std::vector<std::string>& paths)
{
    std::vector<unsigned> numbers;
    for (const std::string& path : paths)
    {
        if (path.size() < 4 || path.size() > 6)
            continue;
        const bool prefix =
            std::toupper(static_cast<unsigned char>(path[0])) == 'C' &&
            std::toupper(static_cast<unsigned char>(path[1])) == 'O' &&
            std::toupper(static_cast<unsigned char>(path[2])) == 'M';
        if (!prefix)
            continue;
        unsigned number = 0;
        bool digits = true;
        for (std::size_t index = 3; index < path.size(); ++index)
        {
            const unsigned char letter =
                static_cast<unsigned char>(path[index]);
            if (std::isdigit(letter) == 0)
            {
                digits = false;
                break;
            }
            number = number * 10 + static_cast<unsigned>(letter - '0');
        }
        if (digits && number != 0)
            numbers.push_back(number);
    }
    return numbers;
}

unsigned first_com_candidate(const std::vector<unsigned>& in_use)
{
    unsigned highest = 0;
    for (const unsigned number : in_use)
        highest = std::max(highest, number);
    const unsigned above = first_free_from(highest + 1, in_use);
    if (above != 0)
        return above;
    return first_free_from(first_gui_com_number, in_use);
}

unsigned next_com_candidate(unsigned candidate, unsigned start,
                            const std::vector<unsigned>& in_use)
{
    unsigned next = candidate;
    for (unsigned step = 0; step <= last_gui_com_number; ++step)
    {
        next = next >= last_gui_com_number ? first_gui_com_number : next + 1;
        if (next == start)
            return 0;
        if (!taken(next, in_use))
            return next;
    }
    return 0;
}

} // namespace vss::gui
