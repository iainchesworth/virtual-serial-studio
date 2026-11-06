//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// What Linux and macOS answer in the same way when the permissions layer asks:
// who the user is, which groups the process has, whether a serial device may be
// opened, and the output of a read-only system command. Included by the files
// in `src/platform/linux` and `src/platform/macos`, like
// posix_private_directory.hpp.
//

#pragma once

#include <vss/ports/permissions.hpp>

#include <grp.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace vss::detail::posix
{

inline bool path_exists(const char* path) noexcept
{
    std::error_code ignored;
    return std::filesystem::exists(path, ignored);
}

inline std::string read_small_file(const char* path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return {};
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

inline std::string environment(const char* name)
{
    const char* value = std::getenv(name);
    return value == nullptr ? std::string() : std::string(value);
}

inline std::string user_name()
{
    const passwd* entry = ::getpwuid(::geteuid());
    return entry == nullptr || entry->pw_name == nullptr
               ? std::string()
               : std::string(entry->pw_name);
}

inline std::set<gid_t> process_groups()
{
    std::set<gid_t> groups;
    groups.insert(::getegid());
    const int count = ::getgroups(0, nullptr);
    if (count > 0)
    {
        std::vector<gid_t> list(static_cast<std::size_t>(count));
        const int got = ::getgroups(count, list.data());
        if (got > 0)
            groups.insert(list.begin(), list.begin() + got);
    }
    return groups;
}

/// The owner group, mode and `access(2)` answer for one serial device. Nothing
/// is opened. `access` answers for the file mode only, which is what makes a
/// policy denial recognisable: a device it allows and an open that is refused.
inline permissions::device_access observe_device(const std::string& path,
                                                 const std::string& user,
                                                 const std::set<gid_t>& groups)
{
    using permissions::tri;
    permissions::device_access out;
    out.path = path;
    struct stat info{};
    if (::stat(path.c_str(), &info) != 0)
        return out;
    out.exists = true;

    if (const group* owner = ::getgrgid(info.st_gid);
        owner != nullptr && owner->gr_name != nullptr)
    {
        out.group = owner->gr_name;
        out.listed_in_group = tri::no;
        if (owner->gr_mem != nullptr)
        {
            for (char** member = owner->gr_mem; *member != nullptr; ++member)
            {
                if (user == *member)
                    out.listed_in_group = tri::yes;
            }
        }
    }
    out.in_group = groups.count(info.st_gid) != 0 ? tri::yes : tri::no;
    const bool group_rw =
        (info.st_mode & S_IRGRP) != 0 && (info.st_mode & S_IWGRP) != 0;
    out.group_may_write = group_rw ? tri::yes : tri::no;
    out.permitted =
        ::access(path.c_str(), R_OK | W_OK) == 0 ? tri::yes : tri::no;
    return out;
}

/// Runs a read-only system command and returns what it printed (standard output
/// and error together), or nothing when it could not be started, took longer
/// than `timeout`, or did not exit normally. The command gets a small fixed
/// environment, not this program's, and its language is C so the text can be
/// read by `parse_*`.
inline std::optional<std::string>
capture_command(const std::vector<std::string>& words,
                std::chrono::milliseconds timeout)
{
    if (words.empty())
        return std::nullopt;

    int pipe_ends[2] = {-1, -1};
    if (::pipe(pipe_ends) != 0)
        return std::nullopt;

    posix_spawn_file_actions_t actions;
    if (::posix_spawn_file_actions_init(&actions) != 0)
    {
        ::close(pipe_ends[0]);
        ::close(pipe_ends[1]);
        return std::nullopt;
    }
    ::posix_spawn_file_actions_adddup2(&actions, pipe_ends[1], 1);
    ::posix_spawn_file_actions_adddup2(&actions, pipe_ends[1], 2);
    ::posix_spawn_file_actions_addclose(&actions, pipe_ends[0]);
    ::posix_spawn_file_actions_addclose(&actions, pipe_ends[1]);

    std::vector<std::string> copy = words;
    std::vector<char*> argv;
    for (std::string& word : copy)
        argv.push_back(word.data());
    argv.push_back(nullptr);

    std::string path_variable = "PATH=/usr/bin:/bin:/usr/sbin:/sbin";
    std::string language = "LC_ALL=C";
    char* environment_block[] = {path_variable.data(), language.data(),
                                 nullptr};

    pid_t child = 0;
    const int started = ::posix_spawn(&child, argv[0], &actions, nullptr,
                                      argv.data(), environment_block);
    ::posix_spawn_file_actions_destroy(&actions);
    ::close(pipe_ends[1]);
    if (started != 0)
    {
        ::close(pipe_ends[0]);
        return std::nullopt;
    }

    std::string text;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    bool finished = true;
    for (;;)
    {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (left.count() <= 0)
        {
            finished = false;
            break;
        }
        pollfd waiting{pipe_ends[0], POLLIN, 0};
        const int ready = ::poll(&waiting, 1, static_cast<int>(left.count()));
        if (ready < 0 && errno == EINTR)
            continue;
        if (ready <= 0)
        {
            finished = false;
            break;
        }
        char buffer[512];
        const ssize_t got = ::read(pipe_ends[0], buffer, sizeof(buffer));
        if (got < 0 && errno == EINTR)
            continue;
        if (got <= 0)
            break;
        text.append(buffer, static_cast<std::size_t>(got));
        if (text.size() > 65536)
        {
            finished = false;
            break;
        }
    }
    ::close(pipe_ends[0]);
    if (!finished)
        ::kill(child, SIGKILL);

    int status = 0;
    while (::waitpid(child, &status, 0) < 0)
    {
        if (errno != EINTR)
            return std::nullopt;
    }
    if (!finished || !WIFEXITED(status))
        return std::nullopt;
    return text;
}

} // namespace vss::detail::posix
