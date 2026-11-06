//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the permissions layer says about Linux: serial device access, who
// confines the program, and the firewall front ends. The observations are
// injected, so every outcome is checked on every operating system.
//

#include "test_framework.hpp"

#include <vss/ports/permissions.hpp>

#include <string>
#include <utility>
#include <vector>

using namespace vss::permissions;

namespace
{

const fact* find(const std::vector<fact>& facts, topic what)
{
    for (const fact& each : facts)
    {
        if (each.what == what)
            return &each;
    }
    return nullptr;
}

device_access device(std::string path, std::string group, tri in_group,
                     tri listed, tri may_write, tri permitted)
{
    device_access out;
    out.path = std::move(path);
    out.exists = true;
    out.group = std::move(group);
    out.in_group = in_group;
    out.listed_in_group = listed;
    out.group_may_write = may_write;
    out.permitted = permitted;
    return out;
}

observation linux_with(std::vector<device_access> devices)
{
    observation seen;
    seen.linux_host.user = "alice";
    seen.linux_host.root = tri::no;
    seen.linux_host.devices = std::move(devices);
    seen.linux_host.label = parse_security_label("unconfined\n");
    seen.linux_host.selinux_mode = "disabled";
    seen.linux_host.can_ask_polkit = tri::yes;
    seen.linux_host.ufw_active = tri::no;
    seen.linux_host.firewalld_active = tri::no;
    return seen;
}

context with_devices(std::vector<std::string> paths,
                     std::vector<std::string> refused = {})
{
    context ask;
    ask.device_paths = std::move(paths);
    ask.refused_paths = std::move(refused);
    return ask;
}

context listening_on(std::string host, std::uint16_t port)
{
    context ask;
    listener each;
    each.host = std::move(host);
    each.port = port;
    ask.listeners.push_back(each);
    return ask;
}

} // namespace

TEST_CASE("linux permissions: a device the user may open is fine")
{
    const auto seen = linux_with({device("/dev/ttyUSB0", "dialout", tri::yes,
                                         tri::yes, tri::yes, tri::yes)});
    const auto facts = evaluate(seen, with_devices({"/dev/ttyUSB0"}), "linux");
    const fact* serial = find(facts, topic::serial_access);
    REQUIRE(serial != nullptr);
    REQUIRE(serial->status == state::ok);
    REQUIRE_EQ(serial->why, std::string(reason::devices_open));
}

TEST_CASE("linux permissions: no devices listed is not a problem and not a "
          "claim")
{
    const auto facts = evaluate(linux_with({}), with_devices({}), "linux");
    const fact* serial = find(facts, topic::serial_access);
    REQUIRE(serial->status == state::info);
    REQUIRE_EQ(serial->why, std::string(reason::no_devices));
}

TEST_CASE("linux permissions: a device refused for lack of the group names the "
          "command and the polkit step")
{
    const auto seen = linux_with({device("/dev/ttyUSB0", "dialout", tri::no,
                                         tri::no, tri::yes, tri::no)});
    const auto facts = evaluate(seen, with_devices({"/dev/ttyUSB0"}), "linux");
    const fact* serial = find(facts, topic::serial_access);
    REQUIRE(serial->status == state::attention);
    REQUIRE_EQ(serial->why, std::string(reason::not_in_group));
    REQUIRE_EQ(serial->subject, std::string("/dev/ttyUSB0"));
    REQUIRE_EQ(serial->remedy, std::string("sudo usermod -aG dialout alice"));
    REQUIRE(serial->can_act);
    REQUIRE(serial->group_step.has_value());
    REQUIRE_EQ(serial->group_step->group, std::string("dialout"));
    REQUIRE_EQ(serial->group_step->user, std::string("alice"));

    // Without pkexec the command is shown and no step is offered.
    auto no_polkit = seen;
    no_polkit.linux_host.can_ask_polkit = tri::no;
    const auto plain =
        evaluate(no_polkit, with_devices({"/dev/ttyUSB0"}), "linux");
    const fact* shown = find(plain, topic::serial_access);
    REQUIRE(!shown->can_act);
    REQUIRE(!shown->group_step.has_value());
    REQUIRE_EQ(shown->remedy, std::string("sudo usermod -aG dialout alice"));
}

