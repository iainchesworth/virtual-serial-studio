//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// vss_cli - the headless face of Virtual Serial Studio.
//
// It owns the same vss::studio as the GUI, so everything the GUI can build
// can be scripted, measured and put in CI.
//

#include "permissions_command.hpp"

#include <vss/ports/listen_transport.hpp>
#include <vss/studio/benchmark.hpp>
#include <vss/studio/capture.hpp>
#include <vss/studio/replay.hpp>
#include <vss/studio/studio.hpp>
#include <vss/testkit/harness.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;
using vss::endpoint_id;
using vss::studio;

struct arguments
{
    std::vector<std::string> positional;
    std::map<std::string, std::string> options;
    /// Every value an option was given, for the ones that may repeat.
    std::map<std::string, std::vector<std::string>> repeated;
    std::vector<std::string> flags;

    bool has(const std::string& flag) const
    {
        return std::find(flags.begin(), flags.end(), flag) != flags.end();
    }

    std::string get(const std::string& key, const std::string& fallback = {}) const
    {
        const auto it = options.find(key);
        return it == options.end() ? fallback : it->second;
    }

    std::vector<std::string> all(const std::string& key) const
    {
        const auto it = repeated.find(key);
        return it == repeated.end() ? std::vector<std::string>{} : it->second;
    }

    int number(const std::string& key, int fallback) const
    {
        const auto it = options.find(key);
        if (it == options.end())
            return fallback;
        try
        {
            return std::stoi(it->second);
        }
        catch (...)
        {
            return fallback;
        }
    }
};

void print_usage()
{
    std::printf(
        "Virtual Serial Studio command line\n"
        "\n"
        "  vss_cli devices [--json]\n"
        "      List serial ports and com0com pairs on this machine.\n"
        "\n"
        "  vss_cli share <uri> [--listeners=N] [--seconds=N]\n"
        "      Open one port and give every listener its own virtual port.\n"
        "\n"
        "  vss_cli join <uri> <uri> [<uri>...] --target=<uri>\n"
        "      Merge several publishers into one serial port.\n"
        "\n"
        "  vss_cli bus [<uri>...] [--virtual=N] [--tap=<uri>|virtual]\n"
        "              [--kind=auto|loopback|pty|windows-driver] "
        "[--seconds=N]\n"
        "      Put ports on one shared multi-drop bus: each hears every other\n"
        "      and none hears itself.  Each <uri> is a port to attach; "
        "--virtual\n"
        "      adds N virtual ports for other programs to open.  --tap adds a\n"
        "      listen-only port that hears every member and sends to none.\n"
        "      Runs until Ctrl-C or SIGTERM, or for N seconds.\n"
        "\n"
        "  vss_cli bridge <uri> <uri>\n"
        "      Join two transports back to back (for example serial to tcp).\n"
        "      tcp-listen://host:port and udp-listen://host:port wait for "
        "other\n"
        "      computers to connect instead of connecting out.\n"
        "      ?allow=a,b,c after the port (addresses or ranges such as\n"
        "      192.168.1.0/24) lets only those computers in; the others are\n"
        "      closed at once. For example:\n"
        "        vss_cli bridge serial://COM3 "
        "rfc2217-listen://192.168.1.10:7001?allow=192.168.1.0/24\n"
        "      RFC 2217 carries the baud rate, framing and modem lines. It "
        "has\n"
        "      no password: listen on 127.0.0.1, or add allow=.\n"
        "\n"
        "  vss_cli loopback [--seconds=N]\n"
        "      A virtual port, a redirect and a monitor: no hardware needed.\n"
        "\n"
        "  vss_cli virtual [--kind=auto|loopback|pty|windows-driver] [--com=N]\n"
        "                  [--seconds=N]\n"
        "      Create one virtual port and print the path to open from an "
        "app.\n"
        "      --com=N (1 to 255) asks the Windows driver for COM<N>.  It "
        "needs the\n"
        "      driver and its helper service, and fails when another device "
        "has the\n"
        "      number.  Other systems and kinds of port have no COM numbers.\n"
        "      It runs until Ctrl-C or SIGTERM, or for N seconds.\n"
        "\n"
        "  vss_cli vport --name=<name>[,<name>...] [--link=<a>:<b>[,...]]\n"
        "                [--stable-dir=<dir>] [--seconds=N]\n"
        "      Create pseudo terminals with stable names (Linux and macOS). "
        "Each\n"
        "      name is a symbolic link an application can open; --link joins "
        "two\n"
        "      of the ports like a null-modem cable.  Runs until Ctrl-C or "
        "SIGTERM.\n"
        "\n"
        "  vss_cli run <topology.json> [--seconds=N]\n"
        "      Apply a saved topology and keep it alive.  A virtual port with "
        "a\n"
        "      \"com_number\" gets that COM number from the Windows driver.\n"
        "\n"
        "  vss_cli capture [<topology.json>] [--uri=<uri>...] --out=<file>\n"
        "                  [--format=cap|csv|jsonl] [--port=<name>[,<name>...]]\n"
        "                  [--seconds=N]\n"
        "      Record every byte that crosses the ports, each with its time in\n"
        "      milliseconds from the start and the port it belongs to.  cap is\n"
        "      the `[ms] R|W 0x..|0x..` text format, one line per chunk, and\n"
        "      has no port names; csv and jsonl carry the port, the direction\n"
        "      and the absolute start time.  R is bytes the port received and\n"
        "      W is bytes written to it, so use --port to keep one row per\n"
        "      message.\n"
        "      The format comes from the file name when --format is left out.\n"
        "      Runs until Ctrl-C or SIGTERM, or for N seconds.\n"
        "\n"
        "  vss_cli play <file> [<topology.json>] (--uri=<uri> | --port=<name>)\n"
        "               [--speed=X] [--loop] [--direction=R|W] [--from=<name>]\n"
        "               [--seconds=N]\n"
        "      Write the records of a .cap or .jsonl capture into one port at\n"
        "      the times they were recorded, so a recorded master or device can\n"
        "      drive a bus without the original program.  --uri opens the port\n"
        "      a program would open; --port names a port of the topology.  R\n"
        "      (the default) sends the bytes the recorded port received, W the\n"
        "      bytes written to it.  --from keeps the records of one recorded\n"
        "      port.  A loop lasts as long as the capture did.\n"
        "\n"
        "  vss_cli bench --from=<uri> --to=<uri> [--bytes=1MB] [--chunk=4KB]\n"
        "      Measure a real end-to-end transfer.\n"
        "\n"
        "  vss_cli selftest\n"
        "      Check the wiring without touching hardware.\n"
        "\n"
        "%s"
        "  Common: --verbose, --quiet\n"
        "\n"
        "Virtual Serial Studio is free software under the GNU General Public\n"
        "License, version 3 or later.  This program comes with ABSOLUTELY NO\n"
        "WARRANTY; see LICENSE for details.\n",
        vss_cli::permissions_usage());
}

