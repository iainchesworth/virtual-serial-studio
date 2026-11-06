//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "backend/engine_studio_backend.hpp"

#include "backend/event_bridge.hpp"
#include "backend/platform/com0com_platform.hpp"

#include <vss/core/hub.hpp>
#include <vss/ports/com0com.hpp>
#include <vss/ports/peer_open.hpp>
#include <vss/ports/permissions.hpp>
#include <vss/studio/studio.hpp>

#include <QMetaObject>
#include <QString>
#include <QTimer>

#include <algorithm>
#include <cstddef>
#include <unordered_map>
#include <utility>

namespace vss::gui
{
namespace
{

QVector<vss::endpoint_id> to_vector(const std::vector<vss::endpoint_id>& ids)
{
    QVector<vss::endpoint_id> out;
    out.reserve(static_cast<qsizetype>(ids.size()));
    for (const vss::endpoint_id id : ids)
        out.push_back(id);
    return out;
}

peer_state to_peer_state(vss::peer_open_state state)
{
    switch (state)
    {
    case vss::peer_open_state::open:
        return peer_state::open;
    case vss::peer_open_state::not_open:
        return peer_state::not_open;
    case vss::peer_open_state::unknown:
        break;
    }
    return peer_state::unknown;
}

port_view make_port(
    const vss::endpoint_info& info,
    const std::unordered_map<vss::endpoint_id, vss::peer_open_state>& peers)
{
    port_view view;
    view.id = info.id;
    view.name = QString::fromStdString(info.name);
    view.path = QString::fromStdString(info.path);
    view.description = QString::fromStdString(info.description);
    view.kind = info.kind;
    view.state = info.state;
    view.serial = info.serial;
    view.lines = info.lines;
    view.stats = info.stats;
    view.supports_control_lines = info.supports_control_lines;
    view.open = info.open;
    view.studio_only = info.kind == vss::endpoint_kind::mock ||
                       view.path.startsWith(QStringLiteral("loopback:"));
    // Only a pseudo terminal on a platform that can inspect other processes has
    // a definite answer.  Every other port stays unknown, and the GUI must
    // not claim what it cannot know.
    if (const auto found = peers.find(info.id); found != peers.end())
        view.peer = to_peer_state(found->second);
    return view;
}

link_view make_link(const vss::link_info& info)
{
    link_view view;
    view.id = info.id;
    view.mode = info.mode;
    view.options = info.options;
    view.sources = to_vector(info.sources);
    view.targets = to_vector(info.targets);
    view.owner = info.owner;
    view.stats = info.stats;

    if (info.mode == vss::link_mode::duplex)
    {
        view.shape = info.sources.size() == 1 && info.targets.size() == 1
                         ? link_shape::two_way
                         : link_shape::split;
    }
    else if (!info.options.emulate_modem_lines)
    {
        view.shape = link_shape::monitor;
    }
    else
    {
        view.shape =
            info.sources.size() > 1 ? link_shape::join : link_shape::one_way;
    }
    return view;
}

bool same_port(const port_view& left, const port_view& right)
{
    return left.id == right.id && left.name == right.name &&
           left.path == right.path && left.description == right.description &&
           left.kind == right.kind && left.state == right.state &&
           left.lines == right.lines && left.open == right.open &&
           left.supports_control_lines == right.supports_control_lines &&
           left.studio_only == right.studio_only && left.peer == right.peer &&
           left.stats.bytes_rx == right.stats.bytes_rx &&
           left.stats.bytes_tx == right.stats.bytes_tx &&
           left.stats.read_ops == right.stats.read_ops &&
           left.stats.write_ops == right.stats.write_ops &&
           left.stats.errors == right.stats.errors &&
           left.stats.dropped == right.stats.dropped;
}

bool same_link(const link_view& left, const link_view& right)
{
    return left.id == right.id && left.shape == right.shape &&
           left.mode == right.mode &&
           left.options.policy == right.options.policy &&
           left.options.emulate_modem_lines ==
               right.options.emulate_modem_lines &&
           left.sources == right.sources && left.targets == right.targets &&
           left.owner == right.owner && left.stats.bytes == right.stats.bytes &&
           left.stats.messages == right.stats.messages &&
           left.stats.dropped == right.stats.dropped;
}

QString platform_name()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#else
    return QStringLiteral("linux");
#endif
}

} // namespace

