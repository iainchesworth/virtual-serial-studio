//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "../permissions_platform.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

#include <unistd.h>

namespace vss::detail
{

std::string_view platform_host_name() noexcept
{
    return "linux";
}

// Nothing here is asked yet: every fact stays unknown, which the GUI says.
permissions::observation platform_observe(const permissions::context&)
{
    return {};
}

std::string platform_this_program()
{
    char buffer[4096];
    const ssize_t length = ::readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
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

permissions::step_result
platform_run_elevated(const std::string&, const std::vector<std::string>&)
{
    return {permissions::step_status::unsupported,
            "this platform has no elevated helper step"};
}

} // namespace vss::detail
