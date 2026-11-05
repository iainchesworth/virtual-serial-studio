//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#pragma once

#include <vss/core/error.hpp>
#include <vss/core/transport.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace vss {

enum class listen_protocol
{
    tcp,
    udp,
    /// TCP carrying Telnet with the COM-PORT option: an RFC 2217 access
    /// server.
    rfc2217,
};

/// Where a listening endpoint waits for other computers.
struct listen_address
{
    listen_protocol protocol = listen_protocol::tcp;
    /// An IP address; empty means every IPv4 interface.
    std::string host;
    /// 0 asks the system for a free port.
    std::uint16_t port = 0;

    friend bool operator==(const listen_address&, const listen_address&) = default;
};

/// True for `tcp-listen://`, `udp-listen://` and `rfc2217-listen://`, the
/// schemes of an endpoint that waits for others to connect instead of
/// connecting itself.
bool is_listen_uri(std::string_view uri) noexcept;

/// Parses `tcp-listen://host:port`, `udp-listen://host:port` and
/// `rfc2217-listen://host:port`. The host is
/// an IP address, `[::1]` for IPv6, or empty for every IPv4 interface. The
/// URI carries nothing else.
result<listen_address> parse_listen_uri(std::string_view uri);

/// The URI for an address, the inverse of parse_listen_uri.
std::string make_listen_uri(const listen_address& address);

/// A byte channel that waits for other computers.
///
/// TCP: any number of clients may connect. What they send is read by the hub in
/// the order it arrives, and what the hub writes goes to every client that is
/// connected at that moment. With no client connected a write succeeds and the
/// bytes are dropped, as on a serial port with nothing plugged in.
///
/// UDP: datagrams from anyone are read, and a write is sent to every sender
/// heard from lately (up to sixteen).
///
/// RFC 2217: TCP clients speak Telnet and negotiate the COM-PORT option, so a
/// serial-over-network client (pySerial's `rfc2217://`, a terminal server
/// driver) can open the endpoint as a serial port. Payload bytes travel as
/// they do for TCP, with 0xFF escaped. The client's baud rate, framing, flow
/// control, DTR, RTS and break are accepted and remembered but change no real
/// device; DTR and RTS are the endpoint's output lines, and CTS, DSR, DCD and
/// RI that reach the endpoint are sent to the clients as modem state.
class listen_transport final : public transport
{
public:
    /// Binds and starts listening. `ec` is set when the address is in use or
    /// cannot be bound.
    listen_transport(const executor_type& executor, listen_address address,
                     error_code& ec);
    ~listen_transport() override;

    listen_transport(const listen_transport&) = delete;
    listen_transport& operator=(const listen_transport&) = delete;

    std::string describe() const override;
    std::string public_path() const override;
    bool is_open() const override;

    void async_read_some(boost::asio::mutable_buffer buffer,
                         io_handler handler) override;
    void async_write_some(boost::asio::const_buffer buffer,
                          io_handler handler) override;

    void close() override;
    void cancel() override;

    /// True for the RFC 2217 role, which has control lines.
    bool supports_control_lines() const noexcept override;
    void set_control_line(control_line line, bool state) override;
    control_lines lines() const override;

    /// The port actually bound, which differs from the request when that was 0.
    std::uint16_t local_port() const noexcept;

    /// TCP clients connected now, or UDP senders heard from.
    std::size_t peer_count() const noexcept;

private:
    class state;
    std::shared_ptr<state> state_;
};

} // namespace vss