void print_topology(studio& app)
{
    const auto endpoints = app.endpoint_infos();
    std::printf("ports: %zu\n", endpoints.size());
    for (const auto& info : endpoints)
    {
        std::printf("  [%llu] %-14s %-10s %-24s %-8s %s  rx=%s tx=%s total=%s\n",
                    static_cast<unsigned long long>(info.id), info.name.c_str(),
                    vss::to_string(info.kind), info.path.c_str(),
                    vss::to_string(info.state),
                    vss::to_string(info.lines).c_str(),
                    vss::format_byte_size(info.stats.bytes_rx).c_str(),
                    vss::format_byte_size(info.stats.bytes_tx).c_str(),
                    vss::format_byte_size(info.stats.total_bytes()).c_str());
    }

    const auto links = app.link_infos();
    std::printf("links: %zu\n", links.size());
    for (const auto& link : links)
    {
        if (link.mode == vss::link_mode::bus)
        {
            std::printf("  [%llu] %-8s %zu members  bytes=%llu dropped=%llu\n",
                        static_cast<unsigned long long>(link.id),
                        vss::to_string(link.mode), link.sources.size(),
                        static_cast<unsigned long long>(link.stats.bytes),
                        static_cast<unsigned long long>(link.stats.dropped));
            continue;
        }
        std::printf("  [%llu] %-8s %zu -> %zu  bytes=%llu dropped=%llu\n",
                    static_cast<unsigned long long>(link.id),
                    vss::to_string(link.mode), link.sources.size(),
                    link.targets.size(),
                    static_cast<unsigned long long>(link.stats.bytes),
                    static_cast<unsigned long long>(link.stats.dropped));
    }

    // A script that reads this through a pipe needs the table at once, not when
    // the process exits.
    std::fflush(stdout);
}

void run_for(studio& app, int seconds, bool verbose)
{
    if (seconds <= 0)
        return;

    const auto started = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - started < std::chrono::seconds(seconds))
    {
        std::this_thread::sleep_for(500ms);
        if (verbose)
            print_topology(app);
    }
    print_topology(app);
}

std::atomic<bool> g_stop_requested{false};

extern "C" void request_stop(int)
{
    g_stop_requested.store(true);
}

/// Starts catching SIGINT and SIGTERM.  A command that stays alive calls this
/// before it prints anything: a script that has read the path may signal at
/// once, and a signal that arrives before the handler exists kills the process
/// with its links still in place.
void catch_stop_signals()
{
    g_stop_requested.store(false);
    std::signal(SIGINT, request_stop);
    std::signal(SIGTERM, request_stop);
}

/// Keeps the studio alive until a signal, or for `seconds` when that is more
/// than zero.  Needs catch_stop_signals() first.  Returns true when a signal
/// ended it.
bool run_until_stopped(studio& app, int seconds, bool verbose)
{
    const auto started = std::chrono::steady_clock::now();
    auto last_print = started;
    while (!g_stop_requested.load())
    {
        const auto now = std::chrono::steady_clock::now();
        if (seconds > 0 && now - started >= std::chrono::seconds(seconds))
            break;
        if (verbose && now - last_print >= 500ms)
        {
            print_topology(app);
            last_print = now;
        }
        std::this_thread::sleep_for(100ms);
    }
    return g_stop_requested.load();
}

