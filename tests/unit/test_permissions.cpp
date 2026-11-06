//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The permissions layer: what a firewall rule list means, what an observation
// of Windows says, and the text of the one firewall step. Observations are
// injected, so every outcome is checked on every operating system without
// touching the real firewall.
//

#include "test_framework.hpp"

#include <vss/ports/permissions.hpp>

#include <string>
#include <vector>

using namespace vss::permissions;

namespace
{

const std::string program = "C:\\Studio\\bin\\vss_gui.exe";

firewall_rule_record allow_rule(std::string path, std::string ports,
                                int protocol = 6)
{
    firewall_rule_record rule;
    rule.enabled = true;
    rule.inbound = true;
    rule.allow = true;
    rule.program = std::move(path);
    rule.local_ports = std::move(ports);
    rule.protocol = protocol;
    return rule;
}

const fact* find(const std::vector<fact>& facts, topic what)
{
    for (const fact& each : facts)
    {
        if (each.what == what)
            return &each;
    }
    return nullptr;
}

context listening_on(std::string host, std::uint16_t port,
                     transport_protocol protocol = transport_protocol::tcp)
{
    context ask;
    ask.program_path = program;
    listener each;
    each.host = std::move(host);
    each.port = port;
    each.protocol = protocol;
    ask.listeners.push_back(each);
    return ask;
}

observation firewall_on_with(std::vector<firewall_rule_record> rules)
{
    observation seen;
    seen.windows.elevated = tri::no;
    seen.windows.administrator = tri::yes;
    seen.windows.firewall_on = tri::yes;
    seen.windows.block_all_inbound = tri::no;
    seen.windows.active_profiles = profile_private;
    seen.windows.rules = std::move(rules);
    return seen;
}

} // namespace

TEST_CASE("permissions: loopback hosts are recognised and the wildcard is not")
{
    REQUIRE(is_loopback_host("127.0.0.1"));
    REQUIRE(is_loopback_host("127.1.2.3"));
    REQUIRE(is_loopback_host("localhost"));
    REQUIRE(is_loopback_host("LOCALHOST"));
    REQUIRE(is_loopback_host("::1"));
    REQUIRE(is_loopback_host("[::1]"));

    REQUIRE(!is_loopback_host(""));
    REQUIRE(!is_loopback_host("0.0.0.0"));
    REQUIRE(!is_loopback_host("::"));
    REQUIRE(!is_loopback_host("192.168.1.5"));
    REQUIRE(!is_loopback_host("127.0.0"));
    REQUIRE(!is_loopback_host("127.0.0.256"));
    REQUIRE(!is_loopback_host("127.0.0.1.5"));
    REQUIRE(!is_loopback_host("example.invalid"));
}

TEST_CASE("permissions: a Windows port list covers the ports it names")
{
    REQUIRE(port_list_covers("", 4001));
    REQUIRE(port_list_covers("*", 4001));
    REQUIRE(port_list_covers("4001", 4001));
    REQUIRE(!port_list_covers("4001", 4002));
    REQUIRE(port_list_covers("4000-4010", 4005));
    REQUIRE(!port_list_covers("4000-4010", 4011));
    REQUIRE(port_list_covers("80, 4000-4010,5000", 5000));
    REQUIRE(!port_list_covers("80,443", 8080));
    // A named group such as RPC covers none of the ports we use.
    REQUIRE(!port_list_covers("RPC", 4001));
    // A port that is not chosen yet is covered only by "any port".
    REQUIRE(port_list_covers("*", 0));
    REQUIRE(!port_list_covers("4001", 0));
}