EngineStudioBackend::EngineStudioBackend(vss::studio& studio, QObject* parent)
    : StudioBackend(parent), studio_(studio),
      bridge_(std::make_unique<EventBridge>(studio.engine()))
{
    connect(bridge_.get(), &EventBridge::batch_ready, this,
            [this](const event_batch& events)
            {
                for (const auto& event : events)
                {
                    if (event.kind == vss::event_kind::lines)
                        Q_EMIT lines_changed(event.endpoint);
                }
                Q_EMIT batch_ready(events);
                rebuild();
            });
    bridge_->start();

    // Opening a pseudo terminal raises no engine event, so a program that
    // connects or goes away is noticed by looking again once a second.
    if (vss::peer_open_supported())
    {
        peer_timer_ = new QTimer(this);
        peer_timer_->setInterval(1000);
        connect(peer_timer_, &QTimer::timeout, this,
                [this] { rebuild(); });
        peer_timer_->start();
    }
    rebuild();
}

EngineStudioBackend::~EngineStudioBackend()
{
    bridge_->stop();
}

capabilities EngineStudioBackend::caps() const
{
    capabilities caps = capabilities{};
    // Boost.VirtualSerial's network backends only connect. Waiting for other
    // computers is `vss_ports`' listen transport (`tcp-listen://`,
    // `udp-listen://` and `rfc2217-listen://`), which
    // `vss::studio::attach_serial` opens.
    caps.network_server = true;
    // `vss::run_benchmark` is the core `vss_cli bench` uses, shared with the
    // GUI.
    caps.benchmark = true;
    // `vss::studio::inject_fault` reaches the mock fixture behind a port.
    caps.faults = true;
    // A pseudo terminal on Linux can be checked for a program that has it
    // open; every other port reads as unknown and the GUI says nothing.
    caps.peer_open = vss::peer_open_supported();
    // The window's Undo history reverses changes with exact inverse
    // operations (see `shell/undo_steps.cpp`), not by restoring a snapshot.
    caps.undo = true;
    // `vss::hub::set_link_wiring` holds the wires of every link.
    caps.line_matrix = true;
    // The JSON editor places a schema error on its line with a path index of
    // the text, and asks `vss::config::from_json` which field it refused.
    caps.json_positions = true;
    // `vss::studio::rename_port` renames the endpoint, the studio record
    // and a pseudo terminal's stable link together.
    caps.rename_port = true;
    caps.com0com_pair_creation =
        detail::platform_supports_com0com_pair_creation();
    // The rule is made by the elevated command-line helper; the platform must
    // have the step and be able to start a program through its own prompt.
    caps.firewall_rule = vss::permissions::can_manage_firewall_rules() &&
                         vss::permissions::can_run_elevated();
    return caps;
}

vss::result<vss::benchmark_result> EngineStudioBackend::run_benchmark(
    vss::endpoint_id from, vss::endpoint_id to,
    const vss::benchmark_options& options,
    const vss::benchmark_progress_handler& progress,
    const std::atomic<bool>* cancel)
{
    // Runs on a worker thread: it uses only the hub's lookup, which is safe
    // from any thread, and holds the endpoints so a removal cannot free them.
    const auto sender = studio_.engine().find_endpoint(from);
    const auto receiver = studio_.engine().find_endpoint(to);
    if (!sender || !receiver)
        return vss::make_error_code(vss::errc::not_found);
    return vss::run_benchmark(*sender, *receiver, options, progress, cancel);
}

std::optional<line_wiring> EngineStudioBackend::link_wiring(vss::link_id id) const
{
    const auto info = studio_.engine().find_link(id);
    if (!info)
        return std::nullopt;
    const vss::modem_wiring& wires = info->options.wiring;
    line_wiring out;
    out.dtr_to_dsr = wires.dtr_to_dsr;
    out.dtr_to_cts = wires.dtr_to_cts;
    out.dtr_to_dcd = wires.dtr_to_dcd;
    out.dtr_to_ri = wires.dtr_to_ri;
    out.rts_to_dsr = wires.rts_to_dsr;
    out.rts_to_cts = wires.rts_to_cts;
    out.rts_to_dcd = wires.rts_to_dcd;
    out.rts_to_ri = wires.rts_to_ri;
    return out;
}

