//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Per-component micro-benchmarks for vss_perf.
//
// Each benchmark exercises one component on its own, so a regression points at
// a component and a Tracy capture shows that component's zones under a
// `bench.<name>` zone. Every figure is a rate the CI can compare with a floor
// in perf/floors.json.
//

#include "perf_common.hpp"

#include <vss/config/document.hpp>
#include <vss/core/bytes.hpp>
#include <vss/core/profile.hpp>
#include <vss/core/serial_settings.hpp>
#include <vss/core/traffic_log.hpp>
#include <vss/studio/studio.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace vss_perf
{
namespace
{

using clock_type = std::chrono::steady_clock;

vss::config::document make_document(std::size_t ports)
{
    vss::config::document document;
    document.name = "component-benchmark";
    for (std::size_t index = 0; index < ports; ++index)
    {
        vss::config::port_entry port;
        port.name = "port-" + std::to_string(index);
        port.kind = "virtual";
        port.virtual_kind = "loopback";
        port.serial.baud_rate = 115200;
        document.ports.push_back(port);
    }
    for (std::size_t index = 0; index + 1 < ports; index += 2)
    {
        vss::config::link_entry link;
        link.mode = "duplex";
        link.sources = {"port-" + std::to_string(index)};
        link.targets = {"port-" + std::to_string(index + 1)};
        document.links.push_back(link);
    }
    return document;
}

measurement bench_bytes_hex()
{
    VSS_PROFILE_ZONE("bench.bytes_hex");

    const vss::byte_buffer payload(64 * 1024, 0x5a);
    constexpr std::size_t iterations = 100;
    std::size_t checked = 0;

    const auto start = clock_type::now();
    for (std::size_t index = 0; index < iterations; ++index)
    {
        const std::string text = vss::to_hex(vss::bytes_view(payload));
        const auto back = vss::from_hex(text);
        if (back && back->size() == payload.size())
            checked += payload.size();
    }
    const double elapsed = seconds_since(start);
    return {"bytes_hex",
            checked == 0
                ? 0.0
                : static_cast<double>(checked) / elapsed / (1024.0 * 1024.0),
            "MB/s", "to_hex and from_hex round trip"};
}

measurement bench_traffic_log()
{
    VSS_PROFILE_ZONE("bench.traffic_log");

    vss::traffic_log log(8192);
    const auto data =
        std::make_shared<const vss::byte_buffer>(vss::byte_buffer(64, 0x33));
    constexpr std::size_t count = 400'000;

    const auto start = clock_type::now();
    for (std::size_t index = 0; index < count; ++index)
    {
        vss::traffic_record record;
        record.endpoint = 1;
        record.name = "port";
        record.data = data;
        log.push(std::move(record));
        if ((index % 50'000) == 0)
            static_cast<void>(log.snapshot(512));
    }
    const double elapsed = seconds_since(start);
    return {"traffic_log_push", static_cast<double>(count) / elapsed,
            "records/s", "bounded ring, snapshots every 50k"};
}

measurement bench_settings_label()
{
    VSS_PROFILE_ZONE("bench.serial_settings");

    vss::serial_settings settings;
    settings.baud_rate = 115200;
    constexpr std::size_t count = 200'000;
    std::size_t length = 0;

    const auto start = clock_type::now();
    for (std::size_t index = 0; index < count; ++index)
    {
        settings.baud_rate = 9600 + static_cast<unsigned>(index % 8) * 9600;
        if (!vss::validate(settings))
            length += vss::to_label(settings).size();
    }
    const double elapsed = seconds_since(start);
    return {"serial_settings_label",
            length == 0 ? 0.0 : static_cast<double>(count) / elapsed, "ops/s",
            "validate and label"};
}

measurement bench_config_validate()
{
    VSS_PROFILE_ZONE("bench.config_validate");

    const auto document = make_document(200);
    constexpr std::size_t count = 2'000;
    std::size_t valid = 0;

    const auto start = clock_type::now();
    for (std::size_t index = 0; index < count; ++index)
    {
        if (!vss::config::validate(document))
            ++valid;
    }
    const double elapsed = seconds_since(start);
    return {"config_validate",
            valid == 0 ? 0.0 : static_cast<double>(valid) / elapsed, "ops/s",
            "200 ports, 100 links"};
}

measurement bench_studio_apply(std::size_t iterations)
{
    VSS_PROFILE_ZONE("bench.studio_apply");

    const auto document = make_document(40);
    std::size_t applied = 0;

    const auto start = clock_type::now();
    for (std::size_t index = 0; index < iterations; ++index)
    {
        vss::studio app;
        if (app.apply(document))
            ++applied;
    }
    const double elapsed = seconds_since(start);
    return {"studio_apply",
            applied == 0 ? 0.0 : static_cast<double>(applied) / elapsed,
            "ops/s", "start a studio and apply 40 ports, 20 links"};
}

measurement bench_studio_snapshot(std::size_t iterations)
{
    VSS_PROFILE_ZONE("bench.studio_snapshot");

    vss::studio app;
    if (!app.apply(make_document(80)))
        return {"studio_snapshot", 0.0, "ops/s", "apply failed"};

    std::size_t seen = 0;
    const auto start = clock_type::now();
    for (std::size_t index = 0; index < iterations; ++index)
    {
        seen += app.endpoint_infos().size();
        seen += app.link_infos().size();
        seen += app.snapshot().ports.size();
    }
    const double elapsed = seconds_since(start);
    return {"studio_snapshot",
            seen == 0 ? 0.0 : static_cast<double>(iterations) / elapsed,
            "ops/s", "endpoint_infos, link_infos and snapshot, 80 ports"};
}

measurement bench_studio_ports(std::size_t iterations)
{
    VSS_PROFILE_ZONE("bench.studio_ports");

    vss::studio app;
    std::size_t done = 0;
    const auto start = clock_type::now();
    for (std::size_t index = 0; index < iterations; ++index)
    {
        const std::string name = "bench-" + std::to_string(index);
        const auto port =
            app.create_virtual_port(name, vss::virtual_port_kind::loopback);
        if (port && app.remove_port(name))
            ++done;
    }
    const double elapsed = seconds_since(start);
    return {"studio_port_cycle",
            done == 0 ? 0.0 : static_cast<double>(done) / elapsed, "ops/s",
            "create and remove a loopback port"};
}

} // namespace

std::vector<measurement> run_component_benchmarks(bool quick)
{
    std::vector<measurement> results;
    results.push_back(bench_bytes_hex());
    results.push_back(bench_traffic_log());
    results.push_back(bench_settings_label());
    results.push_back(bench_config_validate());
    results.push_back(bench_studio_ports(quick ? 50 : 200));
    results.push_back(bench_studio_snapshot(quick ? 50 : 300));
    results.push_back(bench_studio_apply(quick ? 5 : 20));
    return results;
}

} // namespace vss_perf
