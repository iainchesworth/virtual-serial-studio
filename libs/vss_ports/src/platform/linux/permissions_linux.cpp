//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// What Linux says about serial device access, confinement and the firewall, and
// the one thing Studio may do about it once the user agrees: ask polkit
// (`pkexec`) to put the user in the group that owns the serial devices.
//
// Everything read here is readable by an ordinary user: the process's groups,
// `stat` and `access` of a device, /proc/self/attr/current (the AppArmor
// profile or SELinux context), /sys/fs/selinux/enforce and /etc/ufw/ufw.conf.
// Firewall rules need root, so they are not read, and the fact says so.
//

#include "../permissions_platform.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include <grp.h>
#include <pwd.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace vss::detail
{
namespace
{

using namespace permissions;

bool exists(const char* path) noexcept
{
    std::error_code ignored;
    return std::filesystem::exists(path, ignored);
}

std::string read_small_file(const char* path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return {};
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

std::string environment(const char* name)
{
    const char* value = std::getenv(name);
    return value == nullptr ? std::string() : std::string(value);
}

std::string user_name()
{
    const passwd* entry = ::getpwuid(::geteuid());
    return entry == nullptr || entry->pw_name == nullptr
               ? std::string()
               : std::string(entry->pw_name);
}

std::set<gid_t> process_groups()
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

device_access observe_device(const std::string& path, const std::string& user,
                             const std::set<gid_t>& groups)
{
    device_access out;
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

std::string find_usermod()
{
    for (const char* candidate :
         {"/usr/sbin/usermod", "/sbin/usermod", "/usr/bin/usermod"})
    {
        if (exists(candidate))
            return candidate;
    }
    return {};
}

tri ufw_enabled()
{
    if (!exists("/etc/ufw/ufw.conf"))
        return tri::no;
    std::ifstream file("/etc/ufw/ufw.conf");
    if (!file)
        return tri::unknown;
    std::string line;
    while (std::getline(file, line))
    {
        std::string compact;
        for (const char c : line)
        {
            if (c != ' ' && c != '\t' && c != '\r')
                compact += static_cast<char>(
                    std::tolower(static_cast<unsigned char>(c)));
        }
        if (compact == "enabled=yes")
            return tri::yes;
    }
    return tri::no;
}

} // namespace

std::string_view platform_host_name() noexcept
{
    return "linux";
}

permissions::observation platform_observe(const permissions::context& ask)
{
    observation seen;
    linux_observation& out = seen.linux_host;

    out.user = user_name();
    out.root = ::geteuid() == 0 ? tri::yes : tri::no;

    const std::set<gid_t> groups = process_groups();
    for (const std::string& path : ask.device_paths)
        out.devices.push_back(observe_device(path, out.user, groups));

    out.label =
        parse_security_label(read_small_file("/proc/self/attr/current"));
    if (exists("/sys/fs/selinux"))
    {
        const std::string enforce = read_small_file("/sys/fs/selinux/enforce");
        if (!enforce.empty())
            out.selinux_mode =
                enforce.front() == '1' ? "enforcing" : "permissive";
    }
    else
    {
        out.selinux_mode = "disabled";
    }

    if (const std::string id = environment("FLATPAK_ID");
        !id.empty() || exists("/.flatpak-info"))
    {
        out.sandbox = "flatpak";
        out.sandbox_name = id;
    }
    else if (!environment("SNAP").empty() && !environment("SNAP_NAME").empty())
    {
        out.sandbox = "snap";
        out.sandbox_name = environment("SNAP_NAME");
    }

    out.ufw_active = ufw_enabled();
    out.firewalld_active =
        exists("/run/firewalld.pid") || exists("/run/firewalld") ? tri::yes
                                                                 : tri::no;
    out.can_ask_polkit = exists("/usr/bin/pkexec") && !find_usermod().empty()
                             ? tri::yes
                             : tri::no;
    return seen;
}

std::string platform_this_program()
{
    char buffer[4096];
    const ssize_t length =
        ::readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (length <= 0)
        return {};
    return std::string(buffer, static_cast<std::size_t>(length));
}

bool platform_can_manage_firewall_rules() noexcept
{
    return false;
}

permissions::step_result
platform_apply_firewall_rule(const permissions::firewall_rule_spec&, bool)
{
    return {permissions::step_status::unsupported,
            "this platform has no firewall rule step"};
}

bool platform_can_run_elevated() noexcept
{
    return false;
}

permissions::step_result platform_run_elevated(const std::string&,
                                               const std::vector<std::string>&)
{
    return {permissions::step_status::unsupported,
            "this platform has no generic elevated step"};
}

bool platform_can_add_user_to_group() noexcept
{
    return exists("/usr/bin/pkexec") && !find_usermod().empty();
}

permissions::step_result
platform_add_user_to_group(const permissions::group_membership_spec& spec)
{
    // Only the user running Studio can be put in a group by this step.
    if (spec.user != user_name())
        return {step_status::invalid, "the step changes the current user only"};
    const std::string usermod = find_usermod();
    if (usermod.empty() || !exists("/usr/bin/pkexec"))
        return {step_status::unsupported, "pkexec or usermod is not installed"};

    std::vector<std::string> words = {"/usr/bin/pkexec", usermod, "-aG",
                                      spec.group, spec.user};
    std::vector<char*> argv;
    for (std::string& word : words)
        argv.push_back(word.data());
    argv.push_back(nullptr);

    pid_t child = 0;
    if (::posix_spawn(&child, "/usr/bin/pkexec", nullptr, nullptr, argv.data(),
                      environ) != 0)
        return {step_status::failed, "pkexec could not be started"};

    int status = 0;
    while (::waitpid(child, &status, 0) < 0)
    {
        if (errno != EINTR)
            return {step_status::failed, "the helper did not finish"};
    }
    if (!WIFEXITED(status))
        return {step_status::failed, "the helper ended abnormally"};
    switch (WEXITSTATUS(status))
    {
    case 0:
        return {step_status::done, "the user was added to the group"};
    case 126:
        // pkexec: the authentication dialog was dismissed.
        return {step_status::declined,
                "the authentication prompt was dismissed"};
    case 127:
        // pkexec: not authorised, or authentication failed.
        return {step_status::not_elevated, "polkit did not authorise the step"};
    default:
        break;
    }
    return {step_status::failed, "usermod did not succeed"};
}

} // namespace vss::detail