int command_devices(const arguments& args)
{
    const auto devices = vss::enumerate_serial_devices();
    const auto installation = vss::detect_com0com();

    if (args.has("--json"))
    {
        std::printf("{\n  \"devices\": [\n");
        for (std::size_t index = 0; index < devices.size(); ++index)
        {
            const auto& device = devices[index];
            std::printf(
                "    {\"path\":\"%s\",\"description\":\"%s\",\"virtual\":%s,"
                "\"com0com\":%s,\"pair\":%d,\"side\":\"%s\"}%s\n",
                device.path.c_str(), device.description.c_str(),
                device.virtual_device ? "true" : "false",
                device.com0com ? "true" : "false", device.com0com_pair,
                device.com0com_side.c_str(),
                index + 1 < devices.size() ? "," : "");
        }
        std::printf("  ],\n  \"com0com\": {\"found\": %s, \"pairs\": %zu}\n}\n",
                    installation.found ? "true" : "false",
                    installation.pairs.size());
        return 0;
    }

    std::printf("serial ports: %zu\n", devices.size());
    for (const auto& device : devices)
    {
        std::printf("  %-16s %s%s\n", device.path.c_str(),
                    device.description.c_str(),
                    device.com0com ? "  (com0com)" : "");
    }

    std::printf("com0com: %s\n", installation.found ? "installed" : "not found");
    for (const auto& pair : installation.pairs)
        std::printf("  pair %d: %s <-> %s\n", pair.index, pair.a.c_str(),
                    pair.b.c_str());
    return 0;
}

int command_share(const arguments& args)
{
    if (args.positional.size() < 2)
    {
        std::fprintf(stderr, "share needs a URI\n");
        return 2;
    }

    const std::string uri = args.positional[1];
    const int listeners = std::max(1, args.number("--listeners", 2));
    const int seconds = args.number("--seconds", 0);

    studio app;
    const auto shared = app.share_serial(uri, listeners);
    if (!shared)
    {
        std::fprintf(stderr, "share failed: %s\n", shared.error().message().c_str());
        return 1;
    }

    std::printf("shared %s with %zu listener(s)\n", uri.c_str(),
                shared->listeners.size());
    for (const endpoint_id id : shared->listeners)
    {
        const auto endpoint = app.engine().find_endpoint(id);
        std::printf("  %-14s %s\n", endpoint ? endpoint->name().c_str() : "?",
                    app.advertised_path(id).c_str());
    }

    run_for(app, seconds, args.has("--verbose"));
    return 0;
}

int command_join(const arguments& args)
{
    std::string target = args.get("--target");
    if (target.empty() || args.positional.size() < 3)
    {
        std::fprintf(stderr, "join needs at least one URI and --target=<uri>\n");
        return 2;
    }

    std::vector<std::string> sources(args.positional.begin() + 1,
                                     args.positional.end());
    studio app;
    const auto link = app.join_ports(sources, target);
    if (!link)
    {
        std::fprintf(stderr, "join failed: %s\n", link.error().message().c_str());
        return 1;
    }

    std::printf("joined %zu publisher(s) into %s (link %llu)\n", sources.size(),
                target.c_str(), static_cast<unsigned long long>(*link));
    run_for(app, args.number("--seconds", 0), args.has("--verbose"));
    return 0;
}

bool parse_virtual_kind(const std::string& kind, vss::virtual_port_kind& out)
{
    if (kind == "auto" || kind == "pty")
        out = vss::virtual_port_kind::pty;
    else if (kind == "loopback")
        out = vss::virtual_port_kind::loopback;
    else if (kind == "windows-driver")
        out = vss::virtual_port_kind::windows_driver;
    else
        return false;
    return true;
}