vss::result<void> EngineStudioBackend::set_link_wiring(vss::link_id id,
                                                       const line_wiring& wiring)
{
    vss::modem_wiring wires;
    wires.dtr_to_dsr = wiring.dtr_to_dsr;
    wires.dtr_to_cts = wiring.dtr_to_cts;
    wires.dtr_to_dcd = wiring.dtr_to_dcd;
    wires.dtr_to_ri = wiring.dtr_to_ri;
    wires.rts_to_dsr = wiring.rts_to_dsr;
    wires.rts_to_cts = wiring.rts_to_cts;
    wires.rts_to_dcd = wiring.rts_to_dcd;
    wires.rts_to_ri = wiring.rts_to_ri;
    const auto changed = studio_.engine().set_link_wiring(id, wires);
    if (!changed)
        return changed.error();
    rebuild();
    return {};
}

vss::result<void> EngineStudioBackend::inject_fault(vss::endpoint_id id,
                                                    vss::fault_kind fault)
{
    return studio_.inject_fault(id, fault);
}

vss::result<vss::endpoint_id> EngineStudioBackend::create_virtual_port(
    const QString& name, vss::virtual_port_kind kind, bool capture)
{
    vss::endpoint_options options = studio_.default_options();
    options.capture_traffic = capture;
    const auto created =
        studio_.create_virtual_port(name.toStdString(), kind, options);
    if (!created)
        return created.error();
    rebuild();
    return (*created)->id();
}

vss::result<vss::endpoint_id>
EngineStudioBackend::create_auto_port(const QString& name, bool capture)
{
    // `studio::create_virtual_port` treats pty as the auto selector and falls
    // back to the best available kind on platforms without a pseudo
    // terminal.  This is the documented auto path, not a simulation.
    return create_virtual_port(name, vss::virtual_port_kind::pty, capture);
}

vss::result<vss::endpoint_id>
EngineStudioBackend::create_mock_port(const QString& name,
                                      const vss::serial_settings& settings,
                                      const QString& target)
{
    const auto created = studio_.create_mock_port(name.toStdString(), settings,
                                                  target.toStdString());
    if (!created)
        return created.error();
    rebuild();
    return (*created)->id();
}

vss::result<vss::endpoint_id>
EngineStudioBackend::attach_serial(const QString& name, const QString& uri,
                                   const vss::serial_settings& settings,
                                   bool capture)
{
    vss::endpoint_options options = studio_.default_options();
    options.capture_traffic = capture;
    const auto attached = studio_.attach_serial(
        name.toStdString(), uri.toStdString(), settings, options);
    if (!attached)
        return attached.error();
    rebuild();
    return (*attached)->id();
}

vss::result<void> EngineStudioBackend::remove_port(vss::endpoint_id id)
{
    const auto removed = studio_.remove_port(id);
    if (!removed)
        return removed.error();
    rebuild();
    return {};
}

vss::result<void> EngineStudioBackend::rename_port(vss::endpoint_id id,
                                                   const QString& name)
{
    const auto renamed = studio_.rename_port(id, name.toStdString());
    if (!renamed)
        return renamed.error();
    rebuild();
    return {};
}

std::vector<device_view> EngineStudioBackend::devices() const
{
    std::vector<device_view> out;
    for (const auto& device : studio_.serial_devices())
    {
        device_view view;
        view.path = QString::fromStdString(device.path);
        view.description = QString::fromStdString(device.description);
        view.hardware_id = QString::fromStdString(device.hardware_id);
        view.virtual_device = device.virtual_device;
        view.com0com = device.com0com;
        view.com0com_pair = device.com0com_pair;
        view.com0com_side = QString::fromStdString(device.com0com_side);
        out.push_back(std::move(view));
    }
    return out;
}

