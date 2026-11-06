//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#pragma once

#include <vss/core/stats.hpp>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace vss::detail {

/// Runs actions at absolute times on the hub's workers, in time order and, for
/// equal times, in the order they were added.  One timer serves the whole
/// queue, so a thousand queued writes cost one wait, and the order of two
/// replies due at the same moment never depends on the timer implementation.
///
/// An action runs on a worker thread and must not block.  It may add more
/// actions.
class timed_queue : public std::enable_shared_from_this<timed_queue>
{
public:
    explicit timed_queue(const boost::asio::any_io_executor& executor)
        : strand_(boost::asio::make_strand(executor))
        , timer_(strand_)
    {
    }

    /// Thread safe.  An action whose time has passed runs at once.
    void at(time_point when, std::function<void()> action)
    {
        {
            std::lock_guard lock(mutex_);
            if (stopped_)
                return;
            items_.push_back(item{when, next_sequence_++, std::move(action)});
            std::push_heap(items_.begin(), items_.end(), later);
        }
        boost::asio::post(strand_,
                          [self = shared_from_this()] { self->arm(); });
    }

    /// Drops what is queued and ignores what is added.
    void stop()
    {
        {
            std::lock_guard lock(mutex_);
            stopped_ = true;
            items_.clear();
        }
        boost::asio::post(strand_,
                          [self = shared_from_this()] { self->timer_.cancel(); });
    }

private:
    struct item
    {
        time_point when;
        std::uint64_t sequence = 0;
        std::function<void()> action;
    };

    static bool later(const item& a, const item& b)
    {
        if (a.when != b.when)
            return a.when > b.when;
        return a.sequence > b.sequence;
    }

    void arm()
    {
        time_point next;
        {
            std::lock_guard lock(mutex_);
            if (stopped_ || items_.empty())
                return;
            next = items_.front().when;
        }
        timer_.expires_at(next);
        timer_.async_wait([self = shared_from_this()](
                              const boost::system::error_code& error) {
            if (!error)
                self->fire();
        });
    }

    void fire()
    {
        for (;;)
        {
            std::function<void()> action;
            {
                std::lock_guard lock(mutex_);
                if (stopped_ || items_.empty() ||
                    items_.front().when > clock_type::now())
                    break;
                std::pop_heap(items_.begin(), items_.end(), later);
                action = std::move(items_.back().action);
                items_.pop_back();
            }
            if (action)
                action();
        }
        arm();
    }

    boost::asio::strand<boost::asio::any_io_executor> strand_;
    boost::asio::steady_timer timer_;
    std::mutex mutex_;
    std::vector<item> items_;
    std::uint64_t next_sequence_ = 0;
    bool stopped_ = false;
};

} // namespace vss::detail