int command_bus(const arguments& args)
{
    catch_stop_signals();

    vss::virtual_port_kind kind = vss::virtual_port_kind::pty;
    if (!parse_virtual_kind(args.get("--kind", "auto"), kind))
    {
        std::fprintf(stderr, "unknown --kind=%s\n", args.get("--kind").c_str());
        return 2;
    }

    const int virtual_count = args.number("--virtual", 0);
    const std::size_t uri_count = args.positional.size() - 1;
    if (virtual_count < 0 ||
        uri_count + static_cast<std::size_t>(virtual_count) < 2)
    {
        std::fprintf(stderr, "bus needs at least two members: give URIs or "
                             "--virtual=N\n");
        return 2;
    }

    studio app;
    std::vector<std::string> members;

    for (std::size_t index = 1; index < args.positional.size(); ++index)
    {
        const std::string name = "member-" + std::to_string(members.size() + 1);
        const auto attached = app.attach_serial(name, args.positional[index]);
        if (!attached)
        {
            std::fprintf(stderr, "cannot open %s: %s\n",
                         args.positional[index].c_str(),
                         attached.error().message().c_str());
            return 1;
        }
        members.push_back((*attached)->name());
        std::printf("%s -> %s\n", (*attached)->name().c_str(),
                    args.positional[index].c_str());
    }

    for (int index = 0; index < virtual_count; ++index)
    {
        const std::string name = "member-" + std::to_string(members.size() + 1);
        const auto created = app.create_virtual_port(name, kind);
        if (!created)
        {
            std::fprintf(stderr, "cannot create %s: %s\n", name.c_str(),
                         created.error().message().c_str());
            return 1;
        }
        members.push_back((*created)->name());
        std::printf("%s -> %s\n", (*created)->name().c_str(),
                    app.advertised_path((*created)->id()).c_str());
    }

    const auto bus = app.bus(members);
    if (!bus)
    {
        std::fprintf(stderr, "bus failed: %s\n", bus.error().message().c_str());
        return 1;
    }

    // The tap is outside the bus: a join from every member that the bus does
    // not feed back, so the program on it hears everything and sends nothing.
    const std::string tap = args.get("--tap");
    if (!tap.empty())
    {
        const auto opened = tap == "virtual"
                                ? app.create_virtual_port("tap", kind)
                                : app.attach_serial("tap", tap);
        if (!opened)
        {
            std::fprintf(stderr, "cannot open the tap: %s\n",
                         opened.error().message().c_str());
            return 1;
        }
        const auto tapped = app.join(members, (*opened)->name());
        if (!tapped)
        {
            std::fprintf(stderr, "cannot tap the bus: %s\n",
                         tapped.error().message().c_str());
            return 1;
        }
        std::printf("tap -> %s\n",
                    tap == "virtual"
                        ? app.advertised_path((*opened)->id()).c_str()
                        : tap.c_str());
    }

    std::printf("bus of %zu members (link %llu)\n", members.size(),
                static_cast<unsigned long long>(*bus));
    std::fflush(stdout);
    run_until_stopped(app, args.number("--seconds", 0), args.has("--verbose"));
    return 0;
}

/// RFC 2217 has no password, so a server that other computers can reach, and
/// that has no allow list, is worth a line of warning.
void warn_if_reachable(const std::string& uri)
{
    if (!uri.starts_with("rfc2217-listen://"))
        return;
    const auto address = vss::parse_listen_uri(uri);
    if (!address || !address->allow.empty() ||
        vss::listens_on_loopback(*address))
        return;
    std::fprintf(stderr,
                 "warning: %s can be reached from other computers, and RFC "
                 "2217 has no password. Anyone who can reach the port can use "
                 "the serial port. Listen on 127.0.0.1, or add "
                 "?allow=192.168.1.0/24 (the addresses that may connect).\n",
                 uri.c_str());
}

int command_bridge(const arguments& args)
{
    if (args.positional.size() < 3)
    {
        std::fprintf(stderr, "bridge needs two URIs\n");
        return 2;
    }

    warn_if_reachable(args.positional[1]);
    warn_if_reachable(args.positional[2]);

    studio app;
    const auto link = app.bridge(args.positional[1], args.positional[2]);
    if (!link)
    {
        std::fprintf(stderr, "bridge failed: %s\n", link.error().message().c_str());
        return 1;
    }

    std::printf("bridged %s <-> %s (link %llu)\n", args.positional[1].c_str(),
                args.positional[2].c_str(),
                static_cast<unsigned long long>(*link));
    run_for(app, args.number("--seconds", 0), args.has("--verbose"));
    return 0;
}

int command_loopback(const arguments& args)
{
    studio app;

    const auto source =
        app.create_virtual_port("console", vss::virtual_port_kind::loopback);
    if (!source)
    {
        std::fprintf(stderr, "cannot create the virtual port\n");
        return 1;
    }
    const auto monitor =
        app.create_virtual_port("monitor", vss::virtual_port_kind::loopback);
    if (!monitor)
    {
        std::fprintf(stderr, "cannot create the monitor port\n");
        return 1;
    }

    const auto direct = app.redirect((*source)->name(), (*monitor)->name());
    if (!direct)
    {
        std::fprintf(stderr, "cannot link the ports\n");
        return 1;
    }

    std::printf("virtual port : %s\n", app.advertised_path((*source)->id()).c_str());
    std::printf("monitor port : %s\n", app.advertised_path((*monitor)->id()).c_str());

    (*source)->write(vss::as_bytes("hello from Virtual Serial Studio\n"));
    std::this_thread::sleep_for(200ms);

    const auto traffic = app.engine().traffic();
    for (const auto& record : traffic)
    {
        if (record.direction == vss::data_direction::rx && record.data)
        {
            std::printf("  %s <- %zu bytes: %s", record.name.c_str(),
                        record.data->size(),
                        vss::to_printable(vss::bytes_view(*record.data)).c_str());
        }
    }

    run_for(app, args.number("--seconds", 0), args.has("--verbose"));
    return 0;
}

