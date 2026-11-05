//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "../virtual_port_platform.hpp"
#include "pty_slave_path.hpp"

#include <vss/ports/native_lines.hpp>

#include <boost/asio/posix/stream_descriptor.hpp>

#include <atomic>
#include <fcntl.h>
#include <memory>
#include <stdlib.h>
#include <string>
#include <termios.h>
#include <unistd.h>
#include <utility>

namespace vss::detail
{
namespace
{

/// A pseudo terminal pair.  The master is ours; the slave path is handed to
/// the rest of the world.
class pty_transport final : public transport
{
public:
    pty_transport(const executor_type& executor, int master, int slave,
                  std::string path, std::string name)
        : executor_(executor), descriptor_(executor, master), slave_fd_(slave),
          path_(std::move(path)), name_(std::move(name))
    {
        boost::system::error_code ignored;
        descriptor_.non_blocking(true, ignored);
        descriptor_.native_non_blocking(true, ignored);
        lines_.set(control_line::dtr, true);
        lines_.set(control_line::rts, true);
        lines_.set(control_line::cts, true);
        lines_.set(control_line::dsr, true);
        lines_.set(control_line::dcd, true);
        refresh_lines();
    }

    ~pty_transport() override { close(); }

    std::string describe() const override
    {
        return "pseudo terminal " + path_ + " (" + name_ + ")";
    }

    std::string public_path() const override { return path_; }

    bool is_open() const override { return open_.load(); }

    void async_read_some(boost::asio::mutable_buffer buffer,
                         io_handler handler) override
    {
        descriptor_.async_read_some(buffer, std::move(handler));
    }

    void async_write_some(boost::asio::const_buffer buffer,
                          io_handler handler) override
    {
        descriptor_.async_write_some(buffer, std::move(handler));
    }

    void close() override
    {
        if (!open_.exchange(false))
            return;
        boost::system::error_code ignored;
        descriptor_.cancel(ignored);
        descriptor_.close(ignored);
        if (slave_fd_ >= 0)
        {
            ::close(slave_fd_);
            slave_fd_ = -1;
        }
    }

    void cancel() override
    {
        boost::system::error_code ignored;
        descriptor_.cancel(ignored);
    }

    bool supports_control_lines() const noexcept override { return true; }

    void set_control_line(control_line line, bool state) override
    {
        if (const auto written =
                native::write_line(descriptor_.native_handle(), line, state);
            !written)
            return;
        lines_.set(line, state);
    }

    control_lines lines() const override { return lines_; }

    void refresh_lines() override
    {
        const auto status =
            native::read_modem_status(descriptor_.native_handle());
        if (!status)
            return;
        lines_.set(control_line::cts, status->cts);
        lines_.set(control_line::dsr, status->dsr);
        lines_.set(control_line::dcd, status->dcd);
        lines_.set(control_line::ri, status->ri);
    }

private:
    executor_type executor_;
    boost::asio::posix::stream_descriptor descriptor_;
    int slave_fd_ = -1;
    std::string path_;
    std::string name_;
    control_lines lines_;
    std::atomic<bool> open_{true};
};

} // namespace

result<virtual_port_result>
platform_create_pty(const transport::executor_type& executor,
                    const std::string& name)
{
    const int master = ::posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0)
        return make_error_code(errc::io_error);

    if (::grantpt(master) != 0 || ::unlockpt(master) != 0)
    {
        ::close(master);
        return make_error_code(errc::io_error);
    }

    std::string slave_path;
    if (!platform_pty_slave_path(master, slave_path))
    {
        ::close(master);
        return make_error_code(errc::io_error);
    }

    // Keep the slave open so the master never reports end-of-file while no
    // application has connected yet, and put it in raw mode so the line
    // discipline does not translate or echo anything.
    const int slave =
        ::open(slave_path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (slave >= 0)
    {
        struct termios settings{};
        if (::tcgetattr(slave, &settings) == 0)
        {
            ::cfmakeraw(&settings);
            settings.c_cflag |= (CLOCAL | CREAD);
            ::tcsetattr(slave, TCSANOW, &settings);
        }
    }

    auto transport_port = std::make_shared<pty_transport>(
        executor, master, slave, slave_path, name);

    virtual_port_result out;
    out.port = std::move(transport_port);
    out.path = slave_path;
    out.kind = virtual_port_kind::pty;
    return out;
}

bool platform_supports_pty() noexcept
{
    return true;
}

} // namespace vss::detail
