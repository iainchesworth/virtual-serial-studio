//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_TESTS_SUPPORT_HPP
#define VSS_TESTS_SUPPORT_HPP

#include "test_framework.hpp"

#include <vss/core/hub.hpp>
#include <vss/core/transport.hpp>
#include <vss/ports/loopback_transport.hpp>
#include <vss/ports/serial_transport.hpp>
#include <vss/testkit/harness.hpp>

#include <boost/asio/post.hpp>

#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace vss_test_support {

inline std::shared_ptr<vss::hub> start_hub(unsigned threads = 2)
{
    vss::hub_options options;
    options.io_threads = threads;
    options.traffic_capacity = 256;
    auto hub = std::make_shared<vss::hub>(options);
    hub->start();
    return hub;
}

inline std::shared_ptr<vss::endpoint> add_loopback(vss::hub& hub,
                                                   const std::string& name)
{
    auto transport = vss::loopback_transport::create(hub.executor(), name);
    auto added = hub.add_endpoint(name, vss::endpoint_kind::virtual_port,
                                  std::move(transport));
    REQUIRE(added);
    return *added;
}

/// An endpoint backed by the serial library's scriptable mock device.  The
/// typed transport is returned so the test can inject reads and inspect
/// writes.
struct mock_endpoint
{
    std::shared_ptr<vss::endpoint> endpoint;
    std::shared_ptr<vss::serial_transport> transport;
};

inline mock_endpoint add_mock(vss::hub& hub, const std::string& name)
{
    boost::system::error_code ec;
    auto transport = vss::serial_transport::make_mock(
        hub.executor(), vss::serial_settings{}, name, ec);
    REQUIRE(!ec);
    REQUIRE(transport != nullptr);

    auto added = hub.add_endpoint(name, vss::endpoint_kind::mock, transport);
    REQUIRE(added);
    return mock_endpoint{*added, std::move(transport)};
}

inline vss::endpoint_options test_options()
{
    vss::endpoint_options options;
    options.capture_traffic = true;
    options.read_buffer_size = 1024;
    return options;
}

/// A transport that fails every operation with a fixed error.
class failing_transport final : public vss::transport
{
public:
    failing_transport(const executor_type& executor, boost::system::error_code error)
        : executor_(executor)
        , error_(error)
    {
    }

    std::string describe() const override { return "failing transport"; }
    bool is_open() const override { return open_; }

    void async_read_some(boost::asio::mutable_buffer, io_handler handler) override
    {
        boost::asio::post(executor_,
                          [handler, error = error_] { handler(error, 0); });
    }

    void async_write_some(boost::asio::const_buffer, io_handler handler) override
    {
        boost::asio::post(executor_,
                          [handler, error = error_] { handler(error, 0); });
    }

    void close() override { open_ = false; }
    void cancel() override {}

private:
    executor_type executor_;
    boost::system::error_code error_;
    bool open_ = true;
};

/// A transport that accepts writes and never completes them.
class stalled_transport final : public vss::transport,
                                public std::enable_shared_from_this<stalled_transport>
{
public:
    explicit stalled_transport(const executor_type& executor)
        : executor_(executor)
    {
    }

    std::string describe() const override { return "stalled transport"; }
    bool is_open() const override { return open_; }

    void async_read_some(boost::asio::mutable_buffer, io_handler handler) override
    {
        boost::asio::post(executor_, [handler] {
            handler(boost::asio::error::operation_aborted, 0);
        });
    }

    void async_write_some(boost::asio::const_buffer, io_handler) override
    {
        // Deliberately never completes.
    }

    void close() override { open_ = false; }
    void cancel() override {}

private:
    executor_type executor_;
    bool open_ = true;
};

/// RAII subscription: unsubscribes before the captured locals are destroyed.
class subscription_guard
{
public:
    subscription_guard(vss::hub& hub, vss::event_filter filter,
                       vss::event_sink sink)
        : hub_(&hub)
        , id_(hub.subscribe(std::move(filter), std::move(sink)))
    {
    }

    ~subscription_guard()
    {
        if (hub_)
            hub_->unsubscribe(id_);
    }

    subscription_guard(const subscription_guard&) = delete;
    subscription_guard& operator=(const subscription_guard&) = delete;

    subscription_guard(subscription_guard&& other) noexcept
        : hub_(std::exchange(other.hub_, nullptr))
        , id_(other.id_)
    {
    }

    vss::subscriber_id id() const noexcept { return id_; }

private:
    vss::hub* hub_ = nullptr;
    vss::subscriber_id id_ = 0;
};

/// A transport that accepts writes in two halves, to exercise the partial
/// completion path of the endpoint write queue.
class partial_transport final : public vss::transport,
                                public std::enable_shared_from_this<partial_transport>
{
public:
    explicit partial_transport(const executor_type& executor)
        : executor_(executor)
    {
    }

    std::string describe() const override { return "partial transport"; }
    bool is_open() const override { return open_; }

    void async_read_some(boost::asio::mutable_buffer, io_handler handler) override
    {
        boost::asio::post(executor_, [handler] {
            handler(boost::asio::error::operation_aborted, 0);
        });
    }

    void async_write_some(boost::asio::const_buffer buffer,
                          io_handler handler) override
    {
        const std::size_t size = buffer.size();
        const std::size_t half = size > 1 ? size / 2 : size;
        boost::asio::post(executor_, [handler, half] { handler({}, half); });
    }

    void close() override { open_ = false; }
    void cancel() override {}

private:
    executor_type executor_;
    bool open_ = true;
};

/// Thread safe recorder for hub events.
///
/// Copies share one buffer, so a sink can (and must) capture the recorder by
/// value: the hub may dispatch after the test's local goes out of scope, and
/// a sink holding a dangling reference corrupts the heap.
class event_recorder
{
public:
    // const: a by-value capture inside a non-mutable lambda is const, and the
    // buffer is reached through the shared pointer anyway.
    void record(const vss::endpoint_event& event) const
    {
        std::lock_guard lock(state_->mutex);
        state_->events.push_back(event);
    }

    std::vector<vss::endpoint_event> snapshot() const
    {
        std::lock_guard lock(state_->mutex);
        return state_->events;
    }

    std::size_t count(vss::event_kind kind) const
    {
        std::lock_guard lock(state_->mutex);
        std::size_t total = 0;
        for (const auto& event : state_->events)
        {
            if (event.kind == kind)
                ++total;
        }
        return total;
    }

    std::string data_ascii(vss::data_direction direction) const
    {
        std::lock_guard lock(state_->mutex);
        std::string out;
        for (const auto& event : state_->events)
        {
            if (event.kind == vss::event_kind::data
                && event.direction == direction && event.data)
                out += vss::to_string(vss::bytes_view(*event.data));
        }
        return out;
    }

    void clear()
    {
        std::lock_guard lock(state_->mutex);
        state_->events.clear();
    }

private:
    struct shared_state
    {
        mutable std::mutex mutex;
        std::vector<vss::endpoint_event> events;
    };

    std::shared_ptr<shared_state> state_ = std::make_shared<shared_state>();
};

} // namespace vss_test_support

#endif // VSS_TESTS_SUPPORT_HPP