TEST_CASE("permissions: a firewall verdict follows program, protocol, port and "
          "network")
{
    listener tcp;
    tcp.port = 4001;

    REQUIRE(firewall_verdict_for({}, program, tcp, profile_private) ==
            firewall_verdict::no_rule);

    // The right rule.
    REQUIRE(firewall_verdict_for({allow_rule(program, "4001")}, program, tcp,
                                 profile_private) == firewall_verdict::allowed);
    // The program is compared without regard to case or the kind of slash.
    REQUIRE(firewall_verdict_for(
                {allow_rule("c:/studio/BIN/VSS_GUI.EXE", "4001")}, program, tcp,
                profile_private) == firewall_verdict::allowed);
    // Another program, another port, another protocol, a disabled rule, an
    // outbound rule and another network do not count.
    REQUIRE(firewall_verdict_for({allow_rule("C:\\Other\\x.exe", "4001")},
                                 program, tcp,
                                 profile_private) == firewall_verdict::no_rule);
    REQUIRE(firewall_verdict_for({allow_rule(program, "4002")}, program, tcp,
                                 profile_private) == firewall_verdict::no_rule);
    REQUIRE(firewall_verdict_for({allow_rule(program, "4001", 17)}, program,
                                 tcp,
                                 profile_private) == firewall_verdict::no_rule);
    auto disabled = allow_rule(program, "4001");
    disabled.enabled = false;
    REQUIRE(firewall_verdict_for({disabled}, program, tcp, profile_private) ==
            firewall_verdict::no_rule);
    auto outbound = allow_rule(program, "4001");
    outbound.inbound = false;
    REQUIRE(firewall_verdict_for({outbound}, program, tcp, profile_private) ==
            firewall_verdict::no_rule);
    auto public_only = allow_rule(program, "4001");
    public_only.profiles = profile_public;
    REQUIRE(firewall_verdict_for({public_only}, program, tcp,
                                 profile_private) == firewall_verdict::no_rule);

    // A rule for any program and the port, or for the program and any port.
    REQUIRE(firewall_verdict_for({allow_rule("", "4001")}, program, tcp,
                                 profile_private) == firewall_verdict::allowed);
    REQUIRE(firewall_verdict_for({allow_rule(program, "*", 256)}, program, tcp,
                                 profile_private) == firewall_verdict::allowed);

    // A block rule wins over an allow rule, whatever the order.
    auto block = allow_rule(program, "4001");
    block.allow = false;
    REQUIRE(firewall_verdict_for({allow_rule(program, "4001"), block}, program,
                                 tcp,
                                 profile_private) == firewall_verdict::blocked);
    REQUIRE(firewall_verdict_for({block, allow_rule(program, "4001")}, program,
                                 tcp,
                                 profile_private) == firewall_verdict::blocked);
}

TEST_CASE("permissions: elevation facts say what the user could do")
{
    observation seen;
    seen.windows.elevated = tri::yes;
    seen.windows.administrator = tri::yes;
    auto facts = evaluate(seen, {}, "windows");
    const fact* elevation = find(facts, topic::windows_elevation);
    REQUIRE(elevation != nullptr);
    REQUIRE(elevation->status == state::ok);
    REQUIRE_EQ(elevation->why, std::string(reason::elevated));

    seen.windows.elevated = tri::no;
    facts = evaluate(seen, {}, "windows");
    elevation = find(facts, topic::windows_elevation);
    REQUIRE(elevation->status == state::info);
    REQUIRE_EQ(elevation->why, std::string(reason::admin_not_elevated));

    seen.windows.administrator = tri::no;
    facts = evaluate(seen, {}, "windows");
    elevation = find(facts, topic::windows_elevation);
    REQUIRE(elevation->status == state::info);
    REQUIRE_EQ(elevation->why, std::string(reason::standard_user));

    // A platform that cannot be asked is not guessed at.
    seen.windows.elevated = tri::unknown;
    facts = evaluate(seen, {}, "windows");
    elevation = find(facts, topic::windows_elevation);
    REQUIRE(elevation->status == state::unknown);
}

TEST_CASE("permissions: no firewall question while Studio listens on this "
          "computer only")
{
    observation seen = firewall_on_with({});
    for (const char* host : {"127.0.0.1", "localhost", "::1"})
    {
        const auto facts = evaluate(seen, listening_on(host, 4001), "windows");
        const fact* firewall = find(facts, topic::windows_firewall);
        REQUIRE(firewall != nullptr);
        REQUIRE(firewall->status == state::ok);
        REQUIRE_EQ(firewall->why, std::string(reason::loopback_only));
    }
    // No listener at all is the same.
    const auto none = evaluate(seen, {}, "windows");
    REQUIRE_EQ(find(none, topic::windows_firewall)->why,
               std::string(reason::loopback_only));
}

TEST_CASE("permissions: a listener on every address needs a firewall rule")
{
    // No rule: attention, with the exact command and the offer to do it.
    auto facts = evaluate(firewall_on_with({}), listening_on("0.0.0.0", 4001),
                          "windows");
    const fact* firewall = find(facts, topic::windows_firewall);
    REQUIRE(firewall->status == state::attention);
    REQUIRE_EQ(firewall->why, std::string(reason::no_allow_rule));
    REQUIRE_EQ(firewall->subject, program);
    REQUIRE(firewall->remedy.find("netsh advfirewall firewall add rule") == 0);
    REQUIRE(firewall->remedy.find("localport=4001") != std::string::npos);
    REQUIRE(firewall->remedy.find("remoteip=LocalSubnet") != std::string::npos);
    // The rule is for the network the computer is on and no other.
    REQUIRE(firewall->remedy.find("profile=private") != std::string::npos);
    REQUIRE(firewall->remedy.find("profile=private,") == std::string::npos);

    // The right rule is there.
    facts = evaluate(firewall_on_with({allow_rule(program, "4001")}),
                     listening_on("", 4001), "windows");
    firewall = find(facts, topic::windows_firewall);
    REQUIRE(firewall->status == state::ok);
    REQUIRE_EQ(firewall->why, std::string(reason::allow_rule));

    // A block rule.
    auto block = allow_rule(program, "4001");
    block.allow = false;
    facts = evaluate(firewall_on_with({block}), listening_on("0.0.0.0", 4001),
                     "windows");
    firewall = find(facts, topic::windows_firewall);
    REQUIRE(firewall->status == state::attention);
    REQUIRE_EQ(firewall->why, std::string(reason::blocked_by_rule));
    REQUIRE(firewall->remedy.empty());
}

