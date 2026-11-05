//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_CORE_ENDPOINT_HPP
#define VSS_CORE_ENDPOINT_HPP

#include <vss/core/control_lines.hpp>
#include <vss/core/event.hpp>
#include <vss/core/serial_settings.hpp>
#include <vss/core/stats.hpp>
#include <vss/core/transport.hpp>
#include <vss/core/types.hpp>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/strand.hpp>

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace vss {

class hub;

/// How much a single endpoint buffers and records.
struct endpoint_options
{
    serial_settings serial;

    /// Read buffer handed to the transport per operation.
    std::size_t read_buffer_size = std::size_t{64} * 1024;

    /// Outstanding bytes allowed before new writes are rejected.
    std::size_t max_write_queue_bytes = std::size_t{8} * 1024 * 1024;

    /// Record traffic in the hub's ring buffer.
    bool capture_traffic = true;
};

/// Snapshot of an endpoint for the GUI and the CLI.
struct endpoint_info
{
    endpoint_id id = 0;
    std::string name;
    std::string path;
    std::string description;
    endpoint_kind kind = endpoint_kind::virtual_port;
    endpoint_state state = endpoint_state::created;
    serial_settings serial;
    control_lines lines;
    endpoint_stats stats;
    bool supports_control_lines = false;
    bool open = false;
};

using write_handler =
    std::function<void(boost::system::error_code, std::size_t)>;

/// A named node in the topology: one transport plus its queues and counters.
///
/// Endpoints are created by #hub, never directly, and are owned by it.  All
/// public methods are safe to call from any thread; traffic callbacks run on
/// the endpoint's strand.
class endpoint : public std::enable_shared_from_this<endpoint>
{
public:
    ~endpoint();

    endpoint(const endpoint&) = delete;
    endpoint& operator=(const endpoint&) = delete;

    endpoint_id id() const noexcept { return id_; }
    /// The current name.  A rename changes it, so it is returned by value.
    std::string name() const;
    endpoint_kind kind() const noexcept { return kind_; }
    endpoint_state state() const noexcept;
    bool is_open() const noexcept;

    std::string public_path() const;
    std::string description() const;
    const endpoint_options& options() const noexcept { return options_; }

    boost::asio::any_io_executor get_executor() const;

    /// Begins the read loop.  Idempotent.
    void start();
    /// Stops reading and closes the transport.
    void stop();
    /// Closes the transport without destroying the endpoint.
    void close();

    /// Ordered shutdown: closes on the endpoint's strand and then invokes
    /// `done`.  Used by #hub::stop so teardown cannot race a completion.
    void shutdown(std::function<void()> done);

    /// Queues `data` for the transport, preserving call order.
    void write(bytes_view data, write_handler handler = {});
    void write(byte_buffer data, write_handler handler = {});

    /// Writes and waits, bounded by `timeout`.  For tools and tests.
    result<std::size_t> write_sync(bytes_view data,
                                   std::chrono::milliseconds timeout);

    control_lines lines() const;
    void refresh_lines();
    error_code set_line(control_line line, bool state);

    endpoint_stats stats() const;
    endpoint_info info() const;
    void reset_stats();

    /// Applies the modem inputs a peer drives (CTS/DSR/DCD/RI).
    void set_peer_inputs(control_lines inputs);

private:
    friend class hub;

    struct pending_write
    {
        std::shared_ptr<byte_buffer> data;
        std::size_t offset = 0;
        write_handler handler;
    };

    endpoint(hub& owner, endpoint_id id, std::string name, endpoint_kind kind,
             std::shared_ptr<transport> transport, endpoint_options options);

    /// Called by the hub, which holds its lock, when it renames the endpoint.
    void set_name(std::string name);

    void do_read();
    void on_read(boost::system::error_code ec, std::size_t count);
    void request_write();
    void start_next_write();
    void on_write(const std::shared_ptr<pending_write>& write,
                  boost::system::error_code ec, std::size_t count);
    void handle_read_failure(const boost::system::error_code& ec,
                             std::string_view what);
    void transition(endpoint_state next);
    void emit(error_code ec, std::string message,
              event_kind kind = event_kind::error);
    void emit_data(data_direction direction,
                   std::shared_ptr<const byte_buffer> data);
    void emit_lines();

    hub& owner_;
    const endpoint_id id_;
    mutable std::mutex name_mutex_;
    std::string name_;
    const endpoint_kind kind_;
    const std::shared_ptr<transport> transport_;
    endpoint_options options_;
    const boost::asio::strand<boost::asio::any_io_executor> strand_;

    byte_buffer read_buffer_;
    mutable std::mutex write_mutex_;
    std::deque<std::shared_ptr<pending_write>> write_queue_;
    std::size_t write_queue_bytes_ = 0;
    bool writing_ = false;
    std::atomic<bool> running_{false};
    std::atomic<endpoint_state> state_{endpoint_state::created};

    mutable std::mutex stats_mutex_;
    endpoint_stats stats_;

    mutable std::mutex lines_mutex_;
    control_lines lines_;
};

} // namespace vss

#endif // VSS_CORE_ENDPOINT_HPP
