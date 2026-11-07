//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reopening a port from what the studio kept of it, the retry of a lost
// connection, and the ports of the Windows driver that a restart takes away.
//

#include "support/test_support.hpp"

#include <vss/studio/studio.hpp>

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/write.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <optional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace vss;
using namespace vss::testkit;

namespace
{
using namespace std::chrono_literals;
namespace asio = boost::asio;

/// A TCP server on loopback that can drop its client, stop listening and start
/// again on the same port.
class tcp_server
{
public:
    tcp_server()
    {
        listen(0);
        thread_ = std::thread([this] { io_.run(); });
    }

    ~tcp_server()
    {
        try
        {
            io_.stop();
        }
        catch (...)
        {
            // best effort: a destructor must not throw, and nothing can act on a failed stop
        }
        if (thread_.joinable())
            thread_.join();
    }

    std::uint16_t port() const { return port_; }

    std::size_t accepted() const { return accepted_.load(); }

    std::string received() const
    {
        std::lock_guard lock(mutex_);
        return received_;
    }

    /// Hangs up on the client, as a server that restarted would.
    void drop()
    {
        asio::post(io_, [this] {
            boost::system::error_code ignored;
            if (socket_)
                socket_->close(ignored);
            socket_.reset();
        });
    }

    /// Refuses new connections until #resume.
    void pause()
    {
        std::promise<void> done;
        asio::post(io_, [this, &done] {
            boost::system::error_code ignored;
            acceptor_->close(ignored);
            done.set_value();
        });
        done.get_future().wait();
    }

    void resume()
    {
        std::promise<void> done;
        asio::post(io_, [this, &done] {
            listen(port_);
            done.set_value();
        });
        done.get_future().wait();
    }

    void send(const std::string& text)
    {
        std::promise<void> done;
        asio::post(io_, [this, &text, &done] {
            boost::system::error_code ignored;
            if (socket_)
                asio::write(*socket_, asio::buffer(text), ignored);
            done.set_value();
        });
        done.get_future().wait();
    }

private:
    void listen(std::uint16_t port)
    {
        acceptor_ = std::make_unique<asio::ip::tcp::acceptor>(io_);
        boost::system::error_code error;
        acceptor_->open(asio::ip::tcp::v4(), error);
        REQUIRE(!error);
        acceptor_->set_option(asio::socket_base::reuse_address(true), error);
        // Loopback only: a bind to every address makes Windows Firewall ask.
        acceptor_->bind(asio::ip::tcp::endpoint(
                            asio::ip::address_v4::loopback(), port),
                        error);
        REQUIRE(!error);
        port_ = acceptor_->local_endpoint().port();
        acceptor_->listen(asio::socket_base::max_listen_connections, error);
        REQUIRE(!error);
        accept();
    }

    void accept()
    {
        acceptor_->async_accept(
            [this](boost::system::error_code error, asio::ip::tcp::socket socket) {
                if (error)
                    return;
                socket_ = std::move(socket);
                accepted_.fetch_add(1);
                read();
                accept();
            });
    }

    void read()
    {
        if (!socket_)
            return;
        socket_->async_read_some(
            asio::buffer(buffer_),
            [this](boost::system::error_code error, std::size_t count) {
                if (error)
                    return;
                {
                    std::lock_guard lock(mutex_);
                    received_.append(buffer_.data(), count);
                }
                read();
            });
    }

    mutable std::mutex mutex_;
    asio::io_context io_;
    // Keeps run() going while no accept or read is pending.
    asio::executor_work_guard<asio::io_context::executor_type> guard_{
        io_.get_executor()};
    std::unique_ptr<asio::ip::tcp::acceptor> acceptor_;
    std::optional<asio::ip::tcp::socket> socket_;
    std::thread thread_;
    std::array<char, 4096> buffer_{};
    std::uint16_t port_ = 0;
    std::atomic<std::size_t> accepted_{0};
    std::string received_;
};

std::string uri_of(const tcp_server& server)
{
    return "tcp://127.0.0.1:" + std::to_string(server.port());
}

bool wait_for_state(const std::shared_ptr<endpoint>& port, endpoint_state state,
                    std::chrono::milliseconds limit = 5000ms)
{
    return wait_until([&] { return port->state() == state; }, limit);
}

/// The log messages an endpoint has published, as text.
struct log_watch
{
    std::shared_ptr<std::mutex> mutex = std::make_shared<std::mutex>();
    std::shared_ptr<std::vector<std::string>> lines =
        std::make_shared<std::vector<std::string>>();
    studio& app;
    subscriber_id id = 0;