TEST_CASE("linux permissions: a group the account has but this login does not "
          "yet know needs a new login, not a command")
{
    const auto seen = linux_with(
        {device("/dev/ttyACM0", "uucp", tri::no, tri::yes, tri::yes, tri::no)});
    const auto facts = evaluate(seen, with_devices({"/dev/ttyACM0"}), "linux");
    const fact* serial = find(facts, topic::serial_access);
    REQUIRE(serial->status == state::attention);
    REQUIRE_EQ(serial->why, std::string(reason::relogin_needed));
    REQUIRE_EQ(serial->remedy, std::string("newgrp uucp"));
    REQUIRE(!serial->can_act);
}

TEST_CASE("linux permissions: a device whose mode forbids the group says so")
{
    const auto seen = linux_with({device("/dev/ttyS3", "dialout", tri::yes,
                                         tri::yes, tri::no, tri::no)});
    const auto facts = evaluate(seen, with_devices({"/dev/ttyS3"}), "linux");
    const fact* serial = find(facts, topic::serial_access);
    REQUIRE_EQ(serial->why, std::string(reason::mode_denies));
    REQUIRE_EQ(serial->remedy, std::string("ls -l /dev/ttyS3"));
}

TEST_CASE("linux permissions: a refusal the file mode does not explain is "
          "policy, and the remedy follows who confines the program")
{
    auto seen = linux_with({device("/dev/ttyUSB0", "dialout", tri::yes,
                                   tri::yes, tri::yes, tri::yes)});
    const auto ask = with_devices({"/dev/ttyUSB0"}, {"/dev/ttyUSB0"});

    // Nobody confines the program: look in the kernel log.
    auto facts = evaluate(seen, ask, "linux");
    const fact* serial = find(facts, topic::serial_access);
    REQUIRE(serial->status == state::attention);
    REQUIRE_EQ(serial->why, std::string(reason::denied_by_policy));
    REQUIRE(serial->remedy.find("journalctl -k") != std::string::npos);

    // AppArmor.
    seen.linux_host.label =
        parse_security_label("/usr/bin/vss_gui (enforce)\n");
    facts = evaluate(seen, ask, "linux");
    serial = find(facts, topic::serial_access);
    REQUIRE(serial->remedy.find("/etc/apparmor.d/local/usr.bin.vss_gui") !=
            std::string::npos);
    REQUIRE(serial->remedy.find("/dev/ttyUSB* rw,") != std::string::npos);
    REQUIRE(serial->remedy.find("sudo apparmor_parser -r") !=
            std::string::npos);

    // SELinux.
    seen.linux_host.label =
        parse_security_label("unconfined_u:unconfined_r:unconfined_t:s0\n");
    seen.linux_host.selinux_mode = "enforcing";
    facts = evaluate(seen, ask, "linux");
    serial = find(facts, topic::serial_access);
    REQUIRE(serial->remedy.find("audit2allow") != std::string::npos);
    REQUIRE(serial->remedy.find("ausearch") != std::string::npos);

    // Flatpak and Snap come first: their sandbox is the cause.
    seen.linux_host.sandbox = "flatpak";
    seen.linux_host.sandbox_name = "org.example.Studio";
    facts = evaluate(seen, ask, "linux");
    REQUIRE_EQ(find(facts, topic::serial_access)->remedy,
               std::string("flatpak override --user --device=all "
                           "org.example.Studio"));
    seen.linux_host.sandbox = "snap";
    seen.linux_host.sandbox_name = "studio";
    facts = evaluate(seen, ask, "linux");
    REQUIRE_EQ(find(facts, topic::serial_access)->remedy,
               std::string("snap connections studio"));
}

TEST_CASE("linux permissions: confinement is information, not an alarm")
{
    auto seen = linux_with({});
    const auto confinement = [](const observation& what)
    { return *find(evaluate(what, {}, "linux"), topic::confinement); };

    REQUIRE(confinement(seen).status == state::ok);
    REQUIRE_EQ(confinement(seen).why, std::string(reason::unconfined));

    seen.linux_host.label = parse_security_label("/usr/bin/vss_gui (complain)");
    REQUIRE(confinement(seen).status == state::info);
    REQUIRE_EQ(confinement(seen).why, std::string(reason::apparmor_profile));
    REQUIRE_EQ(confinement(seen).subject, std::string("/usr/bin/vss_gui"));

    // A bare label such as "kernel" (WSL, some containers) is not a profile.
    seen = linux_with({});
    seen.linux_host.label = parse_security_label("kernel\n");
    REQUIRE(confinement(seen).status == state::ok);
    REQUIRE_EQ(confinement(seen).why, std::string(reason::unconfined));

    seen = linux_with({});
    seen.linux_host.selinux_mode = "enforcing";
    REQUIRE_EQ(confinement(seen).why, std::string(reason::selinux_enforcing));
    seen.linux_host.selinux_mode = "permissive";
    REQUIRE_EQ(confinement(seen).why, std::string(reason::unconfined));
}

