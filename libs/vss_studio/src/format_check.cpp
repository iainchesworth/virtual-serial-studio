//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <vss/core/profile.hpp>

#include <vss/studio/format_check.hpp>

#include <algorithm>
#include <unordered_map>

namespace vss {

std::optional<line_format> running_format(const endpoint_info& info)
{
    if (info.peer_format)
        return info.peer_format;
    if (info.kind == endpoint_kind::physical ||
        info.kind == endpoint_kind::rfc2217)
        return format_of(info.serial);
    return std::nullopt;
}

std::vector<format_mismatch>
find_format_mismatches(const std::vector<endpoint_info>& endpoints,
                       const std::vector<link_info>& links)
{
    VSS_PROFILE_ZONE("studio.format_check");
    std::unordered_map<endpoint_id, line_format> known;
    for (const auto& info : endpoints)
    {
        if (const auto format = running_format(info))
            known.emplace(info.id, *format);
    }

    std::vector<format_mismatch> out;
    for (const auto& link : links)
    {
        std::vector<endpoint_id> ids = link.sources;
        ids.insert(ids.end(), link.targets.begin(), link.targets.end());

        format_mismatch mismatch;
        mismatch.link = link.id;
        bool differs = false;
        for (const endpoint_id id : ids)
        {
            const auto it = known.find(id);
            if (it == known.end())
                continue;
            // Every pair counts: a member that knows only its baud rate agrees
            // with two that differ in framing, and they still differ.
            for (const auto& other : mismatch.members)
            {
                if (!same_format(other.format, it->second))
                    differs = true;
            }
            mismatch.members.push_back({id, it->second});
        }
        if (differs)
            out.push_back(std::move(mismatch));
    }
    return out;
}

} // namespace vss
