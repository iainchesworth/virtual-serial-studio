//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the operating system may stop Studio doing, and the smallest thing the
// user can do about it.
//
// Windows asks for elevation (UAC) and filters inbound connections (Windows
// Firewall). Linux limits serial devices to groups and may confine a program
// (AppArmor, SELinux, Flatpak, Snap). macOS has an application firewall,
// privacy prompts and a sandbox. This header is the one place the GUI and the
// command line ask about all of it.
//
// The design follows three rules:
//
//  * Honest. A platform that cannot be asked answers `state::unknown`, and the
//    caller says so. Nothing is guessed from a missing answer.
//  * Explained. A fact carries a reason code and, where there is one, the exact
//    command or text the user can run. The GUI turns the code into a sentence
//    (translated); the library never writes user-facing prose.
//  * Never silent. Nothing in this header changes a protection. A step that does
//    (a firewall rule) runs only when the caller asks for it by name, after the
//    user has agreed, in a separate elevated process that does that one thing.
//
// `observe()` reads the host and is the only part that depends on the platform.
// `evaluate()` turns an observation into facts and is plain code, so every
// outcome is tested on every platform with injected observations.
//

#ifndef VSS_PORTS_PERMISSIONS_HPP
#define VSS_PORTS_PERMISSIONS_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vss::permissions
{

/// How a fact stands.
enum class state
{
    /// Nothing stands in the way.
    ok,
    /// Nothing is wrong, but the user should know (for example "this needs
    /// administrator rights when you create a pair").
    info,
    /// Something is blocked or missing and Studio cannot work around it.
    attention,
    /// The platform cannot be asked, or the question failed. Say so.
    unknown,
    /// The fact does not apply to this operating system or situation.
    not_applicable
};

/// What a fact is about.
enum class topic
{
    /// Windows: is this process elevated, and could the user elevate it.
    windows_elevation,
    /// Windows: would Windows Firewall let other computers reach a listener.
    windows_firewall
};

enum class transport_protocol
{
    tcp,
    udp
};

/// A socket Studio listens on, or is about to.
struct listener
{
    transport_protocol protocol = transport_protocol::tcp;
    /// Empty means every address, as in `tcp-listen://:4001`.
    std::string host;
    /// Zero when the port is not chosen yet.
    std::uint16_t port = 0;
};

/// What the check is for.
struct context
{
    /// The program that listens. Empty: this executable.
    std::string program_path;
    /// The listeners the check should cover. None: only the general facts.
    std::vector<listener> listeners;
};

/// One reason code per outcome. The GUI chooses the sentence from the topic and
/// the reason, and the tests assert them, so a code is never reused for a new
/// meaning.
namespace reason
{
inline constexpr std::string_view none = "";
// windows_elevation
inline constexpr std::string_view elevated = "elevated";
inline constexpr std::string_view admin_not_elevated = "admin_not_elevated";
inline constexpr std::string_view standard_user = "standard_user";
// windows_firewall
inline constexpr std::string_view loopback_only = "loopback_only";
inline constexpr std::string_view firewall_off = "firewall_off";
inline constexpr std::string_view allow_rule = "allow_rule";
inline constexpr std::string_view no_allow_rule = "no_allow_rule";
inline constexpr std::string_view blocked_by_rule = "blocked_by_rule";
inline constexpr std::string_view block_all_inbound = "block_all_inbound";
// every topic
inline constexpr std::string_view not_asked = "not_asked";
inline constexpr std::string_view other_platform = "other_platform";
} // namespace reason

/// The networks a rule can apply to, as the bits Windows Firewall uses.
inline constexpr long profile_domain = 1;
inline constexpr long profile_private = 2;
inline constexpr long profile_public = 4;
inline constexpr long profile_all = 0x7fffffff;

/// Which computers a rule lets in.
enum class rule_scope
{
    /// The local subnet only (`LocalSubnet`). The default.
    local_subnet,
    /// Any address. Only when the user chooses it.
    any_address
};

/// The only firewall rule Studio makes: one program, one protocol, one port,
/// one scope, on the networks the computer is on now.
struct firewall_rule_spec
{
    std::string program;
    transport_protocol protocol = transport_protocol::tcp;
    std::uint16_t port = 0;
    rule_scope scope = rule_scope::local_subnet;
    /// Bits of `profile_*`. Never `profile_all`.
    long profiles = profile_private;
};

/// One answer.
struct fact
{
    topic what = topic::windows_elevation;
    state status = state::unknown;
    /// One of `reason::*`.
    std::string why;
    /// The thing the fact is about: a program path, a port, a group, a device.
    std::string subject;
    /// The exact command the user can run, shown and copied but never run by
    /// Studio. Empty when there is none.
    std::string remedy;
    /// True when Studio can do the step itself, in an elevated helper, after
    /// the user agrees. The step is named by `firewall_rule_spec`.
    bool can_act = false;
    /// The step itself, when `can_act`: what the confirmation shows and what the
    /// elevated helper does.
    std::optional<firewall_rule_spec> step;
    /// For a listener fact, the listener it is about (zero: none).
    std::uint16_t port = 0;
    transport_protocol protocol = transport_protocol::tcp;
};

// ---------------------------------------------------------------------------
// What the host says
// ---------------------------------------------------------------------------

/// A yes or no that may not be known.
enum class tri
{
    unknown,
    no,
    yes
};

/// How a rule or the firewall treats one listener.
enum class firewall_verdict
{
    unknown,
    /// No enabled rule mentions the program and the port.
    no_rule,
    /// An enabled allow rule covers the program, protocol and port.
    allowed,
    /// An enabled block rule covers them. Block wins over allow.
    blocked
};

/// A Windows firewall rule, reduced to what decides a listener.
struct firewall_rule_record
{
    bool enabled = false;
    bool inbound = true;
    bool allow = true;
    /// Empty: any program.
    std::string program;
    /// 6 TCP, 17 UDP, 256 any.
    int protocol = 256;
    /// The Windows list: "*", "4001", "4000-4010,5000". Empty or "*" is any.
    std::string local_ports;
    /// Bit 1 domain, 2 private, 4 public; 0x7fffffff is every profile.
    long profiles = 0x7fffffff;
};


struct windows_observation
{
    tri elevated = tri::unknown;
    /// The account is an administrator, elevated or not (a split token).
    tri administrator = tri::unknown;
    /// The firewall is on for at least one active profile.
    tri firewall_on = tri::unknown;
    /// The active profile blocks every inbound connection, rules or not.
    tri block_all_inbound = tri::unknown;
    /// Bits of `profile_*` for the networks the computer is on now. Zero when
    /// not known.
    long active_profiles = 0;
    /// Every rule, when the rules could be read.
    std::optional<std::vector<firewall_rule_record>> rules;
};

struct observation
{
    windows_observation windows;
};

/// Reads the host. Nothing is changed. Takes a moment on Windows, where the
/// firewall rules are read, so call it off the GUI thread.
observation observe(const context& ask = {});

/// Turns an observation into facts. The same input always gives the same facts.
std::vector<fact> evaluate(const observation& seen, const context& ask,
                           std::string_view host);

/// `observe` then `evaluate` for this host.
std::vector<fact> probe(const context& ask = {});

/// "windows", "linux" or "macos".
std::string_view host_name() noexcept;

// ---------------------------------------------------------------------------
// Firewall rules (Windows)
// ---------------------------------------------------------------------------

/// True for 127.0.0.0/8, ::1 and the name `localhost`. An empty host is every
/// address and so is not loopback.
bool is_loopback_host(std::string_view host);

/// Reads `tcp-listen://host:port` and `udp-listen://host:port` (the host may be
/// empty or a bracketed IPv6 address). False for anything else.
bool parse_listen_uri(std::string_view uri, listener& out);

/// True when the Windows list covers the port: "*", "80", "10-20,30".
bool port_list_covers(std::string_view list, std::uint16_t port) noexcept;

/// Whether `rules` let a listener in. `program` is compared without regard to
/// case. Block rules win.
firewall_verdict firewall_verdict_for(
    const std::vector<firewall_rule_record>& rules, std::string_view program,
    const listener& what, long active_profiles);

/// Empty when the spec may be applied, otherwise why not.
std::string validate(const firewall_rule_spec& spec);

/// "Virtual Serial Studio TCP 4001". A rule with this name is ours, and the
/// removal step deletes by this name only.
std::string firewall_rule_name(const firewall_rule_spec& spec);

/// Empty when `spec.program` is one of our two programs (`vss_gui.exe`,
/// `vss_cli.exe`) in the same folder as `helper_path`, the elevated helper.
/// The helper refuses any other program, so the step cannot be used to open the
/// firewall for a program that is not Studio.
std::string program_is_ours(const firewall_rule_spec& spec,
                            std::string_view helper_path);

/// The group every rule of ours belongs to.
std::string_view firewall_rule_group() noexcept;

/// The command an administrator could run for the same rule, for the dialog to
/// show and copy.
std::string netsh_add_command(const firewall_rule_spec& spec);
std::string netsh_delete_command(const firewall_rule_spec& spec);

/// The arguments of `vss_cli` for the elevated step, and the reverse.
std::vector<std::string> elevated_arguments(const firewall_rule_spec& spec,
                                            bool remove);
struct parsed_step
{
    firewall_rule_spec spec;
    bool remove = false;
};
std::optional<parsed_step>
parse_elevated_arguments(const std::vector<std::string>& arguments);

/// The result of an elevated step.
enum class step_status
{
    done,
    /// The user declined the elevation prompt.
    declined,
    /// This platform or build cannot run the step.
    unsupported,
    /// Not elevated: the step must run with administrator rights.
    not_elevated,
    /// The spec failed `validate`.
    invalid,
    failed
};

struct step_result
{
    step_status status = step_status::failed;
    /// English text for the log, never shown as is.
    std::string detail;
};

/// Applies or removes the rule. Needs administrator rights; call it only from
/// the elevated helper (`vss_cli permissions firewall-allow`).
step_result apply_firewall_rule(const firewall_rule_spec& spec, bool remove);

/// Starts `executable` with `arguments` elevated, through the operating system's
/// own prompt, waits and returns its exit code as a status. The user sees the
/// prompt and may decline.
step_result run_elevated(const std::string& executable,
                         const std::vector<std::string>& arguments);

/// Exit codes of the elevated helper, so `run_elevated` can report them.
inline constexpr int exit_done = 0;
inline constexpr int exit_invalid = 4;
inline constexpr int exit_not_elevated = 3;
inline constexpr int exit_failed = 5;

/// True when this build can make and remove the firewall rule (Windows).
bool can_manage_firewall_rules() noexcept;

/// True when this build can start a helper elevated (Windows).
bool can_run_elevated() noexcept;

/// Full path of this program.
std::string this_program();

} // namespace vss::permissions

#endif // VSS_PORTS_PERMISSIONS_HPP
