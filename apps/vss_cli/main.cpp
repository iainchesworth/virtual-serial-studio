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

#include <vss/studio/studio.hpp>
#include <vss/testkit/harness.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <map>
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
        "  vss_cli bridge <uri> <uri>\n"
        "      Join two transports back to back (for example serial to tcp).\n"
        "\n"
        "  vss_cli loopback [--seconds=N]\n"
        "      A virtual port, a redirect and a monitor: no hardware needed.\n"
        "\n"
        "  vss_cli virtual [--kind=auto|loopback|pty] [--seconds=N]\n"
        "      Create one virtual port and print the path to open from an "
        "app.\n"
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
        "      Apply a saved topology and keep it alive.\n"
        "\n"
        "  vss_cli bench --from=<uri> --to=<uri> [--bytes=1MB] [--chunk=4KB]\n"
        "      Measure a real end-to-end transfer.\n"
        "\n"
        "  vss_cli selftest\n"
        "      Check the wiring without touching hardware.\n"
        "\n"
        "  Common: --verbose, --quiet\n"
        "\n"
        "Virtual Serial Studio is free software under the GNU General Public\n"
        "License, version 3 or later.  This program comes with ABSOLUTELY NO\n"
        "WARRANTY; see LICENSE for details.\n");
}

std::string format_rate(std::uint64_t bytes, std::chrono::milliseconds elapsed)
{
    if (elapsed.count() <= 0)
        return "n/a";
    const double seconds = static_cast<double>(elapsed.count()) / 1000.0;
    return vss::format_byte_size(static_cast<std::uint64_t>(
               static_cast<double>(bytes) / seconds))
        + "/s";
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
        std::printf("  [%llu] %-8s %zu -> %zu  bytes=%llu dropped=%llu\n",
                    static_cast<unsigned long long>(link.id),
                    vss::to_string(link.mode), link.sources.size(),
                    link.targets.size(),
                    static_cast<unsigned long long>(link.stats.bytes),
                    static_cast<unsigned long long>(link.stats.dropped));
    }
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

int command_bridge(const arguments& args)
{
    if (args.positional.size() < 3)
    {
        std::fprintf(stderr, "bridge needs two URIs\n");
        return 2;
    }

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

    studio app;
    const auto created = automatic
        ? app.create_virtual_port("virtual", vss::virtual_port_kind::pty)
        : app.create_virtual_port("virtual", requested);
    if (!created)
    {
        std::fprintf(stderr, "cannot create the virtual port: %s\n",
                     created.error().message().c_str());
        return 1;
    }

    const std::string path = app.advertised_path((*created)->id());
    std::printf("virtual port %s\n", (*created)->name().c_str());
    std::printf("open this from your application: %s\n", path.c_str());
    std::printf("transport: %s\n", (*created)->description().c_str());

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
                     applied.error().message().c_str());
        return 1;
    }

    print_topology(app);
    run_for(app, args.number("--seconds", 0), args.has("--verbose"));
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

    const vss::byte_buffer payload = vss::testkit::make_pattern(*bytes, 11);
    const auto started = std::chrono::steady_clock::now();

    for (std::size_t offset = 0; offset < payload.size(); offset += *chunk)
    {
        const std::size_t size = std::min<std::size_t>(*chunk, payload.size() - offset);
        const auto written = (*sender)->write_sync(
            vss::bytes_view(payload.data() + offset, size), 10s);
        if (!written)
        {
            std::fprintf(stderr, "write failed at offset %zu\n", offset);
            return 1;
        }
    }

    const bool complete = vss::testkit::wait_for_rx(**receiver, payload.size(), 60s);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);

    // Print sizes through the formatter: uint64_t is `unsigned long long` on
    // macOS and `unsigned long` on Linux, so %zu would not match both.
    std::printf("sent      : %s in %s byte chunks\n",
                vss::format_byte_size(payload.size()).c_str(),
                vss::format_byte_size(*chunk).c_str());
    std::printf("received  : %s\n",
                vss::format_byte_size((*receiver)->stats().bytes_rx).c_str());
    std::printf("elapsed   : %lld ms\n", static_cast<long long>(elapsed.count()));
    std::printf("throughput: %s\n", format_rate((*receiver)->stats().bytes_rx, elapsed).c_str());

    if (!complete || (*receiver)->stats().bytes_rx < payload.size())
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

arguments parse(int argc, char** argv)
{
    arguments parsed;
    for (int index = 1; index < argc; ++index)
    {
        const std::string value = argv[index];
        if (value.rfind("--", 0) == 0)
        {
            const auto equals = value.find('=');
            if (equals == std::string::npos)
                parsed.flags.push_back(value);
            else
                parsed.options[value.substr(0, equals)] = value.substr(equals + 1);
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
        if (command == "bench")
            return command_bench(args);
        if (command == "selftest")
            return command_selftest();
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
