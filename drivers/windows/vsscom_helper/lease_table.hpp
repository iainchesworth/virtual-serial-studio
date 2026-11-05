//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Which slots of the helper are taken, and by whom. It holds the rules and
// nothing of Windows, so a test can run them anywhere: the lowest free slot, a
// limit for each owner, and a limit for all owners together.
//
// An owner is any string that is the same for the same user. The helper uses
// the binary form of the user's SID.
//

#pragma once

#include <cstddef>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace vss::helper
{

enum class lease_status
{
    granted,
    owner_quota, // this owner holds the most they may
    no_slots     // every slot is taken
};

struct lease_grant
{
    lease_status status = lease_status::no_slots;
    std::size_t slot = 0; // valid when status is granted
};

class lease_table
{
public:
    lease_table(std::size_t max_total, std::size_t max_per_owner)
        : max_per_owner_(max_per_owner), owners_(max_total),
          taken_(max_total, false)
    {
    }

    /// Takes the lowest free slot for `owner`. The lowest free slot, so that a
    /// user who makes a port again gets the same slot and with it the same COM
    /// name.
    lease_grant acquire(std::string_view owner)
    {
        const std::lock_guard lock(mutex_);

        std::size_t held = 0;
        for (std::size_t slot = 0; slot < taken_.size(); ++slot)
        {
            if (taken_[slot] && owners_[slot] == owner)
                ++held;
        }
        // The owner's limit is reported first: a user at the limit should hear
        // that, not that the machine is full.
        if (held >= max_per_owner_)
            return {lease_status::owner_quota, 0};

        for (std::size_t slot = 0; slot < taken_.size(); ++slot)
        {
            if (taken_[slot])
                continue;
            taken_[slot] = true;
            owners_[slot] = std::string(owner);
            return {lease_status::granted, slot};
        }
        return {lease_status::no_slots, 0};
    }

    /// Gives the slot back. False when `owner` does not hold it, which leaves
    /// the table as it was.
    bool release(std::string_view owner, std::size_t slot)
    {
        const std::lock_guard lock(mutex_);
        if (slot >= taken_.size() || !taken_[slot] || owners_[slot] != owner)
            return false;
        taken_[slot] = false;
        owners_[slot].clear();
        return true;
    }

    std::size_t count() const
    {
        const std::lock_guard lock(mutex_);
        std::size_t total = 0;
        for (const bool taken : taken_)
            total += taken ? 1 : 0;
        return total;
    }

    std::size_t count_of(std::string_view owner) const
    {
        const std::lock_guard lock(mutex_);
        std::size_t total = 0;
        for (std::size_t slot = 0; slot < taken_.size(); ++slot)
            total += (taken_[slot] && owners_[slot] == owner) ? 1 : 0;
        return total;
    }

private:
    mutable std::mutex mutex_;
    std::size_t max_per_owner_;
    std::vector<std::string> owners_;
    std::vector<bool> taken_;
};

} // namespace vss::helper