/// The COM number of --com=N: 0 when there is none, and -1 when the option is
/// not a whole number from 1 to 255.
int com_option(const arguments& args)
{
    if (args.has("--com"))
        return -1;
    const auto found = args.options.find("--com");
    if (found == args.options.end())
        return 0;

    const std::string& text = found->second;
    if (text.empty() || text.size() > 3
        || !std::all_of(text.begin(), text.end(),
                        [](char c) { return c >= '0' && c <= '9'; }))
        return -1;
    const int number = std::stoi(text);
    return number >= 1 && number <= 255 ? number : -1;
}

/// What went wrong when a port was asked for a COM number, in words that say
/// what to do about it.
std::string com_failure(const boost::system::error_code& error, int number)
{
    if (number != 0)
    {
        if (error == vss::make_error_code(vss::errc::already_exists))
            return "COM" + std::to_string(number)
                   + " is already used by another device (a real port, com0com"
                     " or another virtual port); choose another number";
        if (error == vss::make_error_code(vss::errc::unsupported))
            return "COM numbers need the Virtual Serial Studio driver and its "
                   "helper service (vsscom_helper), and one of them is missing"
                   " or too old";
        if (error == vss::make_error_code(vss::errc::device_busy))
            return "a program has COM" + std::to_string(number) + " open";
    }
    return error.message();
}

int command_virtual(const arguments& args)
{
    catch_stop_signals();
    const std::string kind = args.get("--kind", "auto");

    vss::virtual_port_kind requested = vss::virtual_port_kind::loopback;
    bool automatic = true;
    if (kind == "loopback")
    {
        automatic = false;
        requested = vss::virtual_port_kind::loopback;
    }
    else if (kind == "pty")
    {
        automatic = false;
        requested = vss::virtual_port_kind::pty;
    }
    else if (kind == "windows-driver")
    {
        automatic = false;
        requested = vss::virtual_port_kind::windows_driver;
    }
    else if (kind != "auto")
    {
        std::fprintf(stderr, "unknown --kind=%s\n", kind.c_str());
        return 2;
    }

    const int com = com_option(args);
    if (com < 0)
    {
        std::fprintf(stderr,
                     "--com wants a COM number from 1 to 255, as in --com=7\n");
        return 2;
    }
    if (com != 0 && !automatic
        && requested != vss::virtual_port_kind::windows_driver)
    {
        std::fprintf(stderr,
                     "--com is for the Windows driver; --kind=%s has no COM "
                     "numbers\n",
                     kind.c_str());
        return 2;
    }

    studio app;
    const auto created = automatic
        ? app.create_virtual_port("virtual", vss::virtual_port_kind::pty, {},
                                  static_cast<unsigned>(com))
        : app.create_virtual_port("virtual", requested, {},
                                  static_cast<unsigned>(com));
    if (!created)
    {
        std::fprintf(stderr, "cannot create the virtual port: %s\n",
                     com_failure(created.error(), com).c_str());
        return 1;
    }

    const std::string path = app.advertised_path((*created)->id());
    std::printf("virtual port %s\n", (*created)->name().c_str());
    std::printf("open this from your application: %s\n", path.c_str());
    std::printf("transport: %s\n", (*created)->description().c_str());
    std::fflush(stdout);

    if (args.has("--verbose"))
    {
        for (const auto& device : vss::enumerate_serial_devices())
            std::printf("  device: %s\n", device.path.c_str());
    }

    // A script reading this through a pipe must see the path now, not when
    // the buffer fills or the process ends.
    std::fflush(stdout);
    run_until_stopped(app, args.number("--seconds", 0), false);
    return 0;
}

std::vector<std::string> split_list(const std::string& text, char separator)
{
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= text.size())
    {
        const auto end = text.find(separator, start);
        const auto stop = end == std::string::npos ? text.size() : end;
        if (stop > start)
            parts.push_back(text.substr(start, stop - start));
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return parts;
}

int command_vport(const arguments& args)
{
    catch_stop_signals();
    if (!vss::stable_links_supported())
    {
        std::fprintf(stderr,
                     "vport needs pseudo terminals, which this platform does "
                     "not have.\nUse `vss_cli virtual` instead.\n");
        return 2;
    }

    const auto names = split_list(args.get("--name"), ',');
    if (names.empty())
    {
        std::fprintf(stderr, "vport needs --name=<name>[,<name>...]\n");
        return 2;
    }

    vss::studio_options options;
    options.stable_links = true;
    options.stable_dir = args.get("--stable-dir");
    studio app(options);

    for (const auto& name : names)
    {
        const auto created =
            app.create_virtual_port(name, vss::virtual_port_kind::pty);
        if (!created)
        {
            std::fprintf(stderr, "cannot create %s: %s\n", name.c_str(),
                         created.error().message().c_str());
            return 1;
        }
        // A name the studio had to change (it was already taken) would print
        // something other than what the operator asked for.
        if ((*created)->name() != name)
        {
            std::fprintf(stderr, "cannot create %s: the name is taken\n",
                         name.c_str());
            return 1;
        }
        std::printf("%s -> %s\n", app.advertised_path((*created)->id()).c_str(),
                    (*created)->public_path().c_str());
    }

    for (const auto& pair : split_list(args.get("--link"), ','))
    {
        const auto colon = pair.find(':');
        if (colon == std::string::npos || colon == 0 ||
            colon + 1 == pair.size())
        {
            std::fprintf(stderr, "--link wants <a>:<b>, not %s\n",
                         pair.c_str());
            return 2;
        }
        const auto joined =
            app.connect(pair.substr(0, colon), pair.substr(colon + 1));
        if (!joined)
        {
            std::fprintf(stderr, "cannot link %s: %s\n", pair.c_str(),
                         joined.error().message().c_str());
            return 1;
        }
        std::printf("linked %s\n", pair.c_str());
    }

    std::fflush(stdout);
    run_until_stopped(app, args.number("--seconds", 0), args.has("--verbose"));
    return 0;
}