std::vector<traffic_view> EngineStudioBackend::traffic(int limit) const
{
    std::vector<traffic_view> out;
    const auto records =
        studio_.engine().traffic(static_cast<std::size_t>(std::max(limit, 0)));
    out.reserve(records.size());
    for (const auto& record : records)
    {
        traffic_view view;
        view.endpoint = record.endpoint;
        view.name = QString::fromStdString(record.name);
        view.direction = record.direction;
        view.when = record.when;
        if (record.data)
        {
            view.data =
                QByteArray(reinterpret_cast<const char*>(record.data->data()),
                           static_cast<qsizetype>(record.data->size()));
        }
        out.push_back(std::move(view));
    }
    return out;
}

setup_facts EngineStudioBackend::facts() const
{
    setup_facts facts;
    const auto installation = studio_.detect_com0com();
    facts.com0com_found = installation.found;
    facts.com0com_pairs = static_cast<int>(installation.pairs.size());
    facts.serial_devices = static_cast<int>(studio_.serial_devices().size());
    facts.platform = platform_name();
    // No platform helper proves elevation yet.  Leave it unknown rather than
    // telling the user something the build cannot check.
    facts.elevation_known = false;
    facts.elevated = false;
    return facts;
}

std::optional<port_view> EngineStudioBackend::port(vss::endpoint_id id) const
{
    const auto it =
        std::find_if(ports_.begin(), ports_.end(),
                     [id](const port_view& view) { return view.id == id; });
    if (it == ports_.end())
        return std::nullopt;
    return *it;
}

std::optional<link_view> EngineStudioBackend::link(vss::link_id id) const
{
    const auto it =
        std::find_if(links_.begin(), links_.end(),
                     [id](const link_view& view) { return view.id == id; });
    if (it == links_.end())
        return std::nullopt;
    return *it;
}

vss::result<vss::link_id>
EngineStudioBackend::connect_ports(vss::endpoint_id first,
                                   vss::endpoint_id second,
                                   const vss::link_options& options)
{
    const auto linked = studio_.engine().connect(first, second, options);
    if (!linked)
        return linked.error();
    rebuild();
    return *linked;
}

vss::result<vss::link_id>
EngineStudioBackend::split(vss::endpoint_id source,
                           const std::vector<vss::endpoint_id>& targets,
                           const vss::link_options& options)
{
    const auto linked = studio_.engine().split(source, targets, options);
    if (!linked)
        return linked.error();
    rebuild();
    return *linked;
}

vss::result<vss::link_id>
EngineStudioBackend::join(const std::vector<vss::endpoint_id>& sources,
                          vss::endpoint_id target,
                          const vss::link_options& options)
{
    const auto linked = studio_.engine().join(sources, target, options);
    if (!linked)
        return linked.error();
    rebuild();
    return *linked;
}

vss::result<vss::link_id>
EngineStudioBackend::redirect(vss::endpoint_id from, vss::endpoint_id to,
                              const vss::link_options& options)
{
    const auto linked = studio_.engine().redirect(from, to, options);
    if (!linked)
        return linked.error();
    rebuild();
    return *linked;
}

vss::result<vss::link_id> EngineStudioBackend::monitor(vss::endpoint_id from,
                                                       vss::endpoint_id to)
{
    const auto linked = studio_.engine().monitor(from, to);
    if (!linked)
        return linked.error();
    rebuild();
    return *linked;
}

vss::result<void> EngineStudioBackend::disconnect(vss::link_id id)
{
    const auto removed = studio_.engine().disconnect(id);
    if (!removed)
        return removed.error();
    rebuild();
    return {};
}

vss::result<void> EngineStudioBackend::set_owner(vss::link_id id,
                                                 vss::endpoint_id owner)
{
    const auto changed = studio_.engine().set_owner(id, owner);
    if (!changed)
        return changed.error();
    rebuild();
    return {};
}

vss::result<void> EngineStudioBackend::release_owner(vss::link_id id)
{
    const auto changed = studio_.engine().release_owner(id);
    if (!changed)
        return changed.error();
    rebuild();
    return {};
}

