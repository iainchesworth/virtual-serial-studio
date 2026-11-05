//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <vss/ports/listen_transport.hpp>

#include "platform/listen_socket_platform.hpp"

#include <boost/asio/buffer.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/write.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <deque>
#include <mutex>
#include <optional>
#include <vector>

namespace vss {

namespace asio = boost::asio;

namespace {

constexpr std::string_view tcp_scheme = "tcp-listen://";
constexpr std::string_view udp_scheme = "udp-listen://";

/// Bytes held for the hub before the clients are told to wait.
constexpr std::size_t receive_limit = 1U << 20;
constexpr std::size_t chunk_size = 4096;
constexpr std::size_t udp_peer_limit = 16;

} // namespace

bool is_listen_uri(std::string_view uri) noexcept
{
    return uri.starts_with(tcp_scheme) || uri.starts_with(udp_scheme);
}

result<listen_address> parse_listen_uri(std::string_view uri)
{
    listen_address address;
    if (uri.starts_with(tcp_scheme))
    {
        address.protocol = listen_protocol::tcp;
        uri.remove_prefix(tcp_scheme.size());
    }
    else if (uri.starts_with(udp_scheme))
    {
        address.protocol = listen_protocol::udp;
        uri.remove_prefix(udp_scheme.size());
    }
    else
    {
        return make_error_code(errc::invalid_uri);
    }

    std::string_view host;
    std::string_view port;
    if (uri.starts_with('['))
    {
        const auto close = uri.find(']');
        if (close == std::string_view::npos || close + 1 >= uri.size() ||
            uri[close + 1] != ':')
        {
            return make_error_code(errc::invalid_uri);
        }
        host = uri.substr(1, close - 1);
        port = uri.substr(close + 2);
    }
    else
    {
        const auto colon = uri.rfind(':');
        if (colon == std::string_view::npos)
            return make_error_code(errc::invalid_uri);
        host = uri.substr(0, colon);
        port = uri.substr(colon + 1);
        if (host.find(':') != std::string_view::npos)
            return make_error_code(errc::invalid_uri);
    }

    unsigned value = 0;
    const auto parsed =
        std::from_chars(port.data(), port.data() + port.size(), value);
    if (port.empty() || parsed.ec != std::errc{} ||
        parsed.ptr != port.data() + port.size() || value > 65535)
    {
        return make_error_code(errc::invalid_uri);
    }

    if (!host.empty())
    {
        boost::system::error_code ec;
        asio::ip::make_address(std::string(host), ec);
        if (ec)
            return make_error_code(errc::invalid_uri);
    }

    address.host = std::string(host);
    address.port = static_cast<std::uint16_t>(value);
    return address;
}

std::string make_listen_uri(const listen_address& address)
{
    std::string text(address.protocol == listen_protocol::tcp ? tcp_scheme
                                                              : udp_scheme);
    if (address.host.find(':') != std::string::npos)
        text += '[' + address.host + ']';
    else
        text += address.host;
    text += ':' + std::to_string(address.port);
    return text;
}

/// Everything runs on one strand, so the sockets and the queues need no lock.
/// The handlers hold the state, which lets it outlive the transport object
/// while an operation is still outstanding.
class listen_transport::state : public std::enable_shared_from_this<state>
{
public:
    state(const executor_type& executor, listen_address address)
        : executor_(executor)
        , strand_(asio::make_strand(executor))
        , address_(std::move(address))
        , acceptor_(strand_)
        , datagrams_(strand_)
    {
    }