    explicit log_watch(studio& studio_app) : app(studio_app)
    {
        id = app.engine().subscribe(
            event_filter{std::nullopt, event_kind::log_message},
            [mutex = mutex, lines = lines](const endpoint_event& event) {
                std::lock_guard lock(*mutex);
                lines->push_back(event.message);
            });
    }

    ~log_watch() { app.engine().unsubscribe(id); }

    bool saw(const std::string& fragment) const
    {
        std::lock_guard lock(*mutex);
        for (const auto& line : *lines)
        {
            if (line.find(fragment) != std::string::npos)
                return true;
        }
        return false;
    }
};

reconnect_policy quick_policy(unsigned max_attempts = 0)
{
    reconnect_policy policy;
    policy.enabled = true;
    policy.first_delay = 40ms;
    policy.max_delay = 160ms;
    policy.max_attempts = max_attempts;
    return policy;
}

} // namespace

TEST_CASE("reconnect: the delay doubles up to the longest")
{
    reconnect_policy policy;
    policy.first_delay = 1000ms;
    policy.max_delay = 5000ms;
    REQUIRE_EQ(backoff_delay(policy, 0).count(), 1000);
    REQUIRE_EQ(backoff_delay(policy, 1).count(), 2000);
    REQUIRE_EQ(backoff_delay(policy, 2).count(), 4000);
    REQUIRE_EQ(backoff_delay(policy, 3).count(), 5000);
    REQUIRE_EQ(backoff_delay(policy, 40).count(), 5000);

    // Odd settings still give a wait the thread can use.
    policy.first_delay = 0ms;
    policy.max_delay = 0ms;
    REQUIRE_EQ(backoff_delay(policy, 0).count(), 1);
    REQUIRE_EQ(backoff_delay(policy, 9).count(), 1);
    policy.first_delay = 9000ms;
    policy.max_delay = 2000ms;
    REQUIRE_EQ(backoff_delay(policy, 0).count(), 2000);
}

TEST_CASE("reopen: a TCP client whose server hung up is opened again")
{
    tcp_server server;
    studio app;
    const auto port = app.attach_serial("remote", uri_of(server));
    REQUIRE(port);
    REQUIRE(wait_until([&] { return server.accepted() == 1; }, 5000ms));

    server.drop();
    REQUIRE(wait_for_state(*port, endpoint_state::closed));

    REQUIRE(app.reopen((*port)->id()));
    REQUIRE((*port)->is_open());
    REQUIRE(wait_until([&] { return server.accepted() == 2; }, 5000ms));

    // The same endpoint carries bytes both ways again.
    const auto written = (*port)->write_sync(as_bytes("again"), 3000ms);
    REQUIRE(written);
    REQUIRE(wait_until([&] { return server.received() == "again"; }, 5000ms));
    server.send("back");
    REQUIRE(wait_for_rx(**port, 4, 5000ms));
}

TEST_CASE("reopen: the links of the port stay")
{
    tcp_server server;
    studio app;
    const auto remote = app.attach_serial("remote", uri_of(server));
    REQUIRE(remote);
    const auto local = app.create_virtual_port("local", virtual_port_kind::loopback);
    REQUIRE(local);
    const auto link = app.connect("remote", "local");
    REQUIRE(link);
    // The server must hold the connection before it can hang up on it.
    REQUIRE(wait_until([&] { return server.accepted() == 1; }, 5000ms));

    server.drop();
    REQUIRE(wait_for_state(*remote, endpoint_state::closed));
    REQUIRE(app.reopen((*remote)->id()));
    // The client side connects first: until the server has accepted the new
    // connection, what it sends goes nowhere.
    REQUIRE(wait_until([&] { return server.accepted() == 2; }, 5000ms));

    // A link made before the loss still joins the two.
    REQUIRE_EQ(app.link_infos().size(), std::size_t{1});
    server.send("through");
    REQUIRE(wait_until(
        [&] { return (*local)->stats().bytes_tx >= 7; }, 5000ms));
}

