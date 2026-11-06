//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The part of the permissions layer that is the same everywhere: reading a
// firewall rule list, deciding what an observation means, and the text of the
// one firewall step. The host is read in src/platform/<os>/.
//

#include <vss/ports/permissions.hpp>

#include "platform/permissions_platform.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <string>
#include <string_view>
#include <utility>

namespace vss::permissions
{
namespace
{

std::string lower(std::string_view text)
{
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c)
                   { return static_cast<char>(std::tolower(c)); });
    return out;
}

/// A path as the firewall compares it: no case, one kind of separator.
std::string path_key(std::string_view text)
{
    std::string out = lower(text);
    std::replace(out.begin(), out.end(), '/', '\\');
    return out;
}

std::string_view trim(std::string_view text) noexcept
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.remove_suffix(1);
    return text;
}

bool parse_number(std::string_view text, unsigned long& value) noexcept
{
    text = trim(text);
    if (text.empty())
        return false;
    const auto result =
        std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

std::string_view protocol_word(transport_protocol protocol) noexcept
{
    return protocol == transport_protocol::tcp ? "tcp" : "udp";
}

std::string_view protocol_upper(transport_protocol protocol) noexcept
{
    return protocol == transport_protocol::tcp ? "TCP" : "UDP";
}

int protocol_number(transport_protocol protocol) noexcept
{
    return protocol == transport_protocol::tcp ? 6 : 17;
}

std::string profile_words(long profiles)
{
    std::string out;
    const auto add = [&](long bit, std::string_view word)
    {
        if ((profiles & bit) == 0)
            return;
        if (!out.empty())
            out += ',';
        out += word;
    };
    add(profile_domain, "domain");
    add(profile_private, "private");
    add(profile_public, "public");
    return out;
}

bool rule_names_program(const firewall_rule_record& rule,
                        std::string_view program)
{
    return rule.program.empty() || path_key(rule.program) == path_key(program);
}

bool rule_covers_protocol(const firewall_rule_record& rule,
                          transport_protocol protocol) noexcept
{
    return rule.protocol == 256 || rule.protocol == protocol_number(protocol);
}

bool rule_applies_now(const firewall_rule_record& rule,
                      long active_profiles) noexcept
{
    // Without a known profile any rule might apply; assume it does, so the
    // answer errs towards "covered" only when the rule really names the
    // port and program.
    return active_profiles == 0 || (rule.profiles & active_profiles) != 0;
}

std::string quote(std::string_view text)
{
    std::string out = "\"";
    out += text;
    out += '"';
    return out;
}

firewall_verdict worst(firewall_verdict a, firewall_verdict b) noexcept
{
    const auto rank = [](firewall_verdict v)
    {
        switch (v)
        {
        case firewall_verdict::blocked:
            return 3;
        case firewall_verdict::unknown:
            return 2;
        case firewall_verdict::no_rule:
            return 1;
        case firewall_verdict::allowed:
            break;
        }
        return 0;
    };
    return rank(a) >= rank(b) ? a : b;
}

std::string program_of(const context& ask)
{
    return ask.program_path.empty() ? this_program() : ask.program_path;
}

std::vector<fact> windows_facts(const windows_observation& seen,
                                const context& ask)
{
    std::vector<fact> facts;

    fact elevation;
    elevation.what = topic::windows_elevation;
    if (seen.elevated == tri::unknown)
    {
        elevation.status = state::unknown;
        elevation.why = std::string(reason::not_asked);
    }
    else if (seen.elevated == tri::yes)
    {
        elevation.status = state::ok;
        elevation.why = std::string(reason::elevated);
    }
    else
    {
        // Not elevated is the normal, safe state. It is information: some
        // steps (a new com0com pair) will ask for elevation when they run.
        elevation.status = state::info;
        elevation.why = seen.administrator == tri::yes
                            ? std::string(reason::admin_not_elevated)
                            : std::string(reason::standard_user);
    }
    facts.push_back(std::move(elevation));

    fact firewall;
    firewall.what = topic::windows_firewall;
    firewall.subject = program_of(ask);

    std::vector<listener> exposed;
    for (const listener& each : ask.listeners)
    {
        if (!is_loopback_host(each.host))
            exposed.push_back(each);
    }

    firewall.port = exposed.empty() ? std::uint16_t{0} : exposed.front().port;
    firewall.protocol = exposed.empty() ? transport_protocol::tcp
                                        : exposed.front().protocol;

    if (exposed.empty())
    {
        firewall.status = state::ok;
        firewall.why = std::string(reason::loopback_only);
        facts.push_back(std::move(firewall));
        return facts;
    }

    if (seen.firewall_on == tri::no)
    {
        firewall.status = state::ok;
        firewall.why = std::string(reason::firewall_off);
        facts.push_back(std::move(firewall));
        return facts;
    }

    if (seen.firewall_on == tri::yes && seen.block_all_inbound == tri::yes)
    {
        firewall.status = state::attention;
        firewall.why = std::string(reason::block_all_inbound);
        facts.push_back(std::move(firewall));
        return facts;
    }

    if (!seen.rules.has_value())
    {
        firewall.status = state::unknown;
        firewall.why = std::string(reason::not_asked);
        facts.push_back(std::move(firewall));
        return facts;
    }

    firewall_verdict overall = firewall_verdict::allowed;
    const listener* missing = nullptr;
    for (const listener& each : exposed)
    {
        const firewall_verdict one = firewall_verdict_for(
            *seen.rules, firewall.subject, each, seen.active_profiles);
        overall = worst(overall, one);
        if (one == firewall_verdict::no_rule && missing == nullptr)
            missing = &each;
    }

    switch (overall)
    {
    case firewall_verdict::allowed:
        firewall.status = state::ok;
        firewall.why = std::string(reason::allow_rule);
        break;
    case firewall_verdict::blocked:
        firewall.status = state::attention;
        firewall.why = std::string(reason::blocked_by_rule);
        break;
    case firewall_verdict::no_rule:
    {
        firewall.status = state::attention;
        firewall.why = std::string(reason::no_allow_rule);
        firewall_rule_spec spec;
        spec.program = firewall.subject;
        spec.protocol = missing->protocol;
        spec.port = missing->port;
        spec.profiles = seen.active_profiles != 0 &&
                                seen.active_profiles != profile_all
                            ? seen.active_profiles
                            : profile_private;
        if (validate(spec).empty())
        {
            firewall.remedy = netsh_add_command(spec);
            firewall.can_act = can_manage_firewall_rules();
            if (firewall.can_act)
                firewall.step = spec;
        }
        break;
    }
    case firewall_verdict::unknown:
        firewall.status = state::unknown;
        firewall.why = std::string(reason::not_asked);
        break;
    }
    facts.push_back(std::move(firewall));
    return facts;
}

} // namespace