    error_code open()
    {
        boost::system::error_code ec;
        asio::ip::address ip = asio::ip::address_v4::any();
        if (!address_.host.empty())
        {
            ip = asio::ip::make_address(address_.host, ec);
            if (ec)
                return make_error_code(errc::invalid_uri);
        }

        if (address_.protocol == listen_protocol::tcp)
        {
            const asio::ip::tcp::endpoint where(ip, address_.port);
            acceptor_.open(where.protocol(), ec);
            if (ec)
                return ec;
            ec = detail::platform_prepare_listening_socket(
                static_cast<std::uintptr_t>(acceptor_.native_handle()));
            if (ec)
                return ec;
            acceptor_.bind(where, ec);
            if (ec)
                return ec;
            acceptor_.listen(asio::socket_base::max_listen_connections, ec);
            if (ec)
                return ec;
            port_.store(acceptor_.local_endpoint(ec).port());
        }
        else
        {
            const asio::ip::udp::endpoint where(ip, address_.port);
            datagrams_.open(where.protocol(), ec);
            if (ec)
                return ec;
            datagrams_.bind(where, ec);
            if (ec)
                return ec;
            port_.store(datagrams_.local_endpoint(ec).port());
        }
        open_.store(true);
        return {};
    }

    void start()
    {
        if (address_.protocol == listen_protocol::tcp)
            accept_next();
        else
            receive_next();
    }

    void read(asio::mutable_buffer buffer, io_handler handler)
    {
        asio::post(strand_,
                   [self = shared_from_this(), buffer,
                    handler = std::move(handler)]() mutable
                   {
                       if (self->closed_ || !self->open_.load())
                       {
                           self->complete(std::move(handler),
                                          asio::error::operation_aborted, 0);
                           return;
                       }
                       bool busy = false;
                       {
                           std::lock_guard lock(self->read_mutex_);
                           busy = self->pending_read_.has_value();
                           if (!busy)
                               self->pending_read_ =
                                   pending{buffer, std::move(handler)};
                       }
                       if (busy)
                       {
                           self->complete(std::move(handler),
                                          asio::error::already_started, 0);
                           return;
                       }
                       self->deliver();
                   });
    }

    void write(asio::const_buffer buffer, io_handler handler)
    {
        auto payload = std::make_shared<std::vector<char>>(
            static_cast<const char*>(buffer.data()),
            static_cast<const char*>(buffer.data()) + buffer.size());
        asio::post(strand_,
                   [self = shared_from_this(), payload,
                    handler = std::move(handler)]() mutable
                   {
                       self->send(std::move(payload), std::move(handler));
                   });
    }

    // close() and cancel() end a parked read at once, on the caller's thread,
    // and do not wait for the strand. The parked handler holds the endpoint,
    // the endpoint holds this transport and this state holds the handler: if
    // the strand never ran again (the hub stops its io_context when every
    // endpoint has closed), the cycle would never be broken.
    void close()
    {
        open_.store(false);
        abort_pending_read();
        asio::post(strand_, [self = shared_from_this()] { self->shut_down(); });
    }

    void cancel()
    {
        abort_pending_read();
        // A read posted before this call has not reached the strand yet.
        asio::post(strand_,
                   [self = shared_from_this()] { self->abort_pending_read(); });
    }

    bool is_open() const noexcept { return open_.load(); }
    std::uint16_t port() const noexcept { return port_.load(); }
    std::size_t peers() const noexcept { return peers_.load(); }
    const listen_address& address() const noexcept { return address_; }

private:
    struct pending
    {
        asio::mutable_buffer buffer;
        io_handler handler;
    };

    struct client
    {
        explicit client(asio::ip::tcp::socket accepted)
            : socket(std::move(accepted))
        {
        }

        asio::ip::tcp::socket socket;
        std::array<char, chunk_size> buffer{};
        bool reading = false;
    };

    /// The parked read, removed from the state; empty when none is waiting.
    std::optional<pending> take_pending_read()
    {
        std::lock_guard lock(read_mutex_);
        std::optional<pending> taken = std::move(pending_read_);
        pending_read_.reset(); // a moved-from optional still holds a value
        return taken;
    }

    void abort_pending_read()
    {
        if (auto taken = take_pending_read())
            complete(std::move(taken->handler), asio::error::operation_aborted,
                     0);
    }

    void complete(io_handler handler, error_code ec, std::size_t count)
    {
        asio::post(executor_,
                   [handler = std::move(handler), ec, count]() mutable
                   { handler(ec, count); });
    }

