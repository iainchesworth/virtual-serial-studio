//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_CORE_TRAFFIC_LOG_HPP
#define VSS_CORE_TRAFFIC_LOG_HPP

#include <vss/core/bytes.hpp>
#include <vss/core/stats.hpp>
#include <vss/core/types.hpp>

#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace vss {

/// One captured chunk of traffic, shared with subscribers.
struct traffic_record
{
    endpoint_id endpoint = 0;
    std::string name;
    data_direction direction = data_direction::rx;
    std::shared_ptr<const byte_buffer> data;
    time_point when{};
};

/// Bounded ring buffer of recent traffic, used by the monitor views.
class traffic_log
{
public:
    explicit traffic_log(std::size_t capacity = 8192);

    void push(traffic_record record);

    /// Newest last.  `limit == 0` returns everything retained.
    std::vector<traffic_record> snapshot(std::size_t limit = 0) const;

    void clear();

    std::size_t size() const;
    std::size_t capacity() const;
    void set_capacity(std::size_t capacity);

private:
    mutable std::mutex mutex_;
    std::deque<traffic_record> records_;
    std::size_t capacity_;
};

} // namespace vss

#endif // VSS_CORE_TRAFFIC_LOG_HPP