bool is_loopback_host(std::string_view host)
{
    host = trim(host);
    if (host.size() >= 2 && host.front() == '[' && host.back() == ']')
        host = host.substr(1, host.size() - 2);
    if (host.empty())
        return false;
    if (lower(host) == "localhost" || host == "::1")
        return true;
    // 127.0.0.0/8, four decimal parts.
    if (host.substr(0, 4) != "127.")
        return false;
    int parts = 0;
    std::string_view rest = host;
    while (!rest.empty())
    {
        const auto dot = rest.find('.');
        const std::string_view part = rest.substr(0, dot);
        unsigned long value = 0;
        if (!parse_number(part, value) || value > 255)
            return false;
        ++parts;
        if (dot == std::string_view::npos)
            break;
        rest.remove_prefix(dot + 1);
        if (rest.empty())
            return false;
    }
    return parts == 4;
}


bool parse_listen_uri(std::string_view uri, listener& out)
{
    constexpr std::string_view tcp_scheme = "tcp-listen://";
    constexpr std::string_view udp_scheme = "udp-listen://";
    transport_protocol protocol = transport_protocol::tcp;
    if (uri.substr(0, tcp_scheme.size()) == tcp_scheme)
    {
        uri.remove_prefix(tcp_scheme.size());
    }
    else if (uri.substr(0, udp_scheme.size()) == udp_scheme)
    {
        protocol = transport_protocol::udp;
        uri.remove_prefix(udp_scheme.size());
    }
    else
    {
        return false;
    }
    const auto colon = uri.rfind(':');
    if (colon == std::string_view::npos)
        return false;
    std::string_view host = uri.substr(0, colon);
    if (host.size() >= 2 && host.front() == '[' && host.back() == ']')
        host = host.substr(1, host.size() - 2);
    unsigned long number = 0;
    if (!parse_number(uri.substr(colon + 1), number) || number == 0 ||
        number > 65535)
        return false;
    out.protocol = protocol;
    out.host = std::string(host);
    out.port = static_cast<std::uint16_t>(number);
    return true;
}

