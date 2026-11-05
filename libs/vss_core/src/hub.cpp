//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <vss/core/hub.hpp>
#include <vss/core/profile.hpp>

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/post.hpp>

#include <algorithm>
#include <unordered_set>

namespace vss {
namespace {

bool contains(const std::vector<endpoint_id>& values, endpoint_id value)
{
    return std::find(values.begin(), values.end(), value) != values.end();
}

unsigned default_thread_count()
{
    const unsigned hardware = std::thread::hardware_concurrency();
    return hardware == 0 ? 2u : std::max(2u, hardware);
}

} // namespace

hub::hub(hub_options options)
    : options_(options)
    , event_strand_(boost::asio::make_strand(io_))
    , traffic_(options.traffic_capacity)
{
}

hub::~hub()
{
    stop();
    join();
}

void hub::start()
{
    if (running_.exchange(true))
        return;

    stopping_.store(false);
    io_.restart();
    work_ = std::make_unique<
        boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>(
        io_.get_executor());

    const unsigned count =
        options_.io_threads == 0 ? default_thread_count() : options_.io_threads;
    threads_.reserve(count);
    for (unsigned index = 0; index < count; ++index)
        threads_.emplace_back([this] { io_.run(); });
}

void hub::stop()
{
    if (!running_.exchange(false))
        return;

    // The work guard stays until join(). Releasing it here would let the worker
    // threads leave an idle io_context before the shutdown handlers below are
    // posted, so those handlers would never run and an endpoint with a parked
    // read would keep itself alive through its transport.

    // Ask every endpoint to close itself on its own strand: transports are
    // single-threaded objects, so closing them from this thread while a read
    // is in flight would race the serial library.  The io_context is stopped
    // once every shutdown has finished, including the ones remove_endpoint()
    // started earlier, so a transport that never completes cannot hang
    // shutdown and a queued shutdown is never thrown away.
    std::vector<std::shared_ptr<endpoint>> endpoints;
    {
        std::shared_lock lock(mutex_);
        endpoints.reserve(endpoints_.size());
        for (const auto& entry : endpoints_)
            endpoints.push_back(entry.second);
    }

    closing_.fetch_add(endpoints.size());
    stopping_.store(true);
    if (closing_.load() == 0)
        io_.stop();

    for (const auto& entry : endpoints)
        entry->shutdown([this] { endpoint_closed(); });
}

void hub::endpoint_closed()
{
    if (closing_.fetch_sub(1) == 1 && stopping_.load())
        io_.stop();
}

void hub::join()
{
    for (auto& thread : threads_)
    {
        if (thread.joinable())
            thread.join();
    }
    threads_.clear();
    work_.reset();
}

result<std::shared_ptr<endpoint>> hub::add_endpoint(
    const std::string& name, endpoint_kind kind,
    std::shared_ptr<transport> transport,
    endpoint_options options)
{
    if (name.empty())
        return make_error_code(errc::invalid_argument);
    if (!transport)
        return make_error_code(errc::invalid_argument);

    std::shared_ptr<endpoint> created;
    {
        std::unique_lock lock(mutex_);
        if (names_.find(name) != names_.end())
            return make_error_code(errc::already_exists);
        if (endpoints_.size() >= 4096)
            return make_error_code(errc::port_exhausted);

        const endpoint_id id = next_endpoint_id_++;
        created = std::shared_ptr<endpoint>(
            new endpoint(*this, id, name, kind, std::move(transport), options));
        endpoints_.emplace(id, created);
        names_.emplace(name, id);
    }

    created->start();
    return created;
}

result<void> hub::remove_endpoint(endpoint_id id)
{
    std::shared_ptr<endpoint> victim;
    {
        std::unique_lock lock(mutex_);
        auto it = endpoints_.find(id);
        if (it == endpoints_.end())
            return make_error_code(errc::not_found);

        victim = it->second;

        for (auto link = links_.begin(); link != links_.end();)
        {
            const auto& record = *link->second;
            if (contains(record.sources, id) || contains(record.targets, id))
            {
                const link_id removed = link->first;
                link = links_.erase(link);
                erase_routes(removed);
            }
            else
            {
                ++link;
            }
        }

        routes_.erase(id);
        for (auto& entry : routes_)
        {
            auto& routes = entry.second;
            routes.erase(std::remove_if(routes.begin(), routes.end(),
                                        [id](const route_entry& route) {
                                            return route.to == id;
                                        }),
                         routes.end());
        }

        names_.erase(victim->name());
        endpoints_.erase(it);
    }

    closing_.fetch_add(1);
    victim->shutdown([this] { endpoint_closed(); });
    return {};
}

result<void> hub::rename_endpoint(endpoint_id id, const std::string& name)
{
    if (name.empty())
        return make_error_code(errc::invalid_argument);

    std::unique_lock lock(mutex_);
    auto it = endpoints_.find(id);
    if (it == endpoints_.end())
        return make_error_code(errc::not_found);
    if (names_.find(name) != names_.end())
        return make_error_code(errc::already_exists);

    names_.erase(it->second->name());
    names_.emplace(name, id);
    it->second->set_name(name);
    return {};
}

std::shared_ptr<endpoint> hub::find_endpoint(endpoint_id id) const
{
    std::shared_lock lock(mutex_);
    auto it = endpoints_.find(id);
    return it == endpoints_.end() ? nullptr : it->second;
}

std::shared_ptr<endpoint> hub::find_endpoint(std::string_view name) const
{
    std::shared_lock lock(mutex_);
    auto it = names_.find(std::string(name));
    if (it == names_.end())
        return nullptr;
    auto endpoint_it = endpoints_.find(it->second);
    return endpoint_it == endpoints_.end() ? nullptr : endpoint_it->second;
}

std::vector<std::shared_ptr<endpoint>> hub::endpoints() const
{
    std::shared_lock lock(mutex_);
    std::vector<std::shared_ptr<endpoint>> out;
    out.reserve(endpoints_.size());
    for (const auto& entry : endpoints_)
        out.push_back(entry.second);
    std::sort(out.begin(), out.end(),
              [](const auto& lhs, const auto& rhs) { return lhs->id() < rhs->id(); });
    return out;
}

result<link_id> hub::connect(endpoint_id a, endpoint_id b, link_options options)
{
    return add_link(link_mode::duplex, {a}, {b}, options);
}

result<link_id> hub::split(endpoint_id source,
                           const std::vector<endpoint_id>& targets,
                           link_options options)
{
    return add_link(link_mode::duplex, {source}, targets, options);
}

result<link_id> hub::join(const std::vector<endpoint_id>& sources,
                          endpoint_id target, link_options options)
{
    return add_link(link_mode::one_way, sources, {target}, options);
}

result<link_id> hub::redirect(endpoint_id from, endpoint_id to,
                              link_options options)
{
    return add_link(link_mode::one_way, {from}, {to}, options);
}

result<link_id> hub::monitor(endpoint_id from, endpoint_id to)
{
    link_options options;
    options.emulate_modem_lines = false;
    return add_link(link_mode::one_way, {from}, {to}, options);
}

result<link_id> hub::add_link(link_mode mode,
                              const std::vector<endpoint_id>& sources,
                              const std::vector<endpoint_id>& targets,
                              link_options options)
{
    if (sources.empty() || targets.empty())
        return make_error_code(errc::invalid_argument);

    std::unique_lock lock(mutex_);

    for (endpoint_id id : sources)
    {
        if (endpoints_.find(id) == endpoints_.end())
            return make_error_code(errc::not_found);
        if (contains(targets, id))
            return make_error_code(errc::invalid_argument);
    }
    for (endpoint_id id : targets)
    {
        if (endpoints_.find(id) == endpoints_.end())
            return make_error_code(errc::not_found);
    }

    const bool duplex = mode == link_mode::duplex;
    for (endpoint_id source : sources)
    {
        for (endpoint_id target : targets)
        {
            if (would_create_cycle(source, target)
                || (duplex && would_create_cycle(target, source)))
                return make_error_code(errc::cycle_detected);
        }
    }

    auto record = std::make_shared<link_record>();
    record->id = next_link_id_++;
    record->mode = mode;
    record->options = options;
    record->sources = sources;
    record->targets = targets;

    links_.emplace(record->id, record);

    for (endpoint_id source : sources)
    {
        for (endpoint_id target : targets)
        {
            add_route(record->id, source, target);
            if (duplex)
                add_route(record->id, target, source);
        }
    }

    return record->id;
}

bool hub::would_create_cycle(endpoint_id from, endpoint_id to) const
{
    if (from == to)
        return true;

    std::unordered_set<endpoint_id> visited;
    std::vector<endpoint_id> pending{to};

    while (!pending.empty())
    {
        const endpoint_id current = pending.back();
        pending.pop_back();
        if (current == from)
            return true;
        if (!visited.insert(current).second)
            continue;
        auto it = routes_.find(current);
        if (it == routes_.end())
            continue;
        for (const auto& route : it->second)
            pending.push_back(route.to);
    }
    return false;
}

void hub::add_route(link_id link, endpoint_id from, endpoint_id to)
{
    routes_[from].push_back(route_entry{link, to});
}

void hub::erase_routes(link_id link)
{
    for (auto& entry : routes_)
    {
        auto& routes = entry.second;
        routes.erase(std::remove_if(routes.begin(), routes.end(),
                                    [link](const route_entry& route) {
                                        return route.link == link;
                                    }),
                     routes.end());
    }
}

result<void> hub::disconnect(link_id id)
{
    std::unique_lock lock(mutex_);
    if (links_.find(id) == links_.end())
        return make_error_code(errc::not_found);
    links_.erase(id);
    erase_routes(id);
    return {};
}

link_info hub::describe(const link_record& record) const
{
    link_info info;
    info.id = record.id;
    info.mode = record.mode;
    info.options = record.options;
    info.sources = record.sources;
    info.targets = record.targets;
    info.owner = record.owner.load();
    info.stats.bytes = record.bytes.load();
    info.stats.messages = record.messages.load();
    info.stats.dropped = record.dropped.load();
    return info;
}

std::vector<link_info> hub::links() const
{
    std::shared_lock lock(mutex_);
    std::vector<link_info> out;
    out.reserve(links_.size());
    for (const auto& entry : links_)
        out.push_back(describe(*entry.second));
    std::sort(out.begin(), out.end(),
              [](const link_info& lhs, const link_info& rhs) { return lhs.id < rhs.id; });
    return out;
}

std::optional<link_info> hub::find_link(link_id id) const
{
    std::shared_lock lock(mutex_);
    auto it = links_.find(id);
    if (it == links_.end())
        return std::nullopt;
    return describe(*it->second);
}

result<void> hub::set_owner(link_id id, endpoint_id owner)
{
    std::unique_lock lock(mutex_);
    auto it = links_.find(id);
    if (it == links_.end())
        return make_error_code(errc::not_found);
    auto& record = *it->second;
    if (!contains(record.sources, owner))
        return make_error_code(errc::invalid_argument);
    record.owner.store(owner);
    return {};
}

result<void> hub::release_owner(link_id id)
{
    std::unique_lock lock(mutex_);
    auto it = links_.find(id);
    if (it == links_.end())
        return make_error_code(errc::not_found);
    it->second->owner.store(0);
    return {};
}

result<void> hub::set_link_wiring(link_id id, const modem_wiring& wiring)
{
    std::vector<std::pair<endpoint_id, control_lines>> senders;
    {
        std::unique_lock lock(mutex_);
        auto it = links_.find(id);
        if (it == links_.end())
            return make_error_code(errc::not_found);
        it->second->options.wiring = wiring;

        for (const auto& [from, routes] : routes_)
        {
            const bool uses = std::any_of(
                routes.begin(), routes.end(),
                [id](const route_entry& route) { return route.link == id; });
            if (!uses)
                continue;
            const auto sender = endpoints_.find(from);
            if (sender != endpoints_.end())
                senders.emplace_back(from, sender->second->lines());
        }
    }

    // The peers must see what the new wires carry now, not at the next change.
    for (const auto& [from, lines] : senders)
        route_lines(from, lines);
    return {};
}

void hub::route_data(endpoint_id from, bytes_view data)
{
    VSS_PROFILE_ZONE("hub.route");

    if (data.empty())
        return;

    std::vector<std::shared_ptr<endpoint>> targets;
    {
        std::shared_lock lock(mutex_);
        auto it = routes_.find(from);
        if (it == routes_.end())
            return;

        for (const auto& route : it->second)
        {
            auto link_it = links_.find(route.link);
            if (link_it == links_.end())
                continue;
            auto& record = *link_it->second;

            if (record.options.policy == merge_policy::owner)
            {
                endpoint_id expected = 0;
                if (record.owner.load() == 0)
                    record.owner.compare_exchange_strong(expected, from);
                if (record.owner.load() != from)
                {
                    record.dropped.fetch_add(data.size());
                    continue;
                }
            }

            record.bytes.fetch_add(data.size());
            record.messages.fetch_add(1);

            auto target = endpoints_.find(route.to);
            if (target != endpoints_.end())
                targets.push_back(target->second);
        }
    }

    for (const auto& target : targets)
        target->write(data);
}

void hub::route_lines(endpoint_id from, control_lines lines)
{
    struct update
    {
        std::shared_ptr<endpoint> target;
        control_lines inputs;
    };

    std::vector<update> updates;
    {
        std::shared_lock lock(mutex_);
        auto it = routes_.find(from);
        if (it == routes_.end())
            return;

        for (const auto& route : it->second)
        {
            auto link_it = links_.find(route.link);
            if (link_it == links_.end())
                continue;
            if (!link_it->second->options.emulate_modem_lines)
                continue;
            auto target = endpoints_.find(route.to);
            if (target != endpoints_.end())
                updates.push_back(
                    {target->second,
                     link_it->second->options.wiring.apply(lines)});
        }
    }

    for (const auto& update : updates)
        update.target->set_peer_inputs(update.inputs);
}

void hub::publish(endpoint_event event)
{
    if (event.kind == event_kind::data && event.data)
    {
        traffic_.push(traffic_record{event.endpoint, event.name,
                                     event.direction, event.data, event.when});
    }
    boost::asio::post(event_strand_,
                      [this, event = std::move(event)]() mutable {
                          dispatch(event);
                      });
}

void hub::dispatch(const endpoint_event& event)
{
    VSS_PROFILE_ZONE("hub.dispatch");

    // Once stop() has been called the subscribers may already be tearing down
    // their own state; delivering a late completion would run a sink over
    // dangling captures.  Shutdown traffic is dropped deliberately.
    if (!running_.load())
        return;

    std::vector<subscription> snapshot;
    {
        std::lock_guard lock(subscriber_mutex_);
        snapshot = subscribers_;
    }

    for (const auto& entry : snapshot)
    {
        if (!entry.filter.matches(event))
            continue;
        try
        {
            entry.sink(event);
        }
        catch (...) // NOLINT(bugprone-empty-catch)
        {
            // A subscriber must never take the hub down.
        }
    }
}

subscriber_id hub::subscribe(event_filter filter, event_sink sink)
{
    std::lock_guard lock(subscriber_mutex_);
    const subscriber_id id = next_subscriber_id_++;
    subscribers_.push_back(subscription{id, filter, std::move(sink)});
    return id;
}

void hub::unsubscribe(subscriber_id id)
{
    std::lock_guard lock(subscriber_mutex_);
    subscribers_.erase(std::remove_if(subscribers_.begin(), subscribers_.end(),
                                      [id](const subscription& entry) {
                                          return entry.id == id;
                                      }),
                       subscribers_.end());
}

std::vector<traffic_record> hub::traffic(std::size_t limit) const
{
    return traffic_.snapshot(limit);
}

void hub::clear_traffic()
{
    traffic_.clear();
}

} // namespace vss