vss::result<void> EngineStudioBackend::send(vss::endpoint_id id,
                                            const QByteArray& data)
{
    const auto endpoint = studio_.engine().find_endpoint(id);
    if (!endpoint)
        return vss::make_error_code(vss::errc::not_found);
    if (!endpoint->is_open())
        return vss::make_error_code(vss::errc::not_open);

    const vss::bytes_view bytes(
        reinterpret_cast<const vss::byte*>(data.constData()),
        static_cast<std::size_t>(data.size()));
    // A queue-full rejection is published as a hub error event and reaches
    // the GUI through the coalesced batch.  There is deliberately no
    // per-write callback into the backend: it would have to touch a QObject
    // from the endpoint strand.
    endpoint->write(bytes);
    return {};
}

vss::result<void> EngineStudioBackend::set_line(vss::endpoint_id id,
                                                vss::control_line line,
                                                bool state)
{
    const auto endpoint = studio_.engine().find_endpoint(id);
    if (!endpoint)
        return vss::make_error_code(vss::errc::not_found);
    if (!endpoint->info().supports_control_lines)
        return vss::make_error_code(vss::errc::unsupported);
    if (const auto error = endpoint->set_line(line, state))
        return error;
    Q_EMIT lines_changed(id);
    return {};
}

void EngineStudioBackend::refresh_lines(vss::endpoint_id id)
{
    const auto endpoint = studio_.engine().find_endpoint(id);
    if (!endpoint)
        return;
    endpoint->refresh_lines();
    Q_EMIT lines_changed(id);
}

vss::config::document EngineStudioBackend::snapshot() const
{
    return studio_.snapshot();
}

vss::result<void>
EngineStudioBackend::apply(const vss::config::document& document,
                           bool rollback_on_error)
{
    const auto applied = studio_.apply(document, rollback_on_error);
    if (!applied)
        return applied.error();
    rebuild();
    return {};
}

vss::result<share_view> EngineStudioBackend::share_serial(const QString& uri,
                                                          int listeners)
{
    const auto shared = studio_.share_serial(uri.toStdString(), listeners);
    if (!shared)
        return shared.error();

    share_view view;
    view.source = shared->source;
    view.link = shared->link;
    view.listeners.reserve(static_cast<qsizetype>(shared->listeners.size()));
    for (const vss::endpoint_id id : shared->listeners)
        view.listeners.push_back(id);
    rebuild();
    return view;
}

vss::result<vss::link_id> EngineStudioBackend::bridge(const QString& from_uri,
                                                      const QString& to_uri)
{
    const auto linked =
        studio_.bridge(from_uri.toStdString(), to_uri.toStdString());
    if (!linked)
        return linked.error();
    rebuild();
    return *linked;
}

vss::result<void> EngineStudioBackend::create_com0com_pair()
{
    return detail::platform_create_com0com_pair(studio_);
}

bool EngineStudioBackend::hub_running() const
{
    return studio_.engine().running();
}

void EngineStudioBackend::refresh()
{
    rebuild();
}

void EngineStudioBackend::set_refresh_interval(int milliseconds)
{
    bridge_->set_interval(milliseconds);
}

void EngineStudioBackend::rebuild()
{
    std::vector<port_view> next_ports;
    const auto infos = studio_.endpoint_infos();
    next_ports.reserve(infos.size());
    std::unordered_map<vss::endpoint_id, vss::peer_open_state> peers;
    if (vss::peer_open_supported())
        peers = studio_.peer_states();
    for (const auto& info : infos)
        next_ports.push_back(make_port(info, peers));

    std::vector<link_view> next_links;
    const auto links = studio_.link_infos();
    next_links.reserve(links.size());
    for (const auto& info : links)
        next_links.push_back(make_link(info));

    const bool ports_differ = next_ports.size() != ports_.size() ||
                              !std::equal(next_ports.begin(), next_ports.end(),
                                          ports_.begin(), same_port);
    const bool links_differ = next_links.size() != links_.size() ||
                              !std::equal(next_links.begin(), next_links.end(),
                                          links_.begin(), same_link);

    ports_ = std::move(next_ports);
    links_ = std::move(next_links);

    if (ports_differ)
        Q_EMIT ports_changed();
    if (links_differ)
        Q_EMIT links_changed();
}

} // namespace vss::gui