bool port_list_covers(std::string_view list, std::uint16_t port) noexcept
{
    list = trim(list);
    if (list.empty() || list == "*")
        return true;
    if (port == 0)
        return false;
    while (!list.empty())
    {
        const auto comma = list.find(',');
        const std::string_view item = trim(list.substr(0, comma));
        const auto dash = item.find('-');
        unsigned long low = 0;
        unsigned long high = 0;
        bool ok = false;
        if (dash == std::string_view::npos)
        {
            ok = parse_number(item, low);
            high = low;
        }
        else
        {
            ok = parse_number(item.substr(0, dash), low) &&
                 parse_number(item.substr(dash + 1), high);
        }
        if (ok && port >= low && port <= high)
            return true;
        if (comma == std::string_view::npos)
            break;
        list.remove_prefix(comma + 1);
    }
    return false;
}

firewall_verdict firewall_verdict_for(
    const std::vector<firewall_rule_record>& rules, std::string_view program,
    const listener& what, long active_profiles)
{
    bool allowed = false;
    for (const firewall_rule_record& rule : rules)
    {
        if (!rule.enabled || !rule.inbound)
            continue;
        if (!rule_names_program(rule, program) ||
            !rule_covers_protocol(rule, what.protocol) ||
            !rule_applies_now(rule, active_profiles) ||
            !port_list_covers(rule.local_ports, what.port))
            continue;
        if (!rule.allow)
            return firewall_verdict::blocked;
        allowed = true;
    }
    return allowed ? firewall_verdict::allowed : firewall_verdict::no_rule;
}

std::string validate(const firewall_rule_spec& spec)
{
    if (spec.program.empty())
        return "the program is not named";
    if (spec.program.size() > 520)
        return "the program path is too long";
    const bool drive = spec.program.size() > 3 &&
                       std::isalpha(static_cast<unsigned char>(spec.program[0])) &&
                       spec.program[1] == ':' &&
                       (spec.program[2] == '\\' || spec.program[2] == '/');
    const bool share = spec.program.rfind("\\\\", 0) == 0;
    if (!drive && !share)
        return "the program path is not absolute";
    for (const char c : spec.program)
    {
        if (static_cast<unsigned char>(c) < 0x20 || c == '"' || c == '%' ||
            c == '&' || c == '|' || c == '<' || c == '>' || c == '^')
            return "the program path has a character that is not allowed";
    }
    if (spec.port == 0)
        return "the port is not chosen";
    if (spec.profiles == 0 || (spec.profiles & ~7L) != 0)
        return "the networks are not chosen";
    return {};
}

std::string firewall_rule_name(const firewall_rule_spec& spec)
{
    // The program is part of the name, so two of our programs can each have a
    // rule for the same port and removing one leaves the other.
    const auto slash = spec.program.find_last_of("/\\");
    const std::string file =
        slash == std::string::npos ? spec.program : spec.program.substr(slash + 1);
    return "Virtual Serial Studio " + file + " " +
           std::string(protocol_upper(spec.protocol)) + " " +
           std::to_string(spec.port);
}

std::string program_is_ours(const firewall_rule_spec& spec,
                            std::string_view helper_path)
{
    const auto split = [](std::string_view path)
    {
        const std::string key = path_key(path);
        const auto slash = key.find_last_of('\\');
        return slash == std::string::npos
                   ? std::pair<std::string, std::string>{"", key}
                   : std::pair<std::string, std::string>{
                         key.substr(0, slash), key.substr(slash + 1)};
    };
    const auto program = split(spec.program);
    const auto helper = split(helper_path);
    if (program.second != "vss_gui.exe" && program.second != "vss_cli.exe")
        return "the program is not one of ours";
    if (helper.first.empty() || program.first != helper.first)
        return "the program is not in the folder of the helper";
    return {};
}

std::string_view firewall_rule_group() noexcept
{
    return "Virtual Serial Studio";
}

std::string netsh_add_command(const firewall_rule_spec& spec)
{
    std::string out = "netsh advfirewall firewall add rule name=" +
                      quote(firewall_rule_name(spec)) +
                      " dir=in action=allow program=" + quote(spec.program) +
                      " protocol=" + std::string(protocol_upper(spec.protocol)) +
                      " localport=" + std::to_string(spec.port) + " remoteip=" +
                      (spec.scope == rule_scope::local_subnet ? "LocalSubnet"
                                                              : "Any") +
                      " profile=" + profile_words(spec.profiles);
    return out;
}

std::string netsh_delete_command(const firewall_rule_spec& spec)
{
    return "netsh advfirewall firewall delete rule name=" +
           quote(firewall_rule_name(spec)) + " program=" + quote(spec.program) +
           " protocol=" + std::string(protocol_upper(spec.protocol)) +
           " localport=" + std::to_string(spec.port);
}