    // -- TCP ---------------------------------------------------------------

    void accept_next()
    {
        acceptor_.async_accept(
            [self = shared_from_this()](error_code ec,
                                        asio::ip::tcp::socket socket)
            {
                if (ec)
                {
                    if (!self->closed_ && ec != asio::error::operation_aborted)
                        self->accept_next();
                    return;
                }
                auto made = std::make_shared<client>(std::move(socket));
                self->clients_.push_back(made);
                self->peers_.store(self->clients_.size());
                self->read_client(made);
                self->accept_next();
            });
    }

    void read_client(const std::shared_ptr<client>& peer)
    {
        if (closed_ || peer->reading)
            return;
        if (received_.size() >= receive_limit)
            return; // read_client runs again from deliver() once drained
        peer->reading = true;
        peer->socket.async_read_some(
            asio::buffer(peer->buffer),
            [self = shared_from_this(), peer](error_code ec, std::size_t count)
            {
                peer->reading = false;
                if (ec)
                {
                    self->drop(peer);
                    return;
                }
                self->received_.insert(self->received_.end(),
                                       peer->buffer.begin(),
                                       peer->buffer.begin() +
                                           static_cast<std::ptrdiff_t>(count));
                self->deliver();
                self->read_client(peer);
            });
    }

    void drop(const std::shared_ptr<client>& peer)
    {
        boost::system::error_code ignored;
        peer->socket.close(ignored);
        std::erase(clients_, peer);
        peers_.store(clients_.size());
    }

    // -- UDP ---------------------------------------------------------------

    void receive_next()
    {
        if (closed_)
            return;
        if (received_.size() >= receive_limit)
        {
            receiving_ = false;
            return;
        }
        receiving_ = true;
        datagrams_.async_receive_from(
            asio::buffer(datagram_buffer_), sender_,
            [self = shared_from_this()](error_code ec, std::size_t count)
            {
                self->receiving_ = false;
                if (ec == asio::error::operation_aborted || self->closed_)
                    return;
                if (!ec)
                {
                    self->remember(self->sender_);
                    self->received_.insert(
                        self->received_.end(), self->datagram_buffer_.begin(),
                        self->datagram_buffer_.begin() +
                            static_cast<std::ptrdiff_t>(count));
                    self->deliver();
                }
                self->receive_next();
            });
    }

    void remember(const asio::ip::udp::endpoint& sender)
    {
        std::erase(senders_, sender);
        senders_.push_front(sender);
        if (senders_.size() > udp_peer_limit)
            senders_.pop_back();
        peers_.store(senders_.size());
    }

    // -- shared ------------------------------------------------------------

    void deliver()
    {
        std::optional<pending> taken;
        {
            std::lock_guard lock(read_mutex_);
            if (pending_read_ && !received_.empty())
            {
                taken = std::move(pending_read_);
                pending_read_.reset();
            }
        }
        if (taken)
        {
            const std::size_t count =
                std::min(taken->buffer.size(), received_.size());
            std::copy_n(received_.begin(), count,
                        static_cast<char*>(taken->buffer.data()));
            received_.erase(received_.begin(),
                            received_.begin() +
                                static_cast<std::ptrdiff_t>(count));
            complete(std::move(taken->handler), {}, count);
        }
        if (received_.size() < receive_limit)
        {
            if (address_.protocol == listen_protocol::tcp)
            {
                const auto waiting = clients_;
                for (const auto& peer : waiting)
                    read_client(peer);
            }
            else if (!receiving_)
            {
                receive_next();
            }
        }
    }

