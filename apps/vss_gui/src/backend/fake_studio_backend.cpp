//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "backend/fake_studio_backend.hpp"

#include <QStringList>

#include <algorithm>
#include <utility>

namespace vss::gui
{
namespace
{

QString path_for(vss::virtual_port_kind kind, const QString& name,
                 vss::endpoint_id id)
{
    switch (kind)
    {
    case vss::virtual_port_kind::loopback:
        return QStringLiteral("loopback:%1").arg(name);
    case vss::virtual_port_kind::pty:
        return QStringLiteral("/dev/pts/%1").arg(id);
    case vss::virtual_port_kind::named_pipe:
        return QStringLiteral("\\\\.\\pipe\\%1").arg(name);
    case vss::virtual_port_kind::windows_driver:
        return QStringLiteral("COM%1").arg(id);
    }
    return QStringLiteral("fake:%1").arg(name);
}

vss::endpoint_kind kind_for_uri(const QString& uri)
{
    const QString scheme = uri.section(QStringLiteral("://"), 0, 0);
    if (scheme == QLatin1String("tcp"))
        return vss::endpoint_kind::tcp;
    if (scheme == QLatin1String("udp"))
        return vss::endpoint_kind::udp;
    if (scheme == QLatin1String("rfc2217"))
        return vss::endpoint_kind::rfc2217;
    return vss::endpoint_kind::physical;
}

} // namespace

FakeStudioBackend::FakeStudioBackend(QObject* parent) : StudioBackend(parent) {}

void FakeStudioBackend::set_peer_state(vss::endpoint_id id, peer_state state)
{
    if (port_view* view = find_port(id))
    {
        view->peer = state;
        refresh();
    }
}

void FakeStudioBackend::set_devices(std::vector<device_view> devices)
{
    devices_ = std::move(devices);
}

std::optional<port_view> FakeStudioBackend::port(vss::endpoint_id id) const
{
    const auto it =
        std::find_if(ports_.begin(), ports_.end(),
                     [id](const port_view& view) { return view.id == id; });
    if (it == ports_.end())
        return std::nullopt;
    return *it;
}

std::optional<link_view> FakeStudioBackend::link(vss::link_id id) const
{
    const auto it =
        std::find_if(links_.begin(), links_.end(),
                     [id](const link_view& view) { return view.id == id; });
    if (it == links_.end())
        return std::nullopt;
    return *it;
}

QString FakeStudioBackend::unique_name(const QString& base) const
{
    const QString stem = base.isEmpty() ? QStringLiteral("port") : base;
    const auto taken = [this](const QString& candidate)
    {
        return std::any_of(ports_.begin(), ports_.end(),
                           [&candidate](const port_view& view)
                           { return view.name == candidate; });
    };
    if (!taken(stem))
        return stem;
    for (int suffix = 2;; ++suffix)
    {
        const QString candidate = QStringLiteral("%1-%2").arg(stem).arg(suffix);
        if (!taken(candidate))
            return candidate;
    }
}

std::optional<vss::error_code> FakeStudioBackend::take_error()
{
    if (!next_error_)
        return std::nullopt;
    const vss::error_code error = *next_error_;
    next_error_.reset();
    return error;
}

port_view* FakeStudioBackend::find_port(vss::endpoint_id id)
{
    const auto it =
        std::find_if(ports_.begin(), ports_.end(),
                     [id](const port_view& view) { return view.id == id; });
    return it == ports_.end() ? nullptr : &*it;
}

link_view* FakeStudioBackend::find_link(vss::link_id id)
{
    const auto it =
        std::find_if(links_.begin(), links_.end(),
                     [id](const link_view& view) { return view.id == id; });
    return it == links_.end() ? nullptr : &*it;
}

vss::result<vss::endpoint_id>
FakeStudioBackend::create_virtual_port(const QString& name,
                                       vss::virtual_port_kind kind, bool)
{
    if (const auto error = take_error())
        return *error;

    port_view view;
    view.id = next_port_++;
    view.name = unique_name(name);
    view.kind = vss::endpoint_kind::virtual_port;
    view.state = vss::endpoint_state::open;
    view.open = true;
    view.supports_control_lines = true;
    view.path = path_for(kind, view.name, view.id);
    view.description = QStringLiteral("Virtual port %1").arg(view.name);
    view.studio_only = kind == vss::virtual_port_kind::loopback;
    const vss::endpoint_id id = view.id;
    ports_.push_back(std::move(view));
    refresh();
    return id;
}

vss::result<vss::endpoint_id>
FakeStudioBackend::create_auto_port(const QString& name, bool capture)
{
    return create_virtual_port(name, vss::virtual_port_kind::pty, capture);
}

vss::result<vss::endpoint_id>
FakeStudioBackend::create_mock_port(const QString& name,
                                    const vss::serial_settings& settings,
                                    const QString& target)
{
    if (const auto error = take_error())
        return *error;

    port_view view;
    view.id = next_port_++;
    view.name = unique_name(name);
    view.kind = vss::endpoint_kind::mock;
    view.state = vss::endpoint_state::open;
    view.open = true;
    view.supports_control_lines = true;
    view.serial = settings;
    view.path = target;
    view.description = QStringLiteral("Mock fixture %1").arg(target);
    view.studio_only = true;
    const vss::endpoint_id id = view.id;
    ports_.push_back(std::move(view));
    refresh();
    return id;
}

vss::result<vss::endpoint_id>
FakeStudioBackend::attach_serial(const QString& name, const QString& uri,
                                 const vss::serial_settings& settings, bool)
{
    if (const auto error = take_error())
        return *error;

    port_view view;
    view.id = next_port_++;
    view.name = unique_name(name);
    view.kind = kind_for_uri(uri);
    view.state = vss::endpoint_state::open;
    view.open = true;
    view.supports_control_lines = view.kind == vss::endpoint_kind::physical;
    view.serial = settings;
    view.path = uri;
    view.description = uri;
    view.studio_only = false;
    const vss::endpoint_id id = view.id;
    ports_.push_back(std::move(view));
    refresh();
    return id;
}

vss::result<void> FakeStudioBackend::remove_port(vss::endpoint_id id)
{
    if (const auto error = take_error())
        return *error;

    const auto it =
        std::find_if(ports_.begin(), ports_.end(),
                     [id](const port_view& view) { return view.id == id; });
    if (it == ports_.end())
        return vss::make_error_code(vss::errc::not_found);
    ports_.erase(it);

    std::vector<vss::link_id> removed_links;
    for (const auto& view : links_)
    {
        if (view.sources.contains(id) || view.targets.contains(id))
            removed_links.push_back(view.id);
    }
    for (const vss::link_id link_id : removed_links)
        wiring_.remove(link_id);
    std::erase_if(
        links_, [id](const link_view& view)
        { return view.sources.contains(id) || view.targets.contains(id); });
    refresh();
    return {};
}

vss::result<vss::link_id>
FakeStudioBackend::connect_ports(vss::endpoint_id first,
                                 vss::endpoint_id second,
                                 const vss::link_options& options)
{
    if (const auto error = take_error())
        return *error;
    if (first == second)
        return vss::make_error_code(vss::errc::invalid_argument);
    if (!find_port(first) || !find_port(second))
        return vss::make_error_code(vss::errc::not_found);

    return add_link(link_shape::two_way, vss::link_mode::duplex, {first},
                    {second}, options);
}

vss::result<vss::link_id>
FakeStudioBackend::split(vss::endpoint_id source,
                         const std::vector<vss::endpoint_id>& targets,
                         const vss::link_options& options)
{
    if (const auto error = take_error())
        return *error;
    if (targets.empty())
        return vss::make_error_code(vss::errc::invalid_argument);
    if (!find_port(source))
        return vss::make_error_code(vss::errc::not_found);
    for (const vss::endpoint_id target : targets)
    {
        if (!find_port(target) || target == source)
            return vss::make_error_code(vss::errc::invalid_argument);
    }

    return add_link(link_shape::split, vss::link_mode::duplex, {source},
                    targets, options);
}

vss::result<vss::link_id>
FakeStudioBackend::join(const std::vector<vss::endpoint_id>& sources,
                        vss::endpoint_id target,
                        const vss::link_options& options)
{
    if (const auto error = take_error())
        return *error;
    if (sources.empty())
        return vss::make_error_code(vss::errc::invalid_argument);
    if (!find_port(target))
        return vss::make_error_code(vss::errc::not_found);
    for (const vss::endpoint_id source : sources)
    {
        if (!find_port(source) || source == target)
            return vss::make_error_code(vss::errc::invalid_argument);
    }

    return add_link(link_shape::join, vss::link_mode::one_way, sources,
                    {target}, options);
}

vss::result<vss::link_id>
FakeStudioBackend::redirect(vss::endpoint_id from, vss::endpoint_id to,
                            const vss::link_options& options)
{
    if (const auto error = take_error())
        return *error;
    if (from == to)
        return vss::make_error_code(vss::errc::invalid_argument);
    if (!find_port(from) || !find_port(to))
        return vss::make_error_code(vss::errc::not_found);

    return add_link(link_shape::one_way, vss::link_mode::one_way, {from}, {to},
                    options);
}

vss::result<vss::link_id> FakeStudioBackend::monitor(vss::endpoint_id from,
                                                     vss::endpoint_id to)
{
    if (const auto error = take_error())
        return *error;
    if (from == to)
        return vss::make_error_code(vss::errc::invalid_argument);
    if (!find_port(from) || !find_port(to))
        return vss::make_error_code(vss::errc::not_found);

    vss::link_options options;
    options.emulate_modem_lines = false;
    return add_link(link_shape::monitor, vss::link_mode::one_way, {from}, {to},
                    options);
}

vss::link_id
FakeStudioBackend::add_link(link_shape shape, vss::link_mode mode,
                            const std::vector<vss::endpoint_id>& sources,
                            const std::vector<vss::endpoint_id>& targets,
                            const vss::link_options& options)
{
    link_view view;
    view.id = next_link_++;
    const vss::link_id link_id = view.id;
    view.shape = shape;
    view.mode = mode;
    view.options = options;
    view.sources.reserve(static_cast<qsizetype>(sources.size()));
    for (const vss::endpoint_id id : sources)
        view.sources.push_back(id);
    view.targets.reserve(static_cast<qsizetype>(targets.size()));
    for (const vss::endpoint_id id : targets)
        view.targets.push_back(id);
    links_.push_back(std::move(view));
    refresh();
    return link_id;
}

vss::result<void> FakeStudioBackend::disconnect(vss::link_id id)
{
    if (const auto error = take_error())
        return *error;
    const auto it =
        std::find_if(links_.begin(), links_.end(),
                     [id](const link_view& view) { return view.id == id; });
    if (it == links_.end())
        return vss::make_error_code(vss::errc::not_found);
    wiring_.remove(id);
    links_.erase(it);
    refresh();
    return {};
}

vss::result<void> FakeStudioBackend::set_owner(vss::link_id id,
                                               vss::endpoint_id owner)
{
    if (const auto error = take_error())
        return *error;
    link_view* view = find_link(id);
    if (view == nullptr)
        return vss::make_error_code(vss::errc::not_found);
    if (!view->sources.contains(owner))
        return vss::make_error_code(vss::errc::invalid_argument);
    view->owner = owner;
    refresh();
    return {};
}

vss::result<void> FakeStudioBackend::release_owner(vss::link_id id)
{
    if (const auto error = take_error())
        return *error;
    link_view* view = find_link(id);
    if (view == nullptr)
        return vss::make_error_code(vss::errc::not_found);
    view->owner = 0;
    refresh();
    return {};
}

std::optional<line_wiring> FakeStudioBackend::link_wiring(vss::link_id id) const
{
    const auto it = wiring_.constFind(id);
    if (it == wiring_.constEnd())
        return std::nullopt;
    return it.value();
}

vss::result<void> FakeStudioBackend::set_link_wiring(vss::link_id id,
                                                     const line_wiring& wiring)
{
    if (const auto error = take_error())
        return *error;
    if (find_link(id) == nullptr)
        return vss::make_error_code(vss::errc::not_found);
    if (!caps_.line_matrix)
        return vss::make_error_code(vss::errc::unsupported);
    wiring_.insert(id, wiring);
    refresh();
    return {};
}

vss::result<void> FakeStudioBackend::send(vss::endpoint_id id,
                                          const QByteArray& data)
{
    if (const auto error = take_error())
        return *error;
    port_view* view = find_port(id);
    if (view == nullptr)
        return vss::make_error_code(vss::errc::not_found);
    if (!view->open)
        return vss::make_error_code(vss::errc::not_open);
    sent_.push_back(data);
    view->stats.bytes_tx += static_cast<std::uint64_t>(data.size());
    refresh();
    return {};
}

vss::result<void> FakeStudioBackend::set_line(vss::endpoint_id id,
                                              vss::control_line line,
                                              bool state)
{
    if (const auto error = take_error())
        return *error;
    port_view* view = find_port(id);
    if (view == nullptr)
        return vss::make_error_code(vss::errc::not_found);
    if (!view->supports_control_lines)
        return vss::make_error_code(vss::errc::unsupported);
    view->lines.set(line, state);
    Q_EMIT lines_changed(id);
    refresh();
    return {};
}

void FakeStudioBackend::refresh_lines(vss::endpoint_id id)
{
    if (find_port(id) != nullptr)
        Q_EMIT lines_changed(id);
}

vss::config::document FakeStudioBackend::snapshot() const
{
    vss::config::document document;
    document.name = "fake";

    for (const auto& view : ports_)
    {
        vss::config::port_entry entry;
        entry.name = view.name.toStdString();
        entry.serial = view.serial;
        switch (view.kind)
        {
        case vss::endpoint_kind::virtual_port:
            entry.kind = "virtual";
            entry.virtual_kind =
                view.path.startsWith(QStringLiteral("loopback:")) ? "loopback"
                                                                  : "auto";
            break;
        case vss::endpoint_kind::mock:
            entry.kind = "mock";
            entry.mock_target = view.path.toStdString();
            break;
        case vss::endpoint_kind::physical:
            entry.kind = "physical";
            entry.uri = view.path.toStdString();
            break;
        case vss::endpoint_kind::tcp:
            entry.kind = "tcp";
            entry.uri = view.path.toStdString();
            break;
        case vss::endpoint_kind::udp:
            entry.kind = "udp";
            entry.uri = view.path.toStdString();
            break;
        case vss::endpoint_kind::rfc2217:
            entry.kind = "rfc2217";
            entry.uri = view.path.toStdString();
            break;
        }
        document.ports.push_back(std::move(entry));
    }

    for (const auto& view : links_)
    {
        vss::config::link_entry entry;
        switch (view.shape)
        {
        case link_shape::two_way:
            entry.mode = "duplex";
            break;
        case link_shape::split:
            entry.mode = "split";
            break;
        case link_shape::join:
            entry.mode = "join";
            break;
        case link_shape::one_way:
            entry.mode = "redirect";
            break;
        case link_shape::monitor:
            entry.mode = "monitor";
            break;
        }
        for (const vss::endpoint_id id : view.sources)
        {
            for (const auto& candidate : ports_)
            {
                if (candidate.id == id)
                    entry.sources.push_back(candidate.name.toStdString());
            }
        }
        for (const vss::endpoint_id id : view.targets)
        {
            for (const auto& candidate : ports_)
            {
                if (candidate.id == id)
                    entry.targets.push_back(candidate.name.toStdString());
            }
        }
        entry.emulate_modem_lines = view.options.emulate_modem_lines;
        entry.policy = view.options.policy;
        document.links.push_back(std::move(entry));
    }
    return document;
}

vss::result<void>
FakeStudioBackend::apply(const vss::config::document& document, bool)
{
    if (const auto error = take_error())
        return *error;
    if (const vss::error_code invalid = vss::config::validate(document))
        return invalid;

    std::vector<port_view> next_ports;
    std::vector<link_view> next_links;
    std::vector<std::pair<std::string, vss::endpoint_id>> names;
    vss::endpoint_id next_port = 1;

    for (const auto& entry : document.ports)
    {
        port_view view;
        view.id = next_port++;
        view.name = QString::fromStdString(entry.name);
        view.serial = entry.serial;
        view.state = vss::endpoint_state::open;
        view.open = true;
        view.supports_control_lines = entry.kind != "mock";
        if (entry.kind == "virtual")
        {
            view.kind = vss::endpoint_kind::virtual_port;
            view.studio_only = entry.virtual_kind == "loopback";
            view.path = view.studio_only
                            ? QStringLiteral("loopback:%1").arg(view.name)
                            : QStringLiteral("auto:%1").arg(view.name);
        }
        else if (entry.kind == "mock")
        {
            view.kind = vss::endpoint_kind::mock;
            view.path = QString::fromStdString(entry.mock_target);
            view.studio_only = true;
        }
        else
        {
            view.path = QString::fromStdString(entry.uri);
            const auto kind = vss::config::to_endpoint_kind(entry.kind);
            if (!kind)
                return kind.error();
            view.kind = *kind;
        }
        names.emplace_back(entry.name, view.id);
        next_ports.push_back(std::move(view));
    }

    vss::link_id next_link = 1;
    for (const auto& entry : document.links)
    {
        const auto resolve =
            [&names](const std::string& name) -> std::optional<vss::endpoint_id>
        {
            for (const auto& [candidate, id] : names)
            {
                if (candidate == name)
                    return id;
            }
            return std::nullopt;
        };

        link_view view;
        view.id = next_link++;
        view.options.emulate_modem_lines = entry.emulate_modem_lines;
        view.options.policy = entry.policy;
        if (entry.mode == "duplex")
            view.shape = link_shape::two_way;
        else if (entry.mode == "split")
            view.shape = link_shape::split;
        else if (entry.mode == "join")
            view.shape = link_shape::join;
        else if (entry.mode == "monitor")
            view.shape = link_shape::monitor;
        else
            view.shape = link_shape::one_way;
        view.mode = view.shape == link_shape::join ||
                            view.shape == link_shape::one_way ||
                            view.shape == link_shape::monitor
                        ? vss::link_mode::one_way
                        : vss::link_mode::duplex;

        for (const auto& name : entry.sources)
        {
            const auto id = resolve(name);
            if (!id)
                return vss::make_error_code(vss::errc::config_error);
            view.sources.push_back(*id);
        }
        for (const auto& name : entry.targets)
        {
            const auto id = resolve(name);
            if (!id)
                return vss::make_error_code(vss::errc::config_error);
            view.targets.push_back(*id);
        }
        next_links.push_back(std::move(view));
    }

    ports_ = std::move(next_ports);
    links_ = std::move(next_links);
    wiring_.clear();
    next_port_ = next_port;
    next_link_ = next_link;
    refresh();
    return {};
}

vss::result<share_view> FakeStudioBackend::share_serial(const QString& uri,
                                                        int listeners)
{
    if (listeners < 1 || listeners > 64)
        return vss::make_error_code(vss::errc::invalid_argument);
    if (const auto error = take_error())
        return *error;

    const auto source = attach_serial(QStringLiteral("shared"), uri,
                                      vss::serial_settings{}, true);
    if (!source)
        return source.error();

    std::vector<vss::endpoint_id> created;
    for (int index = 0; index < listeners; ++index)
    {
        const auto listener = create_auto_port(
            QStringLiteral("listener-%1").arg(index + 1), true);
        if (!listener)
        {
            for (const vss::endpoint_id id : created)
                static_cast<void>(remove_port(id));
            static_cast<void>(remove_port(*source));
            return listener.error();
        }
        created.push_back(*listener);
    }

    const auto link = split(*source, created, vss::link_options{});
    if (!link)
    {
        for (const vss::endpoint_id id : created)
            static_cast<void>(remove_port(id));
        static_cast<void>(remove_port(*source));
        return link.error();
    }

    share_view view;
    view.source = *source;
    view.link = *link;
    view.listeners.reserve(static_cast<qsizetype>(created.size()));
    for (const vss::endpoint_id id : created)
        view.listeners.push_back(id);
    return view;
}

vss::result<vss::link_id> FakeStudioBackend::bridge(const QString& from_uri,
                                                    const QString& to_uri)
{
    if (const auto error = take_error())
        return *error;

    const auto from = attach_serial(QStringLiteral("bridge-a"), from_uri,
                                    vss::serial_settings{}, true);
    if (!from)
        return from.error();
    const auto to = attach_serial(QStringLiteral("bridge-b"), to_uri,
                                  vss::serial_settings{}, true);
    if (!to)
    {
        static_cast<void>(remove_port(*from));
        return to.error();
    }
    const auto link = connect_ports(*from, *to, vss::link_options{});
    if (!link)
    {
        static_cast<void>(remove_port(*from));
        static_cast<void>(remove_port(*to));
        return link.error();
    }
    return *link;
}

void FakeStudioBackend::refresh()
{
    Q_EMIT ports_changed();
    Q_EMIT links_changed();
}

void FakeStudioBackend::set_refresh_interval(int milliseconds)
{
    refresh_interval_ms_ = milliseconds;
}

} // namespace vss::gui