std::vector<std::string> elevated_arguments(const firewall_rule_spec& spec,
                                            bool remove)
{
    return {"permissions",
            remove ? "firewall-remove" : "firewall-allow",
            "--program=" + spec.program,
            "--protocol=" + std::string(protocol_word(spec.protocol)),
            "--port=" + std::to_string(spec.port),
            std::string("--scope=") +
                (spec.scope == rule_scope::local_subnet ? "local-subnet"
                                                        : "any"),
            "--profiles=" + profile_words(spec.profiles)};
}

std::optional<parsed_step>
parse_elevated_arguments(const std::vector<std::string>& arguments)
{
    if (arguments.size() < 2 || arguments[0] != "permissions")
        return std::nullopt;

    parsed_step step;
    if (arguments[1] == "firewall-allow")
        step.remove = false;
    else if (arguments[1] == "firewall-remove")
        step.remove = true;
    else
        return std::nullopt;

    bool have_program = false;
    bool have_protocol = false;
    bool have_port = false;
    step.spec.profiles = 0;
    for (std::size_t index = 2; index < arguments.size(); ++index)
    {
        const std::string& argument = arguments[index];
        const auto equals = argument.find('=');
        if (argument.rfind("--", 0) != 0 || equals == std::string::npos)
            return std::nullopt;
        const std::string key = argument.substr(0, equals);
        const std::string value = argument.substr(equals + 1);
        if (key == "--program")
        {
            step.spec.program = value;
            have_program = true;
        }
        else if (key == "--protocol")
        {
            if (value == "tcp")
                step.spec.protocol = transport_protocol::tcp;
            else if (value == "udp")
                step.spec.protocol = transport_protocol::udp;
            else
                return std::nullopt;
            have_protocol = true;
        }
        else if (key == "--port")
        {
            unsigned long number = 0;
            if (!parse_number(value, number) || number == 0 || number > 65535)
                return std::nullopt;
            step.spec.port = static_cast<std::uint16_t>(number);
            have_port = true;
        }
        else if (key == "--scope")
        {
            if (value == "local-subnet")
                step.spec.scope = rule_scope::local_subnet;
            else if (value == "any")
                step.spec.scope = rule_scope::any_address;
            else
                return std::nullopt;
        }
        else if (key == "--profiles")
        {
            std::string_view rest = value;
            while (!rest.empty())
            {
                const auto comma = rest.find(',');
                const std::string_view word = rest.substr(0, comma);
                if (word == "domain")
                    step.spec.profiles |= profile_domain;
                else if (word == "private")
                    step.spec.profiles |= profile_private;
                else if (word == "public")
                    step.spec.profiles |= profile_public;
                else
                    return std::nullopt;
                if (comma == std::string_view::npos)
                    break;
                rest.remove_prefix(comma + 1);
            }
        }
        else
        {
            return std::nullopt;
        }
    }
    if (!have_program || !have_protocol || !have_port)
        return std::nullopt;
    return step;
}

std::string_view host_name() noexcept
{
    return detail::platform_host_name();
}

observation observe(const context& ask)
{
    return detail::platform_observe(ask);
}

std::vector<fact> evaluate(const observation& seen, const context& ask,
                           std::string_view host)
{
    if (host == "windows")
        return windows_facts(seen.windows, ask);
    return {};
}

std::vector<fact> probe(const context& ask)
{
    return evaluate(observe(ask), ask, host_name());
}

step_result apply_firewall_rule(const firewall_rule_spec& spec, bool remove)
{
    if (const std::string why = validate(spec); !why.empty())
        return {step_status::invalid, why};
    if (!detail::platform_can_manage_firewall_rules())
        return {step_status::unsupported,
                "this build cannot change firewall rules"};
    return detail::platform_apply_firewall_rule(spec, remove);
}

step_result run_elevated(const std::string& executable,
                         const std::vector<std::string>& arguments)
{
    if (!detail::platform_can_run_elevated())
        return {step_status::unsupported,
                "this build cannot start a helper elevated"};
    return detail::platform_run_elevated(executable, arguments);
}

bool can_manage_firewall_rules() noexcept
{
    return detail::platform_can_manage_firewall_rules();
}

bool can_run_elevated() noexcept
{
    return detail::platform_can_run_elevated();
}

std::string this_program()
{
    return detail::platform_this_program();
}

} // namespace vss::permissions
