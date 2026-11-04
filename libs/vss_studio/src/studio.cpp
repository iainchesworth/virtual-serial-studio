//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <vss/studio/studio.hpp>

#include <vss/ports/serial_transport.hpp>

#include <algorithm>
#include <filesystem>
#include <utility>

namespace vss {
namespace {

std::string label_for_uri(const std::string& uri)
{
    if (const auto kind = classify_uri(uri))
    {
        std::string base = to_string(*kind);
        const auto scheme = uri.find("://");
        if (scheme != std::string::npos)
        {
            std::string target = uri.substr(scheme + 3);
            const auto query = target.find('?');
            if (query != std::string::npos)
                target.erase(query);
            for (char& character : target)
            {
                if (character == ':' || character == '/' || character == '\\'
                    || character == '.')
                    character = '-';
            }
            if (!target.empty())
                base += "-" + target;
        }
        return base;
    }
    return "port";
}

/// A port name as a link name: the characters a link may hold, at most 64.
std::string link_name(const std::string& name)
{
    std::string result;
    for (const char c : name)
    {
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        const bool digit = c >= '0' && c <= '9';
        result +=
            (letter || digit || c == '.' || c == '_' || c == '-') ? c : '-';
    }
    if (result.size() > 64)
        result.resize(64);
    if (result.empty() || result == "." || result == "..")
        result = "port";
    return result;
}

endpoint_kind kind_for_record(const std::string& kind)
{
    const auto mapped = config::to_endpoint_kind(kind);
    return mapped ? *mapped : endpoint_kind::physical;
}

config::port_entry record_to_entry(const std::string& name,
                                   const std::string& kind,
                                   const std::string& uri,
                                   const std::string& virtual_kind,
                                   const serial_settings& serial, bool capture)
{
    config::port_entry entry;
    entry.name = name;
    entry.kind = kind;
    entry.uri = uri;
    entry.virtual_kind = virtual_kind;
    entry.serial = serial;
    entry.capture = capture;
    return entry;
}

} // namespace

studio::studio(studio_options options)
    : options_(std::move(options))
    , hub_(std::make_shared<vss::hub>(options_.hub))
{
    if (options_.start_hub)
        hub_->start();
}

studio::~studio()
{
    if (hub_)
    {
        hub_->stop();
        hub_->join();
    }

    // The ports are closed, so no application can use the names any more.
    for (const auto& [id, record] : records_)
    {
        if (record.link)
            remove_stable_link(*record.link);
    }
}

void studio::start()
{
    hub_->start();
}

void studio::stop()
{
    hub_->stop();
    hub_->join();
}

endpoint_options studio::default_options() const
{
    endpoint_options options;
    options.capture_traffic = options_.capture;
    return options;
}

std::string studio::unique_name(std::string_view base) const
{
    std::string candidate(base);
    int suffix = 2;
    while (hub_->find_endpoint(candidate))
    {
        candidate = std::string(base) + "-" + std::to_string(suffix);
        ++suffix;
    }
    return candidate;
}

bool studio::path_in_use(const std::string& path) const
{
    if (path.empty())
        return false;
    std::lock_guard lock(record_mutex_);
    return claimed_paths_.find(path) != claimed_paths_.end();
}

void studio::claim_paths(const std::string& target, const std::string& advertised)
{
    std::lock_guard lock(record_mutex_);
    if (!target.empty())
        claimed_paths_.insert(target);
    if (!advertised.empty())
        claimed_paths_.insert(advertised);
}

result<void> studio::link_pty(port_record& record)
{
    if (!options_.stable_links ||
        record.virtual_kind != to_string(virtual_port_kind::pty))
        return {};

    std::filesystem::path directory(options_.stable_dir);
    if (options_.stable_dir.empty())
    {
        const auto resolved = resolve_link_directory(process_environment());
        if (!resolved)
            return resolved.error();
        directory = *resolved;
    }

    const auto made =
        make_stable_link(directory, link_name(record.name), record.target);
    if (!made)
        return made.error();
    record.link = *made;
    record.advertised = made->path;
    return {};
}

result<std::shared_ptr<endpoint>> studio::register_port(
    port_record record, std::shared_ptr<transport> transport,
    endpoint_options options)
{
    if (!transport)
        return make_error_code(errc::invalid_argument);

    const auto linked = link_pty(record);
    if (!linked)
        return linked.error();

    auto created = hub_->add_endpoint(record.name, kind_for_record(record.kind),
                                      std::move(transport), options);
    if (!created)
    {
        if (record.link)
            remove_stable_link(*record.link);
        return created.error();
    }

    record.id = (*created)->id();
    if (record.advertised.empty())
        record.advertised = (*created)->public_path();
    claim_paths(record.target, record.advertised);
    {
        std::lock_guard lock(record_mutex_);
        records_.emplace(record.id, record);
    }
    return *created;
}

result<std::shared_ptr<endpoint>> studio::create_virtual_port(
    std::string name, virtual_port_kind kind, endpoint_options options)
{
    if (kind == virtual_port_kind::pty && !supports(virtual_port_kind::pty))
        return create_auto_virtual_port(std::move(name), options);

    const std::string requested = name;
    const std::string final_name = unique_name(requested);

    auto created = vss::create_virtual_port(hub_->executor(), kind, final_name);
    if (!created)
        return created.error();

    port_record record;
    record.name = final_name;
    record.kind = "virtual";
    record.virtual_kind = to_string(kind);
    record.target = created->path;
    record.advertised = created->path;
    record.capture = options.capture_traffic;

    return register_port(std::move(record), std::move(created->port), options);
}

result<std::shared_ptr<endpoint>> studio::create_auto_virtual_port(
    std::string name, endpoint_options options)
{
    const std::string final_name = unique_name(name);

    // A COM port from our own driver is the goal state: no third-party
    // emulator, and the port behaves like hardware.  Until the driver ships
    // driver_available() is false and this block is skipped.
    if (supports(virtual_port_kind::windows_driver))
    {
        auto created = vss::create_virtual_port(
            hub_->executor(), virtual_port_kind::windows_driver, final_name);
        if (created)
        {
            port_record record;
            record.name = final_name;
            record.kind = "virtual";
            record.virtual_kind = "windows-driver";
            record.target = created->path;
            record.advertised = created->path;
            record.capture = options.capture_traffic;

            return register_port(std::move(record), std::move(created->port),
                                 options);
        }
    }

#if defined(_WIN32)
    const auto installation = detect_com0com();
    for (const auto& pair : installation.pairs)
    {
        // A null-modem pair can be claimed by at most one endpoint: wiring
        // both ends into the same hub is a physical loop.
        if (path_in_use(pair.a) || path_in_use(pair.b))
            continue;

        const std::pair<std::string, std::string> ends[] = {
            {pair.a, pair.b}, {pair.b, pair.a}};
        for (const auto& [attach, advertise] : ends)
        {
            auto opened = attach_serial(
                final_name, "serial://" + attach + "?baud=115200", options);
            if (!opened)
                continue;

            claim_paths(attach, advertise);
            std::lock_guard lock(record_mutex_);
            auto it = records_.find((*opened)->id());
            if (it != records_.end())
            {
                it->second.kind = "virtual";
                it->second.virtual_kind = "auto";
                it->second.advertised = advertise;
            }
            return opened;
        }
    }
#endif

    // Beyond Windows the best virtual port is a pseudo terminal, which is a
    // real tty an application can open; loopback is only the last resort.
    auto created = create_best_virtual_port(hub_->executor(), final_name);
    if (!created)
        return created.error();

    port_record record;
    record.name = final_name;
    record.kind = "virtual";
    record.virtual_kind = to_string(created->kind);
    record.target = created->path;
    record.advertised = created->path;
    record.capture = options.capture_traffic;

    return register_port(std::move(record), std::move(created->port), options);
}

result<std::shared_ptr<endpoint>> studio::attach_serial(
    std::string name, std::string uri, endpoint_options options)
{
    const std::string final_name = unique_name(name);

    boost::system::error_code ec;
    auto transport = std::make_shared<serial_transport>(hub_->executor(), uri, ec);
    if (ec)
        return ec;

    port_record record;
    record.name = final_name;
    record.uri = uri;
    record.capture = options.capture_traffic;
    record.serial = options.serial;
    if (const auto kind = classify_uri(uri))
        record.kind = to_string(*kind);
    else
        record.kind = "physical";
    record.target = transport->public_path();
    record.advertised = transport->public_path();

    return register_port(std::move(record), std::move(transport), options);
}

result<std::shared_ptr<endpoint>> studio::attach_serial(
    std::string name, std::string uri, const serial_settings& settings,
    endpoint_options options)
{
    if (const error_code invalid = validate(settings))
        return invalid;

    options.serial = settings;
    const std::string final_name = unique_name(name);

    boost::system::error_code ec;
    auto transport =
        std::make_shared<serial_transport>(hub_->executor(), uri, settings, ec);
    if (ec)
        return ec;

    port_record record;
    record.name = final_name;
    record.uri = uri;
    record.capture = options.capture_traffic;
    record.serial = settings;
    if (const auto kind = classify_uri(uri))
        record.kind = to_string(*kind);
    else
        record.kind = "physical";
    record.target = transport->public_path();
    record.advertised = transport->public_path();

    return register_port(std::move(record), std::move(transport), options);
}

result<std::shared_ptr<endpoint>> studio::create_mock_port(
    std::string name, const serial_settings& settings, std::string target)
{
    const std::string final_name = unique_name(name);

    boost::system::error_code ec;
    auto transport = serial_transport::make_mock(hub_->executor(), settings,
                                                 target, ec);
    if (ec)
        return ec;
    if (!transport)
        return make_error_code(errc::internal);

    endpoint_options options = default_options();
    options.serial = settings;

    port_record record;
    record.name = final_name;
    record.kind = "mock";
    record.advertised = transport->public_path();
    record.serial = settings;
    record.capture = options.capture_traffic;

    return register_port(std::move(record), std::move(transport), options);
}

result<void> studio::remove_port(endpoint_id id)
{
    std::string target;
    std::string advertised;
    {
        std::lock_guard lock(record_mutex_);
        auto it = records_.find(id);
        if (it != records_.end())
        {
            target = it->second.target;
            advertised = it->second.advertised;
        }
    }

    const auto removed = hub_->remove_endpoint(id);
    if (!removed)
        return removed.error();

    std::lock_guard lock(record_mutex_);
    if (const auto it = records_.find(id);
        it != records_.end() && it->second.link)
        remove_stable_link(*it->second.link);
    records_.erase(id);
    if (!target.empty())
        claimed_paths_.erase(target);
    if (!advertised.empty() && advertised != target)
        claimed_paths_.erase(advertised);
    return {};
}

result<void> studio::remove_port(std::string_view name)
{
    const auto endpoint = hub_->find_endpoint(name);
    if (!endpoint)
        return make_error_code(errc::not_found);
    return remove_port(endpoint->id());
}

std::string studio::advertised_path(endpoint_id id) const
{
    std::lock_guard lock(record_mutex_);
    auto it = records_.find(id);
    if (it == records_.end())
        return {};
    return it->second.advertised;
}

std::shared_ptr<endpoint> studio::find(std::string_view name) const
{
    return hub_->find_endpoint(name);
}

std::vector<endpoint_info> studio::endpoint_infos() const
{
    std::vector<endpoint_info> out;
    for (const auto& endpoint : hub_->endpoints())
    {
        endpoint_info info = endpoint->info();
        info.path = advertised_path(info.id);
        if (info.path.empty())
            info.path = endpoint->public_path();
        out.push_back(std::move(info));
    }
    return out;
}

std::vector<link_info> studio::link_infos() const
{
    return hub_->links();
}

std::vector<endpoint_id> studio::resolve_all(
    const std::vector<std::string>& names, error_code& ec) const
{
    std::vector<endpoint_id> ids;
    ids.reserve(names.size());
    for (const auto& name : names)
    {
        const auto endpoint = hub_->find_endpoint(name);
        if (!endpoint)
        {
            ec = make_error_code(errc::not_found);
            return {};
        }
        ids.push_back(endpoint->id());
    }
    return ids;
}

result<link_id> studio::connect(std::string_view a, std::string_view b)
{
    const auto first = hub_->find_endpoint(a);
    const auto second = hub_->find_endpoint(b);
    if (!first || !second)
        return make_error_code(errc::not_found);
    return hub_->connect(first->id(), second->id());
}

result<link_id> studio::split(std::string_view source,
                              const std::vector<std::string>& targets)
{
    const auto origin = hub_->find_endpoint(source);
    if (!origin)
        return make_error_code(errc::not_found);

    error_code ec;
    const auto ids = resolve_all(targets, ec);
    if (ec)
        return ec;
    return hub_->split(origin->id(), ids);
}

result<link_id> studio::join(const std::vector<std::string>& sources,
                             std::string_view target)
{
    const auto destination = hub_->find_endpoint(target);
    if (!destination)
        return make_error_code(errc::not_found);

    error_code ec;
    const auto ids = resolve_all(sources, ec);
    if (ec)
        return ec;
    return hub_->join(ids, destination->id());
}

result<link_id> studio::redirect(std::string_view from, std::string_view to)
{
    const auto first = hub_->find_endpoint(from);
    const auto second = hub_->find_endpoint(to);
    if (!first || !second)
        return make_error_code(errc::not_found);
    return hub_->redirect(first->id(), second->id());
}

result<link_id> studio::monitor(std::string_view from, std::string_view to)
{
    const auto first = hub_->find_endpoint(from);
    const auto second = hub_->find_endpoint(to);
    if (!first || !second)
        return make_error_code(errc::not_found);
    return hub_->monitor(first->id(), second->id());
}

result<void> studio::disconnect(link_id id)
{
    return hub_->disconnect(id);
}

result<studio::share_result> studio::share_serial(const std::string& uri,
                                                  int listeners)
{
    if (listeners < 1 || listeners > 64)
        return make_error_code(errc::invalid_argument);

    auto source = attach_serial(label_for_uri(uri), uri);
    if (!source)
        return source.error();

    std::vector<endpoint_id> created;
    for (int index = 0; index < listeners; ++index)
    {
        auto listener = create_auto_virtual_port(
            "listener-" + std::to_string(index + 1), default_options());
        if (!listener)
        {
            for (const endpoint_id id : created)
                static_cast<void>(remove_port(id));
            static_cast<void>(remove_port((*source)->id()));
            return listener.error();
        }
        created.push_back((*listener)->id());
    }

    const auto link = hub_->split((*source)->id(), created);
    if (!link)
    {
        for (const endpoint_id id : created)
            static_cast<void>(remove_port(id));
        static_cast<void>(remove_port((*source)->id()));
        return link.error();
    }

    share_result out;
    out.source = (*source)->id();
    out.listeners = std::move(created);
    out.link = *link;
    return out;
}

result<link_id> studio::join_ports(const std::vector<std::string>& source_uris,
                                   const std::string& target_uri)
{
    if (source_uris.empty())
        return make_error_code(errc::invalid_argument);

    std::vector<std::string> sources;
    for (std::size_t index = 0; index < source_uris.size(); ++index)
    {
        auto attached = attach_serial(
            "publisher-" + std::to_string(index + 1), source_uris[index]);
        if (!attached)
            return attached.error();
        sources.push_back((*attached)->name());
    }

    auto target = attach_serial("merged", target_uri);
    if (!target)
        return target.error();

    return join(sources, (*target)->name());
}

result<link_id> studio::bridge(const std::string& from_uri,
                               const std::string& to_uri)
{
    auto from = attach_serial("bridge-a", from_uri);
    if (!from)
        return from.error();
    auto to = attach_serial("bridge-b", to_uri);
    if (!to)
    {
        static_cast<void>(remove_port((*from)->id()));
        return to.error();
    }

    const auto link = hub_->connect((*from)->id(), (*to)->id());
    if (!link)
    {
        static_cast<void>(remove_port((*from)->id()));
        static_cast<void>(remove_port((*to)->id()));
        return link.error();
    }
    return *link;
}

result<void> studio::apply(const config::document& value, bool rollback_on_error)
{
    if (const error_code invalid = config::validate(value))
        return invalid;

    std::vector<endpoint_id> created;
    std::unordered_map<std::string, endpoint_id> ids;

    const auto rollback = [&]() {
        if (!rollback_on_error)
            return;
        for (const endpoint_id id : created)
            static_cast<void>(remove_port(id));
    };

    for (const auto& port : value.ports)
    {
        endpoint_options options = default_options();
        options.serial = port.serial;
        options.capture_traffic = port.capture;

        result<std::shared_ptr<endpoint>> added =
            make_error_code(errc::config_error);

        if (port.kind == "virtual")
        {
            if (port.virtual_kind == "auto")
                added = create_auto_virtual_port(port.name, options);
            else if (port.virtual_kind == "loopback")
                added = create_virtual_port(port.name,
                                            virtual_port_kind::loopback, options);
            else if (port.virtual_kind == "pty")
                added = create_virtual_port(port.name, virtual_port_kind::pty,
                                            options);
            else if (port.virtual_kind == "named-pipe")
                added = create_virtual_port(port.name,
                                            virtual_port_kind::named_pipe,
                                            options);
            else if (port.virtual_kind == "windows-driver")
                added = create_virtual_port(port.name,
                                            virtual_port_kind::windows_driver,
                                            options);
        }
        else if (port.kind == "mock")
        {
            added = create_mock_port(port.name, port.serial, port.mock_target);
        }
        else
        {
            added = attach_serial(port.name, port.uri, port.serial, options);
        }

        if (!added)
        {
            rollback();
            return added.error();
        }

        created.push_back((*added)->id());
        ids.emplace(port.name, (*added)->id());
    }

    for (const auto& link : value.links)
    {
        std::vector<endpoint_id> sources;
        std::vector<endpoint_id> targets;
        for (const auto& name : link.sources)
            sources.push_back(ids.at(name));
        for (const auto& name : link.targets)
            targets.push_back(ids.at(name));

        link_options options;
        options.emulate_modem_lines = link.emulate_modem_lines;
        options.policy = link.policy;

        result<link_id> added = make_error_code(errc::config_error);
        if (link.mode == "duplex")
            added = hub_->connect(sources[0], targets[0], options);
        else if (link.mode == "split")
            added = hub_->split(sources[0], targets, options);
        else if (link.mode == "join")
            added = hub_->join(sources, targets[0], options);
        else if (link.mode == "redirect")
            added = hub_->redirect(sources[0], targets[0], options);
        else if (link.mode == "monitor")
            added = hub_->monitor(sources[0], targets[0]);

        if (!added)
        {
            rollback();
            return added.error();
        }
    }

    return {};
}

config::document studio::snapshot() const
{
    config::document document;
    document.name = "studio";

    std::unordered_map<endpoint_id, std::string> names;
    {
        std::lock_guard lock(record_mutex_);
        for (const auto& entry : records_)
        {
            const auto& record = entry.second;
            document.ports.push_back(record_to_entry(
                record.name, record.kind, record.uri, record.virtual_kind,
                record.serial, record.capture));
            names.emplace(record.id, record.name);
        }
    }

    for (const auto& link : hub_->links())
    {
        config::link_entry entry;
        switch (link.mode)
        {
        case link_mode::duplex:
            entry.mode =
                link.sources.size() == 1 && link.targets.size() == 1 ? "duplex"
                                                                     : "split";
            break;
        case link_mode::one_way:
            if (!link.options.emulate_modem_lines)
                entry.mode = "monitor";
            else if (link.sources.size() > 1)
                entry.mode = "join";
            else
                entry.mode = "redirect";
            break;
        }

        for (const endpoint_id id : link.sources)
        {
            auto it = names.find(id);
            if (it != names.end())
                entry.sources.push_back(it->second);
        }
        for (const endpoint_id id : link.targets)
        {
            auto it = names.find(id);
            if (it != names.end())
                entry.targets.push_back(it->second);
        }

        if (entry.sources.empty() || entry.targets.empty())
            continue;

        entry.emulate_modem_lines = link.options.emulate_modem_lines;
        entry.policy = link.options.policy;
        document.links.push_back(std::move(entry));
    }

    return document;
}

com0com_installation studio::detect_com0com() const
{
    return vss::detect_com0com();
}

std::vector<serial_device_info> studio::serial_devices() const
{
    return enumerate_serial_devices();
}

} // namespace vss
