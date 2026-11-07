//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <vss/core/profile.hpp>

#include "../peer_open_platform.hpp"

#include <unistd.h>

#include <charconv>
#include <filesystem>
#include <system_error>
#include <unordered_set>

namespace vss::detail
{

bool platform_peer_open_supported() noexcept
{
    return true;
}

std::vector<peer_open_state>
platform_peer_open(const std::vector<std::string>& paths)
{
    VSS_PROFILE_ZONE("ports.peer_open.linux");
    namespace fs = std::filesystem;

    const std::unordered_set<std::string> wanted(paths.begin(), paths.end());
    std::unordered_set<std::string> found;
    const long self = static_cast<long>(::getpid());

    std::error_code ec;
    fs::directory_iterator processes("/proc", ec);
    if (ec)
        return std::vector<peer_open_state>(paths.size(),
                                            peer_open_state::unknown);

    for (const fs::directory_iterator end; processes != end;
         processes.increment(ec))
    {
        if (ec)
            break;

        const std::string name = processes->path().filename().string();
        long pid = 0;
        const auto parsed =
            std::from_chars(name.data(), name.data() + name.size(), pid);
        if (parsed.ec != std::errc() ||
            parsed.ptr != name.data() + name.size() || pid == self)
            continue;

        // A process this one may not inspect fails here and is skipped.
        std::error_code inner;
        fs::directory_iterator descriptors(processes->path() / "fd", inner);
        if (inner)
            continue;
        for (const fs::directory_iterator done; descriptors != done;
             descriptors.increment(inner))
        {
            if (inner)
                break;
            std::error_code link_error;
            const fs::path target =
                fs::read_symlink(descriptors->path(), link_error);
            if (!link_error && wanted.contains(target.string()))
                found.insert(target.string());
        }
    }

    std::vector<peer_open_state> out;
    out.reserve(paths.size());
    for (const std::string& path : paths)
        out.push_back(found.contains(path) ? peer_open_state::open
                                           : peer_open_state::not_open);
    return out;
}

} // namespace vss::detail