TEST_CASE("reopen: a server that is still down reports why and the port stays down")
{
    tcp_server server;
    studio app;
    const auto port = app.attach_serial("remote", uri_of(server));
    REQUIRE(port);

    server.pause();
    server.drop();
    REQUIRE(wait_for_state(*port, endpoint_state::closed));

    const auto failed = app.reopen((*port)->id());
    REQUIRE(!failed);
    REQUIRE((*port)->state() == endpoint_state::closed);

    server.resume();
    REQUIRE(app.reopen((*port)->id()));
    REQUIRE((*port)->is_open());
}

TEST_CASE("reopen: what cannot be made again says so")
{
    studio app;
    REQUIRE_EQ(app.reopen(9999), make_error_code(errc::not_found));

    const auto loopback =
        app.create_virtual_port("loop", virtual_port_kind::loopback);
    REQUIRE(loopback);
    REQUIRE_EQ(app.reopen((*loopback)->id()),
               make_error_code(errc::unsupported));

    const auto fixture = app.create_mock_port("fixture");
    REQUIRE(fixture);
    REQUIRE_EQ(app.reopen((*fixture)->id()), make_error_code(errc::unsupported));
}

TEST_CASE("reopen: a listener binds its address again")
{
    studio app;
    const auto listener = app.attach_serial("wait", "tcp-listen://127.0.0.1:0");
    REQUIRE(listener);
    REQUIRE((*listener)->is_open());

    // Restarting a listener on port 0 gives it a fresh port; it stays open.
    REQUIRE(app.reopen((*listener)->id()));
    REQUIRE((*listener)->is_open());
}

TEST_CASE("reconnect: it is off unless asked for")
{
    REQUIRE(!reconnect_policy{}.enabled);

    tcp_server server;
    studio app;
    REQUIRE(!app.current_reconnect_policy().enabled);
    const auto port = app.attach_serial("remote", uri_of(server));
    REQUIRE(port);
    REQUIRE(wait_until([&] { return server.accepted() == 1; }, 5000ms));

    server.drop();
    REQUIRE(wait_for_state(*port, endpoint_state::closed));
    std::this_thread::sleep_for(400ms);
    REQUIRE((*port)->state() == endpoint_state::closed);
    REQUIRE_EQ(server.accepted(), std::size_t{1});
}

TEST_CASE("reconnect: a lost connection is opened again, and it says so")
{
    tcp_server server;
    studio app;
    log_watch watch(app);
    app.set_reconnect_policy(quick_policy());
    REQUIRE(app.current_reconnect_policy() == quick_policy());

    const auto port = app.attach_serial("remote", uri_of(server));
    REQUIRE(port);
    REQUIRE(wait_until([&] { return server.accepted() == 1; }, 5000ms));

    server.drop();
    REQUIRE(wait_until([&] { return server.accepted() == 2; }, 5000ms));
    REQUIRE(wait_for_state(*port, endpoint_state::open));
    REQUIRE((*port)->is_open());
    REQUIRE(wait_until([&] { return watch.saw("connection lost"); }, 5000ms));
    REQUIRE(wait_until([&] { return watch.saw("reconnected"); }, 5000ms));

    // And again: every loss starts a fresh sequence.
    server.drop();
    REQUIRE(wait_until([&] { return server.accepted() == 3; }, 5000ms));
    const auto written = (*port)->write_sync(as_bytes("alive"), 3000ms);
    REQUIRE(written);
    REQUIRE(wait_until([&] { return server.received() == "alive"; }, 5000ms));
}

