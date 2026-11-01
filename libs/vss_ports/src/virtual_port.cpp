//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <vss/ports/virtual_port.hpp>
#include <vss/ports/loopback_transport.hpp>
#include <vss/ports/vcom_driver.hpp>

#include <boost/asio/post.hpp>

#if !defined(_WIN32)
#  include <vss/ports/native_lines.hpp>

#  include <boost/asio/posix/stream_descriptor.hpp>

#  include <fcntl.h>
#  include <mutex>
#  include <stdlib.h>
#  include <termios.h>
#  include <unistd.h>
#endif

#include <utility>

#include <atomic>

namespace vss {
namespace {

#if !defined(_WIN32)

/// A pseudo terminal pair.  The master is ours; the slave path is handed to
/// the rest of the world.
class pty_transport final : public transport
{
public:
    pty_transport(const executor_type& executor, int master, int slave,
                  std::string path, std::string name)
        : executor_(executor)
        , descriptor_(executor, master)
        , slave_fd_(slave)
        , path_(std::move(path))
        , name_(std::move(name))
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
        const auto status = native::read_modem_status(descriptor_.native_handle());
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

result<virtual_port_result> create_pty(const transport::executor_type& executor,
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
#if defined(__linux__)
    {
        char buffer[256]{};
        if (::ptsname_r(master, buffer, sizeof(buffer)) != 0)
        {
            ::close(master);
            return make_error_code(errc::io_error);
        }
        slave_path = buffer;
    }
#else
    {
        static std::mutex ptsname_mutex;
        const std::lock_guard lock(ptsname_mutex);
        const char* resolved = ::ptsname(master);
        if (!resolved)
        {
            ::close(master);
            return make_error_code(errc::io_error);
        }
        slave_path = resolved;
    }
#endif

    // Keep the slave open so the master never reports end-of-file while no
    // application has connected yet, and put it in raw mode so the line
    // discipline does not translate or echo anything.
    const int slave = ::open(slave_path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
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

#endif // !_WIN32

} // namespace

bool supports(virtual_port_kind kind) noexcept
{
    switch (kind)
    {
    case virtual_port_kind::pty:
#if !defined(_WIN32)
        return true;
#else
        return false;
#endif
    case virtual_port_kind::windows_driver:
        return driver_available();
    case virtual_port_kind::named_pipe:
        // Named pipe virtual ports are not implemented yet; com0com covers
        // the real COM port case on Windows.
        return false;
    case virtual_port_kind::loopback:
        return true;
    }
    return false;
}

std::vector<virtual_port_kind> available_kinds()
{
    std::vector<virtual_port_kind> kinds;
    if (supports(virtual_port_kind::windows_driver))
        kinds.push_back(virtual_port_kind::windows_driver);
    if (supports(virtual_port_kind::pty))
        kinds.push_back(virtual_port_kind::pty);
    if (supports(virtual_port_kind::named_pipe))
        kinds.push_back(virtual_port_kind::named_pipe);
    kinds.push_back(virtual_port_kind::loopback);
    return kinds;
}

const char* to_string(virtual_port_kind kind) noexcept
{
    switch (kind)
    {
    case virtual_port_kind::pty: return "pty";
    case virtual_port_kind::windows_driver: return "windows-driver";
    case virtual_port_kind::named_pipe: return "named-pipe";
    case virtual_port_kind::loopback: return "loopback";
    }
    return "unknown";
}

result<virtual_port_result> create_virtual_port(
    const transport::executor_type& executor, virtual_port_kind kind,
    const std::string& name)
{
    switch (kind)
    {
    case virtual_port_kind::loopback:
    {
        auto port = loopback_transport::create(executor, name);
        virtual_port_result out;
        out.path = "loopback:" + name;
        out.kind = virtual_port_kind::loopback;
        out.port = std::move(port);
        return out;
    }
    case virtual_port_kind::pty:
#if !defined(_WIN32)
        return create_pty(executor, name);
#else
        return make_error_code(errc::unsupported);
#endif
    case virtual_port_kind::windows_driver:
        return create_driver_port(executor, name);
    case virtual_port_kind::named_pipe:
        return make_error_code(errc::unsupported);
    }
    return make_error_code(errc::invalid_argument);
}

result<virtual_port_result> create_best_virtual_port(
    const transport::executor_type& executor, const std::string& name)
{
    if (supports(virtual_port_kind::windows_driver))
        return create_virtual_port(executor, virtual_port_kind::windows_driver,
                                   name);
    if (supports(virtual_port_kind::pty))
        return create_virtual_port(executor, virtual_port_kind::pty, name);
    return create_virtual_port(executor, virtual_port_kind::loopback, name);
}

} // namespace vss
