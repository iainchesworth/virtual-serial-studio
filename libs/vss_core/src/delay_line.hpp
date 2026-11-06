//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#pragma once

#include <vss/core/bytes.hpp>
#include <vss/core/endpoint.hpp>
#include <vss/core/stats.hpp>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <random>

namespace vss {

/// Holds the bytes of one route (one source to one target on one link) for
/// the link's delay before writing them to the target.
///
/// The queue is first in, first out and the due times never go backwards, so
/// jitter cannot reorder the bytes.  One timer serves the head of the queue,
/// on a strand of its own, so two chunks never race each other to the target.
/// The hub owns the line; dropping the last reference (the route went away)
/// discards whatever is still waiting.
class delay_line : public std::enable_shared_from_this<delay_line>
{
public:
    delay_line(boost::asio::any_io_executor executor,
               std::shared_ptr<endpoint> target, std::uint32_t delay_ms,
               std::uint32_t jitter_ms);

    delay_line(const delay_line&) = delete;
    delay_line& operator=(const delay_line&) = delete;

    /// Copies `data` and writes it to the target once its delay has passed.
    /// Safe to call from any thread; calls from one thread keep their order.
    void push(bytes_view data);

private:
    struct item
    {
        time_point due;
        byte_buffer data;
    };

    void arm();
    void fire();

    const boost::asio::strand<boost::asio::any_io_executor> strand_;
    boost::asio::steady_timer timer_;
    const std::shared_ptr<endpoint> target_;
    const std::uint32_t delay_ms_;
    const std::uint32_t jitter_ms_;

    std::mutex mutex_;
    std::deque<item> queue_;
    time_point last_due_{};
    std::mt19937 random_;
};

} // namespace vss