TEST_CASE("reconnect: it keeps trying while the server is down")
{
    tcp_server server;
    studio app;
    log_watch watch(app);
    app.set_reconnect_policy(quick_policy());
    const auto port = app.attach_serial("remote", uri_of(server));
    REQUIRE(port);
    REQUIRE(wait_until([&] { return server.accepted() == 1; }, 5000ms));

    server.pause();
    server.drop();
    REQUIRE(wait_until([&] { return watch.saw("could not reconnect"); }, 5000ms));
    REQUIRE((*port)->state() == endpoint_state::closed);

    server.resume();
    REQUIRE(wait_until([&] { return server.accepted() == 2; }, 5000ms));
    REQUIRE(wait_for_state(*port, endpoint_state::open));
}

TEST_CASE("reconnect: it gives up after the most tries asked for")
{
    tcp_server server;
    studio app;
    log_watch watch(app);
    app.set_reconnect_policy(quick_policy(2));
    const auto port = app.attach_serial("remote", uri_of(server));
    REQUIRE(port);
    REQUIRE(wait_until([&] { return server.accepted() == 1; }, 5000ms));

    server.pause();
    server.drop();
    REQUIRE(wait_until([&] { return watch.saw("giving up after 2 tries"); },
                       5000ms));

    // Gave up: the server coming back changes nothing until it is asked.
    server.resume();
    std::this_thread::sleep_for(400ms);
    REQUIRE((*port)->state() == endpoint_state::closed);
    REQUIRE(app.reopen((*port)->id()));
}

TEST_CASE("reconnect: removing the port ends its tries")
{
    tcp_server server;
    studio app;
    app.set_reconnect_policy(quick_policy());
    const auto port = app.attach_serial("remote", uri_of(server));
    REQUIRE(port);
    REQUIRE(wait_until([&] { return server.accepted() == 1; }, 5000ms));

    server.pause();
    server.drop();
    REQUIRE(wait_for_state(*port, endpoint_state::closed));
    REQUIRE(app.remove_port((*port)->id()));

    server.resume();
    std::this_thread::sleep_for(400ms);
    REQUIRE_EQ(server.accepted(), std::size_t{1});
    REQUIRE(app.endpoint_infos().empty());
}

TEST_CASE("reconnect: only a client that connects out is retried")
{
    studio app;
    log_watch watch(app);
    app.set_reconnect_policy(quick_policy());

    // A listener and an in-process port that stop are not retried.
    const auto listener = app.attach_serial("wait", "tcp-listen://127.0.0.1:0");
    REQUIRE(listener);
    const auto loopback =
        app.create_virtual_port("loop", virtual_port_kind::loopback);
    REQUIRE(loopback);
    (*listener)->close();
    (*loopback)->close();
    REQUIRE(wait_for_state(*listener, endpoint_state::closed));
    std::this_thread::sleep_for(300ms);
    REQUIRE(!watch.saw("connection lost"));
    REQUIRE((*listener)->state() == endpoint_state::closed);
}

TEST_CASE("reconnect: turning the policy off stops the tries that are waiting")
{
    tcp_server server;
    studio app;
    reconnect_policy slow = quick_policy();
    slow.first_delay = 300ms;
    slow.max_delay = 300ms;
    app.set_reconnect_policy(slow);
    const auto port = app.attach_serial("remote", uri_of(server));
    REQUIRE(port);
    REQUIRE(wait_until([&] { return server.accepted() == 1; }, 5000ms));

    server.drop();
    REQUIRE(wait_for_state(*port, endpoint_state::closed));
    app.set_reconnect_policy(reconnect_policy{});
    std::this_thread::sleep_for(700ms);
    REQUIRE_EQ(server.accepted(), std::size_t{1});
    REQUIRE((*port)->state() == endpoint_state::closed);
}

TEST_CASE("lost ports: a studio without the Windows driver has none")
{
    studio app;
    const auto loopback =
        app.create_virtual_port("loop", virtual_port_kind::loopback);
    REQUIRE(loopback);
    REQUIRE(app.lost_ports().empty());
    const auto report = app.recreate_lost_ports();
    REQUIRE(report.recreated.empty());
    REQUIRE(report.failed.empty());
}
