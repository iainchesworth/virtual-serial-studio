//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <vss/core/endpoint.hpp>
#include <vss/core/profile.hpp>
#include <vss/core/hub.hpp>

#include <boost/asio/error.hpp>
#include <boost/asio/post.hpp>

#include <algorithm>
#include <future>

namespace vss {

endpoint::endpoint(hub& owner, endpoint_id id, std::string name,
                   endpoint_kind kind, std::shared_ptr<transport> transport,
                   endpoint_options options)
    : owner_(owner)
    , id_(id)
    , name_(std::move(name))
    , kind_(kind)
    , transport_(std::move(transport))
    , options_(options)
    , strand_(boost::asio::make_strand(owner.executor()))
    , read_buffer_(options_.read_buffer_size == 0 ? std::size_t{1}
                                                  : options_.read_buffer_size)
{
    stats_.started = clock_type::now();
    if (options_.max_write_queue_bytes == 0)
        options_.max_write_queue_bytes = 1;
    if (transport_)
        lines_ = transport_->lines();
}

endpoint::~endpoint()
{
    if (transport_)
        transport_->close();
}

endpoint_state endpoint::state() const noexcept
{
    return state_.load();
}

bool endpoint::is_open() const noexcept
{
    return state_.load() == endpoint_state::open && transport_
        && transport_->is_open();
}

std::string endpoint::public_path() const
{
    return transport_ ? transport_->public_path() : std::string();
}

std::string endpoint::description() const
{
    return transport_ ? transport_->describe() : std::string();
}

boost::asio::any_io_executor endpoint::get_executor() const
{
    return strand_;
}

void endpoint::start()
{
    if (running_.exchange(true))
        return;

    transition(endpoint_state::opening);

    if (!transport_ || !transport_->is_open())
    {
        running_ = false;
        transition(endpoint_state::failed);
        emit(make_error_code(errc::not_open), "transport is not open");
        return;
    }

    {
        std::lock_guard lock(lines_mutex_);
        lines_ = transport_->lines();
    }
    transition(endpoint_state::open);

    // A transport may log what it notices about its device. The sink holds the
    // endpoint weakly: the transport can outlive a call into it.
    transport_->set_notice_sink(
        [weak = weak_from_this()](std::string text)
        {
            if (const auto self = weak.lock())
                self->emit({}, std::move(text), event_kind::log_message);
        });

    // Arm the first read on the strand, like every later one.
    auto self = shared_from_this();
    boost::asio::post(strand_, [self] { self->do_read(); });
}

void endpoint::stop()
{
    shutdown({});
}

void endpoint::shutdown(std::function<void()> done)
{
    if (!running_.exchange(false))
    {
        if (done)
            done();
        return;
    }

    auto self = shared_from_this();
    boost::asio::post(strand_, [self, done = std::move(done)] {
        if (self->transport_)
        {
            self->transport_->cancel();
            self->transport_->close();
        }
        {
            std::lock_guard lock(self->write_mutex_);
            self->writing_ = false;
            self->write_queue_.clear();
            self->write_queue_bytes_ = 0;
        }
        self->transition(endpoint_state::closed);
        if (done)
            done();
    });
}

void endpoint::close()
{
    running_ = false;
    if (transport_)
    {
        transport_->cancel();
        transport_->close();
    }
    transition(endpoint_state::closed);
}

void endpoint::do_read()
{
    if (!running_.load())
        return;

    // The transport takes a std::function, which carries no executor, so a
    // bind_executor around the handler would be lost on the way. The completion
    // enters the strand here instead, and everything that touches the
    // transport stays serialised, as the serial library's contract asks.
    auto self = shared_from_this();
    transport_->async_read_some(
        boost::asio::buffer(read_buffer_),
        [self](boost::system::error_code ec, std::size_t count) {
            boost::asio::post(self->strand_,
                              [self, ec, count] { self->on_read(ec, count); });
        });
}

void endpoint::on_read(boost::system::error_code ec, std::size_t count)
{
    VSS_PROFILE_ZONE("endpoint.read");

    if (!running_.load())
        return;

    if (ec)
    {
        handle_read_failure(ec, "read");
        return;
    }

    if (count == 0)
    {
        // A transport that has control lines completes a read with no bytes
        // when only its lines changed (an RFC 2217 client set DTR).
        if (transport_ && transport_->supports_control_lines())
            refresh_lines();
        do_read();
        return;
    }

    {
        std::lock_guard lock(stats_mutex_);
        stats_.bytes_rx += count;
        stats_.read_ops += 1;
        stats_.last_activity = clock_type::now();
    }

    auto chunk = std::make_shared<const byte_buffer>(
        read_buffer_.begin(), read_buffer_.begin() + static_cast<std::ptrdiff_t>(count));

    // Route on the I/O thread: the fast path does not wait for the event
    // dispatcher, and never copies for links with no subscriber.
    owner_.route_data(id_, *chunk);
    if (options_.capture_traffic)
        emit_data(data_direction::rx, std::move(chunk));

    do_read();
}

void endpoint::handle_read_failure(const boost::system::error_code& ec,
                                   std::string_view what)
{
    if (ec == boost::asio::error::operation_aborted)
    {
        // A cancel from stop()/close(); the state transition is handled there.
        return;
    }

    running_ = false;
    {
        std::lock_guard lock(stats_mutex_);
        stats_.errors += 1;
        stats_.last_activity = clock_type::now();
    }

    const bool graceful = ec == boost::asio::error::eof
        || ec == boost::asio::error::connection_reset
        || ec == boost::asio::error::broken_pipe;

    if (transport_)
        transport_->close();

    if (graceful)
    {
        transition(endpoint_state::closed);
        emit(ec, std::string(what) + ": peer closed the connection",
             event_kind::stopped);
    }
    else
    {
        transition(endpoint_state::failed);
        emit(ec, std::string(what) + " failed");
    }
}

void endpoint::request_write()
{
    bool start = false;
    {
        std::lock_guard lock(write_mutex_);
        if (!writing_)
        {
            writing_ = true;
            start = true;
        }
    }
    if (!start)
        return;

    auto self = shared_from_this();
    boost::asio::post(strand_, [self] { self->start_next_write(); });
}

void endpoint::start_next_write()
{
    std::shared_ptr<pending_write> next;
    {
        std::lock_guard lock(write_mutex_);
        if (write_queue_.empty())
        {
            writing_ = false;
            return;
        }
        next = write_queue_.front();
    }

    if (!transport_ || !transport_->is_open())
    {
        std::lock_guard lock(write_mutex_);
        writing_ = false;
        write_queue_.clear();
        write_queue_bytes_ = 0;
        return;
    }

    auto self = shared_from_this();
    // As in do_read: the std::function loses an executor binding, so the
    // completion enters the strand explicitly.
    transport_->async_write_some(
        boost::asio::buffer(*next->data) + next->offset,
        [self, next](boost::system::error_code ec, std::size_t count) {
            boost::asio::post(self->strand_, [self, next, ec, count] {
                self->on_write(next, ec, count);
            });
        });
}

void endpoint::on_write(const std::shared_ptr<pending_write>& write,
                        boost::system::error_code ec, std::size_t count)
{
    VSS_PROFILE_ZONE("endpoint.write");

    if (ec)
    {
        std::lock_guard lock(write_mutex_);
        writing_ = false;
        write_queue_bytes_ = 0;
        write_queue_.clear();
        if (write->handler)
        {
            auto handler = std::move(write->handler);
            boost::asio::post(strand_, [handler, ec] { handler(ec, 0); });
        }
        {
            std::lock_guard stats_lock(stats_mutex_);
            stats_.errors += 1;
        }
        emit(ec, "write failed");
        return;
    }

    write->offset += count;

    {
        std::lock_guard lock(stats_mutex_);
        stats_.bytes_tx += count;
        stats_.write_ops += 1;
        stats_.last_activity = clock_type::now();
    }

    const bool complete = write->offset >= write->data->size();

    if (complete)
    {
        {
            std::lock_guard lock(write_mutex_);
            if (!write_queue_.empty() && write_queue_.front() == write)
                write_queue_.pop_front();
            write_queue_bytes_ -= std::min(write_queue_bytes_, write->data->size());
        }
        if (options_.capture_traffic)
        {
            auto done = std::shared_ptr<const byte_buffer>(write->data);
            emit_data(data_direction::tx, std::move(done));
        }
        if (write->handler)
        {
            auto handler = std::move(write->handler);
            const std::size_t total = write->data->size();
            boost::asio::post(strand_, [handler, total] { handler({}, total); });
        }
    }

    start_next_write();
}

void endpoint::write(bytes_view data, write_handler handler)
{
    write(byte_buffer(data.begin(), data.end()), std::move(handler));
}

void endpoint::write(byte_buffer data, write_handler handler)
{
    if (data.empty())
    {
        if (handler)
        {
            boost::asio::post(strand_, [handler] { handler({}, 0); });
        }
        return;
    }

    auto chunk = std::make_shared<byte_buffer>(std::move(data));
    const std::size_t size = chunk->size();

    {
        std::lock_guard lock(write_mutex_);
        if (write_queue_bytes_ + size > options_.max_write_queue_bytes)
        {
            {
                std::lock_guard stats_lock(stats_mutex_);
                stats_.dropped += size;
            }
            if (handler)
            {
                auto rejected = std::move(handler);
                boost::asio::post(strand_, [rejected] {
                    rejected(make_error_code(errc::queue_full), 0);
                });
            }
            emit(make_error_code(errc::queue_full), "write queue full");
            return;
        }

        auto pending = std::make_shared<pending_write>();
        pending->data = std::move(chunk);
        pending->handler = std::move(handler);
        write_queue_.push_back(std::move(pending));
        write_queue_bytes_ += size;
    }

    request_write();
}

result<std::size_t> endpoint::write_sync(bytes_view data,
                                         std::chrono::milliseconds timeout)
{
    auto promise = std::make_shared<std::promise<std::pair<error_code, std::size_t>>>();
    auto future = promise->get_future();

    write(data, [promise](boost::system::error_code ec, std::size_t count) {
        promise->set_value({ec, count});
    });

    if (timeout.count() <= 0)
    {
        const auto [ec, count] = future.get();
        if (ec)
            return ec;
        return count;
    }

    if (future.wait_for(timeout) != std::future_status::ready)
        return make_error_code(errc::timeout);

    const auto [ec, count] = future.get();
    if (ec)
        return ec;
    return count;
}

control_lines endpoint::lines() const
{
    std::lock_guard lock(lines_mutex_);
    return lines_;
}

void endpoint::refresh_lines()
{
    if (!transport_)
        return;

    transport_->refresh_lines();
    control_lines updated;
    {
        std::lock_guard lock(lines_mutex_);
        const control_lines before = lines_;
        lines_ = transport_->lines();
        updated = lines_;
        if (updated == before)
            return;
    }
    emit_lines();
    owner_.route_lines(id_, updated);
}

error_code endpoint::set_line(control_line line, bool state)
{
    if (!transport_)
        return make_error_code(errc::not_open);
    if (!transport_->supports_control_lines())
        return make_error_code(errc::unsupported);

    transport_->set_control_line(line, state);

    control_lines snapshot;
    {
        std::lock_guard lock(lines_mutex_);
        lines_.set(line, state);
        snapshot = lines_;
    }
    emit_lines();
    owner_.route_lines(id_, snapshot);
    return {};
}

void endpoint::set_peer_inputs(control_lines inputs)
{
    control_lines updated;
    {
        std::lock_guard lock(lines_mutex_);
        updated = with_peer_inputs(lines_, inputs);
        if (updated == lines_)
            return;
        lines_ = updated;
    }

    if (transport_ && transport_->supports_control_lines())
    {
        transport_->set_control_line(control_line::cts, updated.cts());
        transport_->set_control_line(control_line::dsr, updated.dsr());
        transport_->set_control_line(control_line::dcd, updated.dcd());
    }

    emit_lines();
}

endpoint_stats endpoint::stats() const
{
    std::lock_guard lock(stats_mutex_);
    return stats_;
}

void endpoint::reset_stats()
{
    std::lock_guard lock(stats_mutex_);
    const auto started = stats_.started;
    stats_ = endpoint_stats{};
    stats_.started = started;
}

std::string endpoint::name() const
{
    std::lock_guard lock(name_mutex_);
    return name_;
}

void endpoint::set_name(std::string name)
{
    std::lock_guard lock(name_mutex_);
    name_ = std::move(name);
}

endpoint_info endpoint::info() const
{
    endpoint_info out;
    out.id = id_;
    out.name = name();
    out.kind = kind_;
    out.state = state();
    out.path = public_path();
    out.description = description();
    out.serial = options_.serial;
    out.lines = lines();
    out.stats = stats();
    out.supports_control_lines =
        transport_ && transport_->supports_control_lines();
    out.open = is_open();
    if (transport_)
    {
        out.peer_format = transport_->peer_format();
        out.clients = transport_->client_count();
    }
    return out;
}

void endpoint::transition(endpoint_state next)
{
    const endpoint_state previous = state_.exchange(next);
    if (previous == next)
        return;

    endpoint_event event;
    event.kind = next == endpoint_state::open ? event_kind::started
                                               : event_kind::stopped;
    event.endpoint = id_;
    event.name = name();
    event.when = clock_type::now();
    event.message = std::string(to_string(previous)) + " -> "
        + to_string(next);
    owner_.publish(std::move(event));
}

void endpoint::emit(error_code ec, std::string message, event_kind kind)
{
    endpoint_event event;
    event.kind = kind;
    event.endpoint = id_;
    event.name = name();
    event.error = ec;
    event.message = std::move(message);
    event.when = clock_type::now();
    owner_.publish(std::move(event));
}

void endpoint::emit_data(data_direction direction,
                         std::shared_ptr<const byte_buffer> data)
{
    endpoint_event event;
    event.kind = event_kind::data;
    event.endpoint = id_;
    event.name = name();
    event.direction = direction;
    event.data = std::move(data);
    event.when = clock_type::now();
    owner_.publish(std::move(event));
}

void endpoint::emit_lines()
{
    endpoint_event event;
    event.kind = event_kind::lines;
    event.endpoint = id_;
    event.name = name();
    event.lines = lines();
    event.when = clock_type::now();
    owner_.publish(std::move(event));
}

} // namespace vss
