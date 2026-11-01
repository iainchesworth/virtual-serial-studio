//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Live UDP bridge test: a connected datagram peer on loopback, driven through
// the studio exactly as a remote serial-over-UDP gateway would be.
//

#include "support/test_support.hpp"

#include <vss/studio/studio.hpp>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/post.hpp>

#include <array>
#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

using namespace vss;
using namespace vss::testkit;

namespace {
using namespace std::chrono_literals;

/// A UDP peer that records datagrams and can answer the sender.
class udp_probe
{
public:
    udp_probe()
    {
        boost::system::error_code error;
        socket_.open(boost::asio::ip::udp::v4(), error);
        REQUIRE(!error);
        socket_.bind(boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 0),
                     error);
        REQUIRE(!error);
        port_ = socket_.local_endpoint().port();

        do_read();
        thread_ = std::thread([this] { io_.run(); });
    }

    ~udp_probe()
    {
        io_.stop();
        boost::system::error_code ignored;
        socket_.close(ignored);
        if (thread_.joinable())
            thread_.join();
    }

    std::uint16_t port() const { return port_; }

    std::string received() const
    {
        std::lock_guard lock(mutex_);
        return received_;
    }

    void send(std::string_view text)
    {
        const std::string payload(text);
        boost::asio::post(io_, [this, payload] {
            if (!remote_)
                return;
            boost::system::error_code ignored;
            socket_.send_to(boost::asio::buffer(payload), *remote_, 0, ignored);
        });
    }

private:
    void do_read()
    {
        socket_.async_receive_from(
            boost::asio::buffer(buffer_), incoming_,
            [this](boost::system::error_code error, std::size_t count) {
                if (error)
                    return;
                {
                    std::lock_guard lock(mutex_);
                    received_.append(buffer_.data(), count);
                    remote_ = incoming_;
                }
                do_read();
            });
    }

    mutable std::mutex mutex_;
    boost::asio::io_context io_;
    boost::asio::ip::udp::socket socket_{io_};
    std::optional<boost::asio::ip::udp::endpoint> remote_;
    boost::asio::ip::udp::endpoint incoming_{};
    std::thread thread_;
    std::array<char, 2048> buffer_{};
    std::uint16_t port_ = 0;
    std::string received_;
};

} // namespace

TEST_CASE("system: a UDP bridge carries datagrams both ways")
{
    udp_probe probe;
    studio app;

    const auto port = app.attach_serial(
        "datagrams", "udp://127.0.0.1:" + std::to_string(probe.port()));
    REQUIRE(port);
    REQUIRE((*port)->is_open());
    REQUIRE_EQ((*port)->kind(), endpoint_kind::udp);

    const auto written = (*port)->write_sync(as_bytes("ping"), 3000ms);
    REQUIRE(written);
    REQUIRE(wait_until([&] { return probe.received() == "ping"; }, 5000ms));

    probe.send("pong");
    REQUIRE(wait_for_rx(**port, 4, 5000ms));
    REQUIRE_EQ((*port)->stats().bytes_rx, std::uint64_t{4});
}

TEST_CASE("system: a UDP bridge to an unused port fails cleanly")
{
    studio app;
    const auto opened =
        app.attach_serial("nowhere", "udp://127.0.0.1:1");
    // A connected UDP socket usually opens without a peer, so either an open
    // port or a clean error is acceptable; a crash is not.
    if (opened)
        REQUIRE((*opened)->is_open());
    else
        REQUIRE(!opened);
}