TEST_CASE("linux permissions: the security label is read as AppArmor or "
          "SELinux")
{
    auto label = parse_security_label("unconfined\n");
    REQUIRE_EQ(label.module, std::string("apparmor"));
    REQUIRE_EQ(label.mode, std::string("unconfined"));

    label = parse_security_label("/usr/bin/vss_gui (enforce)");
    REQUIRE_EQ(label.module, std::string("apparmor"));
    REQUIRE_EQ(label.name, std::string("/usr/bin/vss_gui"));
    REQUIRE_EQ(label.mode, std::string("enforce"));

    label = parse_security_label("snap.studio.studio (enforce)");
    REQUIRE_EQ(label.name, std::string("snap.studio.studio"));

    label = parse_security_label("system_u:system_r:unconfined_service_t:s0");
    REQUIRE_EQ(label.module, std::string("selinux"));
    REQUIRE_EQ(label.name, std::string("unconfined_service_t"));

    REQUIRE(parse_security_label("").module.empty());
    REQUIRE_EQ(apparmor_profile_file("/usr/bin/vss_gui"),
               std::string("usr.bin.vss_gui"));
    REQUIRE(apparmor_profile_file("snap.studio.studio").empty());
    REQUIRE(apparmor_snippet("snap.studio.studio").find("not a file") !=
            std::string::npos);
}

TEST_CASE("linux permissions: a listener names the firewall command only when "
          "a front end is on")
{
    auto seen = linux_with({});
    const auto ask = listening_on("0.0.0.0", 4001);
    const auto firewall = [&](const observation& what)
    { return *find(evaluate(what, ask, "linux"), topic::linux_firewall); };

    // This computer only: nothing to say.
    const auto local = evaluate(seen, listening_on("127.0.0.1", 4001), "linux");
    REQUIRE_EQ(find(local, topic::linux_firewall)->why,
               std::string(reason::loopback_only));

    // Neither ufw nor firewalld: the rules cannot be read without root.
    REQUIRE(firewall(seen).status == state::unknown);
    REQUIRE_EQ(firewall(seen).why, std::string(reason::rules_need_root));

    seen.linux_host.ufw_active = tri::yes;
    REQUIRE(firewall(seen).status == state::info);
    REQUIRE_EQ(firewall(seen).remedy, std::string("sudo ufw allow 4001/tcp"));

    seen.linux_host.ufw_active = tri::no;
    seen.linux_host.firewalld_active = tri::yes;
    REQUIRE_EQ(firewall(seen).remedy,
               std::string("sudo firewall-cmd --permanent --add-port=4001/tcp "
                           "&& sudo firewall-cmd --reload"));

    // Not known at all: say so.
    seen.linux_host.ufw_active = tri::unknown;
    seen.linux_host.firewalld_active = tri::unknown;
    REQUIRE_EQ(firewall(seen).why, std::string(reason::not_asked));

    listener udp;
    udp.protocol = transport_protocol::udp;
    udp.port = 5000;
    REQUIRE_EQ(ufw_command(udp), std::string("sudo ufw allow 5000/udp"));
}

TEST_CASE("linux permissions: the group step names only the user and a short "
          "list of groups")
{
    group_membership_spec spec;
    spec.user = "alice";
    for (const char* group : {"dialout", "uucp", "tty", "lock"})
    {
        spec.group = group;
        REQUIRE(validate(spec).empty());
    }
    for (const char* group :
         {"sudo", "root", "wheel", "docker", "", "dialout;id"})
    {
        spec.group = group;
        REQUIRE(!validate(spec).empty());
    }
    spec.group = "dialout";
    for (const char* user :
         {"", "Alice", "al ice", "a;b", "$(id)", "-x", "0day", "alice\n"})
    {
        spec.user = user;
        REQUIRE(!validate(spec).empty());
    }
    spec.user = "_svc-1";
    REQUIRE(validate(spec).empty());
    REQUIRE_EQ(usermod_command(spec),
               std::string("sudo usermod -aG dialout _svc-1"));

    // An invalid spec never reaches polkit.
    spec.user = "root;id";
    REQUIRE(add_user_to_group(spec).status == step_status::invalid);
}