/// The first COM number a topology asks for, or 0: enough to word the failure.
int first_com_number(const vss::config::document& document)
{
    for (const auto& port : document.ports)
    {
        if (port.com_number != 0)
            return static_cast<int>(port.com_number);
    }
    return 0;
}

int command_run(const arguments& args)
{
    if (args.positional.size() < 2)
    {
        std::fprintf(stderr, "run needs a topology file\n");
        return 2;
    }

    const auto document = vss::config::load_file(args.positional[1]);
    if (!document)
    {
        std::fprintf(stderr, "cannot load %s: %s\n", args.positional[1].c_str(),
                     document.error().message().c_str());
        return 1;
    }

    studio app;
    if (const auto applied = app.apply(*document); !applied)
    {
        std::fprintf(stderr, "cannot apply the topology: %s\n",
                     com_failure(applied.error(), first_com_number(*document))
                         .c_str());
        return 1;
    }

    print_topology(app);
    run_for(app, args.number("--seconds", 0), args.has("--verbose"));
    return 0;
}

/// Applies the topology a command names and opens every `--uri`, the way
/// `capture`, `play` and `respond` all start.  The topology is the positional
/// argument at `topology_index`, if there is one.  Returns an exit code, or -1
/// when the ports are ready.
int open_ports(studio& app, const arguments& args, std::size_t topology_index)
{
    if (args.positional.size() > topology_index)
    {
        const auto& path = args.positional[topology_index];
        const auto document = vss::config::load_file(path);
        if (!document)
        {
            std::fprintf(stderr, "cannot load %s: %s\n", path.c_str(),
                         document.error().message().c_str());
            return 1;
        }
        if (const auto applied = app.apply(*document); !applied)
        {
            std::fprintf(stderr, "cannot apply the topology: %s\n",
                         applied.error().message().c_str());
            return 1;
        }
    }

    for (const auto& uri : args.all("--uri"))
    {
        const auto attached = app.attach_serial(uri, uri);
        if (!attached)
        {
            std::fprintf(stderr, "cannot open %s: %s\n", uri.c_str(),
                         attached.error().message().c_str());
            return 1;
        }
    }
    return -1;
}

int command_capture(const arguments& args)
{
    const std::string out = args.get("--out");
    if (out.empty())
    {
        std::fprintf(stderr, "capture needs --out=<file>\n");
        return 2;
    }
    if (args.positional.size() < 2 && args.all("--uri").empty())
    {
        std::fprintf(stderr,
                     "capture needs a topology file or at least one --uri\n");
        return 2;
    }

    vss::capture_options options;
    if (args.options.count("--format") != 0)
    {
        const auto format = vss::parse_capture_format(args.get("--format"));
        if (!format)
        {
            std::fprintf(stderr, "unknown --format=%s (use cap, csv or jsonl)\n",
                         args.get("--format").c_str());
            return 2;
        }
        options.format = *format;
    }
    else if (const auto guessed = vss::capture_format_for_path(out))
    {
        options.format = *guessed;
    }
    for (const auto& list : args.all("--port"))
    {
        for (const auto& name : split_list(list, ','))
            options.ports.push_back(name);
    }

    catch_stop_signals();
    studio app;
    if (const int code = open_ports(app, args, 1); code >= 0)
        return code;

    for (const auto& name : options.ports)
    {
        if (app.find(name))
            continue;
        std::fprintf(stderr, "there is no port named %s; the ports are:\n",
                     name.c_str());
        for (const auto& info : app.endpoint_infos())
            std::fprintf(stderr, "  %s\n", info.name.c_str());
        return 2;
    }

    auto session = vss::capture_session::start(app.engine(), out, options);
    if (!session)
    {
        std::fprintf(stderr, "cannot write %s: %s\n", out.c_str(),
                     session.error().message().c_str());
        return 1;
    }

    print_topology(app);
    std::printf("capturing %s to %s (%s)\n",
                options.ports.empty() ? "every port" : "the chosen ports",
                out.c_str(), vss::to_string(options.format));
    std::fflush(stdout);

    run_until_stopped(app, args.number("--seconds", 0), false);

    (*session)->stop();
    const bool good = (*session)->good();
    std::printf("wrote %llu record(s), %llu byte(s)\n",
                static_cast<unsigned long long>((*session)->records()),
                static_cast<unsigned long long>((*session)->bytes()));
    if (!good)
        std::fprintf(stderr, "writing %s failed\n", out.c_str());
    return good ? 0 : 1;
}

