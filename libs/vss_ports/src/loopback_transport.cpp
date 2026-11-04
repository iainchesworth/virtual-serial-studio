//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <vss/ports/loopback_transport.hpp>

#include <boost/asio/associated_executor.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/post.hpp>

#include <algorithm>
#include <utility>

namespace vss {
namespace {

/// Completes `handler` on the executor it was bound to, falling back to the
/// transport's own executor.  Honouring the association is what keeps a
/// strand-bound endpoint consumer serialised with close().
template <class Handler, class... Args>
void complete_on(const loopback_transport::executor_type& fallback,
                 Handler& handler, Args&&... args)
{
    auto executor = boost::asio::get_associated_executor(handler, fallback);
    boost::asio::post(executor, [handler, args...]() mutable {
        handler(args...);
    });
}

} // namespace

loopback_transport::loopback_transport(const executor_type& executor,
                                       std::string name)
    : executor_(executor)
    , name_(std::move(name))
{
    lines_.set(control_line::cts, true);
    lines_.set(control_line::dsr, true);
    lines_.set(control_line::dcd, true);
}

std::shared_ptr<loopback_transport> loopback_transport::create(
    const executor_type& executor, std::string name)
{
    return std::make_shared<loopback_transport>(executor, std::move(name));
}

std::string loopback_transport::describe() const
{
    return "loopback device '" + name_ + "'";
}

std::size_t loopback_transport::available() const
{
    std::lock_guard lock(mutex_);
    return buffer_.size();
}

loopback_transport::io_handler loopback_transport::take_pending_read()
{
    // std::move alone is not enough: libc++ leaves a moved-from std::function
    // that holds a small callable non-empty, which would keep the endpoint the
    // callable captures alive and let a second write complete it again.
    return std::exchange(pending_read_, io_handler{});
}

void loopback_transport::async_read_some(boost::asio::mutable_buffer buffer,
                                         io_handler handler)
{
    std::lock_guard lock(mutex_);
    if (!open_.load())
    {
        complete_on(executor_, handler, make_error_code(errc::not_open),
                    std::size_t{0});
        return;
    }

    if (!buffer_.empty())
    {
        const std::size_t count =
            std::min<std::size_t>(buffer.size(), buffer_.size());
        auto* target = static_cast<byte*>(buffer.data());
        for (std::size_t index = 0; index < count; ++index)
        {
            target[index] = buffer_.front();
            buffer_.pop_front();
        }
        complete_on(executor_, handler, boost::system::error_code{}, count);
        return;
    }

    // Nothing buffered: park the handler until somebody writes.
    pending_read_ = std::move(handler);
    read_buffer_ = buffer;
}

void loopback_transport::async_write_some(boost::asio::const_buffer buffer,
                                          io_handler handler)
{
    const auto* data = static_cast<const byte*>(buffer.data());
    const std::size_t size = buffer.size();

    {
        std::lock_guard lock(mutex_);
        if (!open_.load())
        {
            complete_on(executor_, handler, make_error_code(errc::not_open),
                        std::size_t{0});
            return;
        }

        if (pending_read_)
        {
            auto parked = take_pending_read();
            auto target = read_buffer_;
            const std::size_t count =
                std::min<std::size_t>(target.size(), size);
            auto* destination = static_cast<byte*>(target.data());
            std::copy_n(data, count, destination);
            for (std::size_t index = count; index < size; ++index)
                buffer_.push_back(data[index]);

            complete_on(executor_, parked, boost::system::error_code{}, count);
            complete_on(executor_, handler, boost::system::error_code{}, size);
            return;
        }

        buffer_.insert(buffer_.end(), data, data + size);
    }

    complete_on(executor_, handler, boost::system::error_code{}, size);
}

void loopback_transport::inject(bytes_view data)
{
    std::lock_guard lock(mutex_);
    if (!open_.load() || data.empty())
        return;

    if (pending_read_)
    {
        auto parked = take_pending_read();
        auto target = read_buffer_;
        const std::size_t count =
            std::min<std::size_t>(target.size(), data.size());
        std::copy_n(data.data(), count, static_cast<byte*>(target.data()));
        for (std::size_t index = count; index < data.size(); ++index)
            buffer_.push_back(data[index]);
        complete_on(executor_, parked, boost::system::error_code{}, count);
        return;
    }

    buffer_.insert(buffer_.end(), data.begin(), data.end());
}

void loopback_transport::set_control_line(control_line line, bool state)
{
    std::lock_guard lock(mutex_);
    lines_.set(line, state);
}

control_lines loopback_transport::lines() const
{
    std::lock_guard lock(mutex_);
    return lines_;
}

void loopback_transport::finish_pending_read()
{
    io_handler parked;
    {
        std::lock_guard lock(mutex_);
        parked = take_pending_read();
    }
    if (parked)
        complete_on(executor_, parked, boost::asio::error::operation_aborted,
                    std::size_t{0});
}

void loopback_transport::close()
{
    {
        std::lock_guard lock(mutex_);
        if (!open_.exchange(false))
            return;
        buffer_.clear();
    }
    finish_pending_read();
}

void loopback_transport::cancel()
{
    finish_pending_read();
}

} // namespace vss
