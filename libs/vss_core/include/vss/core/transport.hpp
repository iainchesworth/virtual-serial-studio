//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_CORE_TRANSPORT_HPP
#define VSS_CORE_TRANSPORT_HPP

#include <vss/core/control_lines.hpp>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/system/error_code.hpp>

#include <cstddef>
#include <functional>
#include <string>

namespace vss {

/// The byte channel below an endpoint.
///
/// A transport is deliberately *not* a serial port abstraction: it is the
/// smallest thing the router needs, so a pseudo terminal, a named pipe, an
/// in-memory loopback or `boost::virtual_serial::any_serial_port` all fit
/// behind it.
///
/// Threading: every completion handler runs on the executor the transport was
/// constructed with.  Implementations must tolerate `close()` from another
/// thread while an operation is outstanding, and must complete pending
/// handlers exactly once with `boost::asio::error::operation_aborted`.
class transport
{
public:
    using executor_type = boost::asio::any_io_executor;
    using io_handler =
        std::function<void(boost::system::error_code, std::size_t)>;

    virtual ~transport() = default;

    /// Human readable transport description shown in the GUI.
    virtual std::string describe() const = 0;

    /// OS path other applications use, when there is one.
    virtual std::string public_path() const { return {}; }

    virtual bool is_open() const = 0;

    virtual void async_read_some(boost::asio::mutable_buffer buffer,
                                 io_handler handler) = 0;
    virtual void async_write_some(boost::asio::const_buffer buffer,
                                  io_handler handler) = 0;

    virtual void close() = 0;
    virtual void cancel() {}

    virtual bool supports_control_lines() const noexcept { return false; }
    virtual void set_control_line(control_line, bool) {}
    virtual control_lines lines() const { return {}; }

    /// Re-reads the lines from the device, for transports that poll.
    virtual void refresh_lines() {}

    /// Something a transport has to say about its device that belongs in the
    /// log, such as that a program opened or closed it.  The endpoint hands the
    /// transport a function that logs one line against it; the transport may
    /// call it from any thread, and must stop calling it when it closes.  Most
    /// transports have nothing to say and ignore it.
    using notice_sink = std::function<void(std::string)>;
    virtual void set_notice_sink(notice_sink) {}
};

} // namespace vss

#endif // VSS_CORE_TRANSPORT_HPP