/// The endpoint a `play` or `respond` command acts on: `--port` names a port
/// of the topology, and a single `--uri` stands for itself.
std::shared_ptr<vss::endpoint> chosen_port(studio& app, const arguments& args,
                                           const char* command)
{
    const std::string name = args.get("--port");
    const auto uris = args.all("--uri");
    if (name.empty() && uris.size() != 1)
    {
        std::fprintf(stderr,
                     "%s needs --uri=<uri> or --port=<name> to say which "
                     "port to use\n",
                     command);
        return nullptr;
    }
    const std::string wanted = name.empty() ? uris.front() : name;
    auto port = app.find(wanted);
    if (!port)
    {
        std::fprintf(stderr, "there is no port named %s; the ports are:\n",
                     wanted.c_str());
        for (const auto& info : app.endpoint_infos())
            std::fprintf(stderr, "  %s\n", info.name.c_str());
    }
    return port;
}

int command_play(const arguments& args)
{
    if (args.positional.size() < 2)
    {
        std::fprintf(stderr, "play needs a capture file\n");
        return 2;
    }
    const std::string& file = args.positional[1];

    vss::replay_options options;
    if (args.options.count("--direction") != 0)
    {
        const std::string letter = args.get("--direction");
        if (letter == "R" || letter == "r")
            options.direction = vss::data_direction::rx;
        else if (letter == "W" || letter == "w")
            options.direction = vss::data_direction::tx;
        else
        {
            std::fprintf(stderr, "--direction is R or W, not %s\n",
                         letter.c_str());
            return 2;
        }
    }
    if (args.options.count("--speed") != 0)
    {
        try
        {
            options.speed = std::stod(args.get("--speed"));
        }
        catch (const std::exception&)
        {
            options.speed = 0.0;
        }
        if (!(options.speed > 0.0))
        {
            std::fprintf(stderr, "--speed must be a number above zero\n");
            return 2;
        }
    }
    options.loop = args.has("--loop");
    options.from_port = args.get("--from");

    const auto capture = vss::read_capture_file(file);
    if (!capture)
    {
        std::fprintf(stderr, "cannot read %s: %s\n", file.c_str(),
                     capture.error().message().c_str());
        return 1;
    }
    if (capture->skipped_lines > 0)
        std::fprintf(stderr, "%zu line(s) of %s could not be read and were skipped\n",
                     capture->skipped_lines, file.c_str());

    catch_stop_signals();
    studio app;
    if (const int code = open_ports(app, args, 2); code >= 0)
        return code;
    const auto port = chosen_port(app, args, "play");
    if (!port)
        return 2;

    auto player = vss::replay_player::start(app.engine(), port,
                                            capture->records, options);
    if (!player)
    {
        std::fprintf(stderr, "nothing to play: %s has no %c records%s\n",
                     file.c_str(), vss::direction_letter(options.direction),
                     options.from_port.empty() ? "" : " from that port");
        return 1;
    }

    std::printf("playing %zu record(s) of %s into %s%s\n",
                (*player)->records_per_pass(), file.c_str(),
                port->name().c_str(), options.loop ? " in a loop" : "");
    std::fflush(stdout);

    const int seconds = args.number("--seconds", 0);
    const auto started = std::chrono::steady_clock::now();
    while (!g_stop_requested.load() && !(*player)->finished())
    {
        if (seconds > 0 &&
            std::chrono::steady_clock::now() - started >=
                std::chrono::seconds(seconds))
            break;
        std::this_thread::sleep_for(20ms);
    }
    // The last write is queued on the port; give it a moment to leave.
    if ((*player)->finished())
        std::this_thread::sleep_for(200ms);

    (*player)->stop();
    std::printf("sent %llu record(s), %llu byte(s)\n",
                static_cast<unsigned long long>((*player)->records_sent()),
                static_cast<unsigned long long>((*player)->bytes_sent()));
    if ((*player)->write_errors() > 0)
    {
        std::fprintf(stderr, "%llu write(s) failed\n",
                     static_cast<unsigned long long>((*player)->write_errors()));
        return 1;
    }
    return 0;
}