TEST_CASE("permissions: firewall off, block-all and unreadable rules")
{
    observation off = firewall_on_with({});
    off.windows.firewall_on = tri::no;
    auto facts = evaluate(off, listening_on("0.0.0.0", 4001), "windows");
    REQUIRE_EQ(find(facts, topic::windows_firewall)->why,
               std::string(reason::firewall_off));
    REQUIRE(find(facts, topic::windows_firewall)->status == state::ok);

    // "Block all incoming connections" beats any allow rule, so a rule is not
    // offered.
    observation blocked = firewall_on_with({allow_rule(program, "4001")});
    blocked.windows.block_all_inbound = tri::yes;
    facts = evaluate(blocked, listening_on("0.0.0.0", 4001), "windows");
    const fact* firewall = find(facts, topic::windows_firewall);
    REQUIRE(firewall->status == state::attention);
    REQUIRE_EQ(firewall->why, std::string(reason::block_all_inbound));
    REQUIRE(!firewall->can_act);

    // The rules could not be read: say so, claim nothing.
    observation unread = firewall_on_with({});
    unread.windows.rules.reset();
    facts = evaluate(unread, listening_on("0.0.0.0", 4001), "windows");
    firewall = find(facts, topic::windows_firewall);
    REQUIRE(firewall->status == state::unknown);
    REQUIRE_EQ(firewall->why, std::string(reason::not_asked));
}

TEST_CASE("permissions: the firewall rule is one program, one port, one scope")
{
    firewall_rule_spec spec;
    spec.program = program;
    spec.port = 4001;
    REQUIRE(validate(spec).empty());
    REQUIRE_EQ(firewall_rule_name(spec),
               std::string("Virtual Serial Studio vss_gui.exe TCP 4001"));
    REQUIRE_EQ(
        netsh_add_command(spec),
        std::string("netsh advfirewall firewall add rule name=\"Virtual Serial "
                    "Studio vss_gui.exe TCP 4001\" dir=in action=allow "
                    "program=\"C:\\Studio\\bin\\vss_gui.exe\" protocol=TCP "
                    "localport=4001 remoteip=LocalSubnet profile=private"));
    REQUIRE_EQ(
        netsh_delete_command(spec),
        std::string("netsh advfirewall firewall delete rule name=\"Virtual "
                    "Serial Studio vss_gui.exe TCP 4001\" "
                    "program=\"C:\\Studio\\bin\\vss_gui.exe\" protocol=TCP "
                    "localport=4001"));

    spec.protocol = transport_protocol::udp;
    spec.scope = rule_scope::any_address;
    spec.profiles = profile_domain | profile_private;
    const std::string any = netsh_add_command(spec);
    REQUIRE(any.find("protocol=UDP") != std::string::npos);
    REQUIRE(any.find("remoteip=Any") != std::string::npos);
    REQUIRE(any.find("profile=domain,private") != std::string::npos);
}

TEST_CASE("permissions: a firewall rule that would open too much is refused")
{
    firewall_rule_spec spec;
    spec.program = program;
    spec.port = 4001;

    auto bad = spec;
    bad.port = 0;
    REQUIRE(!validate(bad).empty());
    bad = spec;
    bad.program.clear();
    REQUIRE(!validate(bad).empty());
    bad = spec;
    bad.program = "vss_gui.exe";
    REQUIRE(!validate(bad).empty());
    bad = spec;
    bad.program = "C:\\x\\a\" & calc.exe & \"b.exe";
    REQUIRE(!validate(bad).empty());
    bad = spec;
    bad.program = "C:\\x\\%PATH%.exe";
    REQUIRE(!validate(bad).empty());
    // Every network is never a default and is not allowed through the step.
    bad = spec;
    bad.profiles = profile_all;
    REQUIRE(!validate(bad).empty());
    bad = spec;
    bad.profiles = 0;
    REQUIRE(!validate(bad).empty());
}