    void send(std::shared_ptr<std::vector<char>> payload, io_handler handler)
    {
        const std::size_t size = payload->size();
        if (closed_)
        {
            complete(std::move(handler), asio::error::operation_aborted, 0);
            return;
        }

        if (address_.protocol == listen_protocol::tcp)
        {
            if (clients_.empty())
            {
                complete(std::move(handler), {}, size); // nobody to tell
                return;
            }
            auto remaining = std::make_shared<std::size_t>(clients_.size());
            auto shared_handler =
                std::make_shared<io_handler>(std::move(handler));
            const auto targets = clients_;
            for (const auto& peer : targets)
            {
                asio::async_write(
                    peer->socket, asio::buffer(*payload),
                    [self = shared_from_this(), peer, payload, remaining,
                     shared_handler, size](error_code ec, std::size_t)
                    {
                        if (ec)
                            self->drop(peer);
                        if (--*remaining == 0)
                            self->complete(std::move(*shared_handler), {},
                                           size);
                    });
            }
            return;
        }

        if (senders_.empty())
        {
            complete(std::move(handler), {}, size);
            return;
        }
        auto remaining = std::make_shared<std::size_t>(senders_.size());
        auto shared_handler = std::make_shared<io_handler>(std::move(handler));
        const auto targets = std::vector<asio::ip::udp::endpoint>(
            senders_.begin(), senders_.end());
        for (const auto& target : targets)
        {
            datagrams_.async_send_to(
                asio::buffer(*payload), target,
                [self = shared_from_this(), payload, remaining, shared_handler,
                 size](error_code, std::size_t)
                {
                    if (--*remaining == 0)
                        self->complete(std::move(*shared_handler), {}, size);
                });
        }
    }

    void shut_down()
    {
        if (closed_)
            return;
        closed_ = true;
        boost::system::error_code ignored;
        acceptor_.close(ignored);
        datagrams_.close(ignored);
        for (const auto& peer : clients_)
            peer->socket.close(ignored);
        clients_.clear();
        peers_.store(0);
        abort_pending_read();
    }

    executor_type executor_;
    asio::strand<executor_type> strand_;
    listen_address address_;
    asio::ip::tcp::acceptor acceptor_;
    asio::ip::udp::socket datagrams_;

    std::vector<std::shared_ptr<client>> clients_;
    std::deque<asio::ip::udp::endpoint> senders_;
    asio::ip::udp::endpoint sender_;
    std::array<char, 65536> datagram_buffer_{};
    bool receiving_ = false;

    std::deque<char> received_;
    /// Guarded by read_mutex_: close() and cancel() take it from any thread.
    std::mutex read_mutex_;
    std::optional<pending> pending_read_;
    bool closed_ = false;

    std::atomic<bool> open_{false};
    std::atomic<std::uint16_t> port_{0};
    std::atomic<std::size_t> peers_{0};
};

listen_transport::listen_transport(const executor_type& executor,
                                   listen_address address, error_code& ec)
    : state_(std::make_shared<state>(executor, std::move(address)))
{
    ec = state_->open();
    if (!ec)
        state_->start();
}

listen_transport::~listen_transport()
{
    state_->close();
}

std::string listen_transport::describe() const
{
    const auto& address = state_->address();
    const std::string host = address.host.empty() ? "0.0.0.0" : address.host;
    return std::string(address.protocol == listen_protocol::tcp ? "TCP"
                                                                : "UDP") +
           " listening on " + host + ":" + std::to_string(state_->port()) +
           " (" + std::to_string(state_->peers()) +
           (address.protocol == listen_protocol::tcp ? " connected)"
                                                     : " senders)");
}

std::string listen_transport::public_path() const
{
    auto address = state_->address();
    address.port = state_->port();
    return make_listen_uri(address);
}

bool listen_transport::is_open() const
{
    return state_->is_open();
}

void listen_transport::async_read_some(asio::mutable_buffer buffer,
                                       io_handler handler)
{
    state_->read(buffer, std::move(handler));
}

void listen_transport::async_write_some(asio::const_buffer buffer,
                                        io_handler handler)
{
    state_->write(buffer, std::move(handler));
}

void listen_transport::close()
{
    state_->close();
}

void listen_transport::cancel()
{
    state_->cancel();
}

std::uint16_t listen_transport::local_port() const noexcept
{
    return state_->port();
}

std::size_t listen_transport::peer_count() const noexcept
{
    return state_->peers();
}

} // namespace vss