int command_bench(const arguments& args)
{
    const std::string from = args.get("--from");
    const std::string to = args.get("--to");
    if (from.empty() || to.empty())
    {
        std::fprintf(stderr, "bench needs --from=<uri> and --to=<uri>\n");
        return 2;
    }

    const auto bytes = vss::parse_byte_size(args.get("--bytes", "1MB"));
    if (!bytes)
    {
        std::fprintf(stderr, "invalid --bytes\n");
        return 2;
    }
    const auto chunk = vss::parse_byte_size(args.get("--chunk", "4KB"));
    if (!chunk || *chunk == 0)
    {
        std::fprintf(stderr, "invalid --chunk\n");
        return 2;
    }

    studio app;
    const auto sender = app.attach_serial("sender", from);
    if (!sender)
    {
        std::fprintf(stderr, "cannot open %s: %s\n", from.c_str(),
                     sender.error().message().c_str());
        return 1;
    }
    const auto receiver = app.attach_serial("receiver", to);
    if (!receiver)
    {
        std::fprintf(stderr, "cannot open %s: %s\n", to.c_str(),
                     receiver.error().message().c_str());
        return 1;
    }

    vss::benchmark_options options;
    options.bytes = *bytes;
    options.chunk = static_cast<std::size_t>(*chunk);
    const auto ran = vss::run_benchmark(**sender, **receiver, options);
    if (!ran)
    {
        std::fprintf(stderr, "write failed: %s\n",
                     ran.error().message().c_str());
        return 1;
    }

    // Print sizes through the formatter: uint64_t is `unsigned long long` on
    // macOS and `unsigned long` on Linux, so %zu would not match both.
    std::printf("sent      : %s in %s byte chunks\n",
                vss::format_byte_size(ran->sent).c_str(),
                vss::format_byte_size(*chunk).c_str());
    std::printf("received  : %s\n", vss::format_byte_size(ran->received).c_str());
    std::printf("elapsed   : %lld ms\n",
                static_cast<long long>(ran->elapsed.count()));
    std::printf("throughput: %s\n",
                vss::format_throughput(ran->received, ran->elapsed).c_str());

    if (!ran->complete)
    {
        std::fprintf(stderr, "transfer incomplete\n");
        return 1;
    }
    return 0;
}

int command_selftest()
{
    studio app;

    const auto first =
        app.create_virtual_port("selftest-a", vss::virtual_port_kind::loopback);
    if (!first)
        return 1;
    const auto second =
        app.create_virtual_port("selftest-b", vss::virtual_port_kind::loopback);
    if (!second)
        return 1;

    if (!app.redirect((*first)->name(), (*second)->name()))
        return 1;

    (*first)->write(vss::as_bytes("ping"));
    if (!vss::testkit::wait_until(
            [&] { return (*second)->stats().bytes_tx >= 4; }, 2s))
    {
        std::fprintf(stderr, "data did not flow\n");
        return 1;
    }

    const auto snapshot = app.snapshot();
    if (snapshot.ports.size() != 2 || snapshot.links.size() != 1)
        return 1;

    std::printf("selftest ok: %zu ports, %zu links\n", snapshot.ports.size(),
                snapshot.links.size());
    return 0;
}

/// Options that take a value, so `--out file` means the same as `--out=file`.
bool takes_value(const std::string& name)
{
    static const char* const names[] = {"--out",       "--format", "--port",
                                        "--uri",       "--seconds", "--speed",
                                        "--direction", "--from"};
    return std::find(std::begin(names), std::end(names), name) !=
           std::end(names);
}

arguments parse(int argc, char** argv)
{
    arguments parsed;
    for (int index = 1; index < argc; ++index)
    {
        const std::string value = argv[index];
        if (value.rfind("--", 0) == 0)
        {
            const auto equals = value.find('=');
            if (equals != std::string::npos)
            {
                const auto name = value.substr(0, equals);
                parsed.options[name] = value.substr(equals + 1);
                parsed.repeated[name].push_back(value.substr(equals + 1));
            }
            else if (takes_value(value) && index + 1 < argc)
            {
                parsed.options[value] = argv[index + 1];
                parsed.repeated[value].push_back(argv[index + 1]);
                ++index;
            }
            else
            {
                parsed.flags.push_back(value);
            }
        }
        else
        {
            parsed.positional.push_back(value);
        }
    }
    return parsed;
}

} // namespace

int main(int argc, char** argv)
{
    const arguments args = parse(argc, argv);
    if (args.positional.empty() || args.has("--help") || args.has("-h"))
    {
        print_usage();
        return args.positional.empty() ? 2 : 0;
    }

    const std::string command = args.positional[0];
    try
    {
        if (command == "devices")
            return command_devices(args);
        if (command == "share")
            return command_share(args);
        if (command == "join")
            return command_join(args);
        if (command == "bus")
            return command_bus(args);
        if (command == "bridge")
            return command_bridge(args);
        if (command == "loopback")
            return command_loopback(args);
        if (command == "virtual")
            return command_virtual(args);
        if (command == "vport")
            return command_vport(args);
        if (command == "run")
            return command_run(args);
        if (command == "capture")
            return command_capture(args);
        if (command == "play")
            return command_play(args);
        if (command == "bench")
            return command_bench(args);
        if (command == "selftest")
            return command_selftest();
        if (command == "permissions")
            return vss_cli::command_permissions(
                std::vector<std::string>(argv + 1, argv + argc));
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s failed: %s\n", command.c_str(), error.what());
        return 1;
    }

    std::fprintf(stderr, "unknown command: %s\n", command.c_str());
    print_usage();
    return 2;
}