TEST_CASE("permissions: the elevated helper arguments round-trip and nothing "
          "else is accepted")
{
    firewall_rule_spec spec;
    spec.program = "C:\\Program Files\\Studio\\vss_gui.exe";
    spec.protocol = transport_protocol::udp;
    spec.port = 5000;
    spec.scope = rule_scope::any_address;
    spec.profiles = profile_domain | profile_public;

    for (const bool remove : {false, true})
    {
        const auto arguments = elevated_arguments(spec, remove);
        const auto parsed = parse_elevated_arguments(arguments);
        REQUIRE(parsed.has_value());
        REQUIRE_EQ(parsed->remove, remove);
        REQUIRE_EQ(parsed->spec.program, spec.program);
        REQUIRE(parsed->spec.protocol == transport_protocol::udp);
        REQUIRE_EQ(parsed->spec.port, spec.port);
        REQUIRE(parsed->spec.scope == rule_scope::any_address);
        REQUIRE_EQ(parsed->spec.profiles, spec.profiles);
    }

    const auto good = elevated_arguments(spec, false);
    REQUIRE(!parse_elevated_arguments({}).has_value());
    REQUIRE(!parse_elevated_arguments({"permissions"}).has_value());
    REQUIRE(!parse_elevated_arguments({"devices"}).has_value());
    auto other_step = good;
    other_step[1] = "firewall-disable";
    REQUIRE(!parse_elevated_arguments(other_step).has_value());
    auto extra = good;
    extra.push_back("--remoteip=any");
    REQUIRE(!parse_elevated_arguments(extra).has_value());
    auto missing = good;
    missing.erase(missing.begin() + 4); // --port
    REQUIRE(!parse_elevated_arguments(missing).has_value());
    auto wide_port = good;
    wide_port[4] = "--port=70000";
    REQUIRE(!parse_elevated_arguments(wide_port).has_value());
    auto all_networks = good;
    all_networks[6] = "--profiles=all";
    REQUIRE(!parse_elevated_arguments(all_networks).has_value());
}

TEST_CASE("permissions: the helper makes rules for Studio and nothing else")
{
    firewall_rule_spec spec;
    spec.port = 4001;
    const std::string helper = "C:\\Studio\\bin\\vss_cli.exe";

    spec.program = "C:\\Studio\\bin\\vss_gui.exe";
    REQUIRE(program_is_ours(spec, helper).empty());
    spec.program = "c:/studio/bin/VSS_CLI.EXE";
    REQUIRE(program_is_ours(spec, helper).empty());

    spec.program = "C:\\Windows\\System32\\cmd.exe";
    REQUIRE(!program_is_ours(spec, helper).empty());
    spec.program = "C:\\Users\\x\\Downloads\\vss_gui.exe";
    REQUIRE(!program_is_ours(spec, helper).empty());
    spec.program = "C:\\Studio\\bin\\evil\\vss_gui.exe";
    REQUIRE(!program_is_ours(spec, helper).empty());
    spec.program = "C:\\Studio\\bin\\vss_gui.exe.bat";
    REQUIRE(!program_is_ours(spec, helper).empty());
    spec.program = "C:\\Studio\\bin\\vss_gui.exe";
    REQUIRE(!program_is_ours(spec, "vss_cli.exe").empty());
}

TEST_CASE("permissions: this host can be asked without changing anything")
{
    const std::string host(host_name());
    REQUIRE((host == "windows" || host == "linux" || host == "macos"));
    REQUIRE(!this_program().empty());

    // The real probe returns and, on Windows, always says something about
    // elevation. On the others it may say nothing yet.
    const auto facts = probe();
    if (host == "windows")
        REQUIRE(find(facts, topic::windows_elevation) != nullptr);
    if (host == "linux")
        REQUIRE(find(facts, topic::confinement) != nullptr);

    // A spec that fails validation is refused before anything is touched.
    firewall_rule_spec spec;
    REQUIRE(apply_firewall_rule(spec, false).status == step_status::invalid);
    REQUIRE(apply_firewall_rule(spec, true).status == step_status::invalid);
    REQUIRE_EQ(can_manage_firewall_rules(), host == "windows");
    REQUIRE_EQ(can_run_elevated(), host == "windows");
}

TEST_CASE("permissions: listen addresses are read as the engine reads them")
{
    listener each;
    REQUIRE(parse_listen_uri("tcp-listen://0.0.0.0:4001", each));
    REQUIRE(each.protocol == transport_protocol::tcp);
    REQUIRE_EQ(each.host, std::string("0.0.0.0"));
    REQUIRE_EQ(each.port, std::uint16_t{4001});
    REQUIRE(parse_listen_uri("udp-listen://:5000", each));
    REQUIRE(each.protocol == transport_protocol::udp);
    REQUIRE(each.host.empty());
    REQUIRE(parse_listen_uri("tcp-listen://[::1]:4001", each));
    REQUIRE_EQ(each.host, std::string("::1"));

    for (const char* bad :
         {"tcp://127.0.0.1:4001", "tcp-listen://127.0.0.1",
          "tcp-listen://127.0.0.1:0", "tcp-listen://127.0.0.1:70000",
          "udp-listen://:x", ""})
        REQUIRE(!parse_listen_uri(bad, each));
}
