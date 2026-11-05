//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <vss/ports/com0com.hpp>

#include "platform/com0com_platform.hpp"

#include <map>
#include <utility>

namespace vss
{

std::vector<com0com_pair>
group_com0com_pairs(const std::vector<serial_device_info>& devices)
{
    struct ends
    {
        std::string a;
        std::string b;
    };

    std::map<int, ends> by_index;
    for (const auto& device : devices)
    {
        if (!device.com0com || device.com0com_pair < 0)
            continue;
        if (device.com0com_side == "A")
            by_index[device.com0com_pair].a = device.path;
        else if (device.com0com_side == "B")
            by_index[device.com0com_pair].b = device.path;
    }

    std::vector<com0com_pair> pairs;
    for (const auto& entry : by_index)
    {
        if (entry.second.a.empty() || entry.second.b.empty())
            continue;
        pairs.push_back(
            com0com_pair{entry.first, entry.second.a, entry.second.b});
    }
    return pairs;
}

com0com_installation detect_com0com()
{
    return detail::platform_detect_com0com();
}

result<void> create_com0com_pair(const com0com_installation& installation,
                                 std::string_view port_a,
                                 std::string_view port_b,
                                 bool request_elevation)
{
    return detail::platform_create_com0com_pair(installation, port_a, port_b,
                                                request_elevation);
}

result<void> remove_com0com_pair(const com0com_installation& installation,
                                 int index, bool request_elevation)
{
    return detail::platform_remove_com0com_pair(installation, index,
                                                request_elevation);
}

} // namespace vss
