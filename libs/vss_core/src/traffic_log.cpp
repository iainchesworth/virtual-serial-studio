//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <vss/core/traffic_log.hpp>

#include <algorithm>

namespace vss {

traffic_log::traffic_log(std::size_t capacity)
    : capacity_(capacity == 0 ? 1 : capacity)
{
}

void traffic_log::push(traffic_record record)
{
    std::lock_guard lock(mutex_);
    records_.push_back(std::move(record));
    while (records_.size() > capacity_)
        records_.pop_front();
}

std::vector<traffic_record> traffic_log::snapshot(std::size_t limit) const
{
    std::lock_guard lock(mutex_);
    const std::size_t count =
        limit == 0 ? records_.size() : std::min(limit, records_.size());
    return {records_.end() - static_cast<std::ptrdiff_t>(count), records_.end()};
}

void traffic_log::clear()
{
    std::lock_guard lock(mutex_);
    records_.clear();
}

std::size_t traffic_log::size() const
{
    std::lock_guard lock(mutex_);
    return records_.size();
}

std::size_t traffic_log::capacity() const
{
    std::lock_guard lock(mutex_);
    return capacity_;
}

void traffic_log::set_capacity(std::size_t capacity)
{
    std::lock_guard lock(mutex_);
    capacity_ = capacity == 0 ? 1 : capacity;
    while (records_.size() > capacity_)
        records_.pop_front();
}

} // namespace vss
