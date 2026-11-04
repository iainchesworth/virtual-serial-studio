//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_CORE_HUB_HPP
#define VSS_CORE_HUB_HPP

#include <vss/core/bytes.hpp>
#include <vss/core/control_lines.hpp>
#include <vss/core/endpoint.hpp>
#include <vss/core/error.hpp>
#include <vss/core/event.hpp>
#include <vss/core/link.hpp>
#include <vss/core/traffic_log.hpp>

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace vss {

struct hub_options
{
    /// Worker threads; 0 selects `std::thread::hardware_concurrency()`.
    unsigned io_threads = 0;
    /// Events kept in the traffic ring buffer.
    std::size_t traffic_capacity = 8192;
};

/// The topology engine.
///
/// A hub owns an `io_context`, the endpoints attached to it, the routing
/// table between them and the event dispatcher used by the GUI.  It contains
/// no serial knowledge: endpoints are given a #transport, so the same hub
/// drives real ports, virtual ports, network bridges and in-memory fixtures.
class hub : public std::enable_shared_from_this<hub>
{
public:
    explicit hub(hub_options options = {});
    ~hub();

    hub(const hub&) = delete;
    hub& operator=(const hub&) = delete;

    boost::asio::io_context& io() noexcept { return io_; }
    boost::asio::any_io_executor executor() noexcept
    {
        return io_.get_executor();
    }

    void start();
    void stop();
    void join();
    bool running() const noexcept { return running_.load(); }

    // -- endpoints ---------------------------------------------------------

    result<std::shared_ptr<endpoint>> add_endpoint(
        const std::string& name, endpoint_kind kind,
        std::shared_ptr<transport> transport, endpoint_options options = {});

    result<void> remove_endpoint(endpoint_id id);
    result<void> rename_endpoint(endpoint_id id, const std::string& name);

    std::shared_ptr<endpoint> find_endpoint(endpoint_id id) const;
    std::shared_ptr<endpoint> find_endpoint(std::string_view name) const;
    std::vector<std::shared_ptr<endpoint>> endpoints() const;

    // -- topology ----------------------------------------------------------

    /// Bidirectional link between two endpoints.
    result<link_id> connect(endpoint_id a, endpoint_id b,
                            link_options options = {});

    /// One endpoint feed many listeners; replies are merged back.
    result<link_id> split(endpoint_id source,
                          const std::vector<endpoint_id>& targets,
                          link_options options = {});

    /// Many publishers merged into one endpoint.
    result<link_id> join(const std::vector<endpoint_id>& sources,
                         endpoint_id target, link_options options = {});

    /// One way traffic from `from` to `to`.
    result<link_id> redirect(endpoint_id from, endpoint_id to,
                             link_options options = {});

    /// Passive one-way copy, for a monitor port.
    result<link_id> monitor(endpoint_id from, endpoint_id to);

    result<void> disconnect(link_id id);

    std::vector<link_info> links() const;
    std::optional<link_info> find_link(link_id id) const;

    /// Assigns the single publisher allowed by #merge_policy::owner.
    result<void> set_owner(link_id id, endpoint_id owner);
    result<void> release_owner(link_id id);

    // -- events ------------------------------------------------------------

    /// Registers a sink.
    ///
    /// Sinks run on the hub's event strand, never inline with the publish.
    /// `unsubscribe` stops future deliveries but cannot recall one that is
    /// already running, so a sink must capture state that outlives the hub
    /// (a `shared_ptr`, or locals declared before the hub is destroyed) --
    /// never a stack local of the scope that created it.
    subscriber_id subscribe(event_filter filter, event_sink sink);

    /// Accepts any callable invocable with an #endpoint_event, so callers do
    /// not have to spell `std::function` at the call site.  The `EventSink`
    /// concept is what turns a mistyped handler into a readable diagnostic.
    template <EventSink Sink>
    subscriber_id subscribe(event_filter filter, Sink&& sink)
    {
        return subscribe(filter, event_sink(std::forward<Sink>(sink)));
    }
    void unsubscribe(subscriber_id id);

    std::vector<traffic_record> traffic(std::size_t limit = 0) const;
    void clear_traffic();

    // -- used by endpoint --------------------------------------------------

    void route_data(endpoint_id from, bytes_view data);
    void route_lines(endpoint_id from, control_lines lines);
    void publish(endpoint_event event);

private:
    struct route_entry
    {
        link_id link = 0;
        endpoint_id to = 0;
    };

    struct link_record
    {
        link_id id = 0;
        link_mode mode = link_mode::duplex;
        link_options options;
        std::vector<endpoint_id> sources;
        std::vector<endpoint_id> targets;
        std::atomic<endpoint_id> owner{0};
        std::atomic<std::uint64_t> bytes{0};
        std::atomic<std::uint64_t> messages{0};
        std::atomic<std::uint64_t> dropped{0};
    };

    struct subscription
    {
        subscriber_id id = 0;
        event_filter filter;
        event_sink sink;
    };

    result<link_id> add_link(link_mode mode,
                             const std::vector<endpoint_id>& sources,
                             const std::vector<endpoint_id>& targets,
                             link_options options);
    bool would_create_cycle(endpoint_id from, endpoint_id to) const;
    void add_route(link_id link, endpoint_id from, endpoint_id to);
    void erase_routes(link_id link);
    link_info describe(const link_record& record) const;
    void dispatch(const endpoint_event& event);
    void endpoint_closed();

    hub_options options_;
    boost::asio::io_context io_;
    std::unique_ptr<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>
        work_;
    std::vector<std::thread> threads_;
    std::atomic<bool> running_{false};
    // Shutdowns in flight, from remove_endpoint() and stop(). The io_context is
    // stopped only once stop() has been called and this reaches zero.
    std::atomic<std::size_t> closing_{0};
    std::atomic<bool> stopping_{false};

    mutable std::shared_mutex mutex_;
    endpoint_id next_endpoint_id_ = 1;
    link_id next_link_id_ = 1;
    std::unordered_map<endpoint_id, std::shared_ptr<endpoint>> endpoints_;
    std::unordered_map<std::string, endpoint_id> names_;
    std::unordered_map<link_id, std::shared_ptr<link_record>> links_;
    std::unordered_map<endpoint_id, std::vector<route_entry>> routes_;

    mutable std::mutex subscriber_mutex_;
    subscriber_id next_subscriber_id_ = 1;
    std::vector<subscription> subscribers_;

    boost::asio::strand<boost::asio::any_io_executor> event_strand_;
    traffic_log traffic_;
};

} // namespace vss

#endif // VSS_CORE_HUB_HPP
