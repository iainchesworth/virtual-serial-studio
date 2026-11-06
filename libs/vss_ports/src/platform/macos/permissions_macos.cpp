//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "../permissions_platform.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

#include <mach-o/dyld.h>

namespace vss::detail
{

std::string_view platform_host_name() noexcept
{
    return "macos";
}

// Nothing here is asked yet: every fact stays unknown, which the GUI says.
permissions::observation platform_observe(const permissions::context&)
{
    return {};
}

std::string platform_this_program()
{
    std::uint32_t size = 0;
    ::_NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (::_NSGetExecutablePath(buffer.data(), &size) != 0)
        return {};
    buffer.resize(std::char_traits<char>::length(buffer.c_str()));
    return buffer;
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
            "this platform has no elevated helper step"};
}

bool platform_can_add_user_to_group() noexcept
{
    return false;
}

permissions::step_result
platform_add_user_to_group(const permissions::group_membership_spec&)
{
    return {permissions::step_status::unsupported,
            "this platform has no group step"};
}

} // namespace vss::detail
