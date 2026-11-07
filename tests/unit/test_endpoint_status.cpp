//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "support/test_support.hpp"

#include <vss/core/hub.hpp>
#include <vss/studio/format_check.hpp>

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

using namespace vss;
using namespace vss_test_support;

namespace
{

/// A transport that never completes a read and reports what the test tells it
/// to, the way a driver port or a listener would.
class reporting_transport final : public vss::transport
{
public:
    std::string describe() const override { return "reporting transport"; }
    bool is_open() const override { return open_.load(); }

    void async_read_some(boost::asio::mutable_buffer, io_handler) override {}
    void async_write_some(boost::asio::const_buffer, io_handler) override {}
    void close() override { open_ = false; }

    std::optional<line_format> peer_format() const override
    {
        std::lock_guard lock(mutex_);
        return format_;
    }

    std::optional<std::size_t> client_count() const noexcept override
    {
        std::lock_guard lock(mutex_);
        return clients_;
    }

    void set_format(std::optional<line_format> format)
    {
        std::lock_guard lock(mutex_);
        format_ = format;
    }

    void set_clients(std::optional<std::size_t> clients)
    {
        std::lock_guard lock(mutex_);
        clients_ = clients;
    }

private:
    mutable std::mutex mutex_;
    std::optional<line_format> format_;
    std::optional<std::size_t> clients_;
    std::atomic<bool> open_{true};
};

line_format format(unsigned baud, unsigned bits = 8,
                   parity_mode parity = parity_mode::none,
                   stop_bits_mode stop = stop_bits_mode::one)
{
    return {baud, bits, parity, stop};
}

endpoint_info info_of(endpoint_id id, endpoint_kind kind,
                      std::optional<line_format> peer = std::nullopt,
                      unsigned configured_baud = 115200)
{
    endpoint_info info;
    info.id = id;
    info.kind = kind;
    info.peer_format = peer;
    info.serial.baud_rate = configured_baud;
    return info;
}

link_info link_of(link_id id, std::vector<endpoint_id> sources,
                  std::vector<endpoint_id> targets = {},
                  link_mode mode = link_mode::duplex)
{
    link_info link;
    link.id = id;
    link.mode = mode;
    link.sources = std::move(sources);
    link.targets = std::move(targets);
    return link;
}

} // namespace

TEST_CASE("endpoint info: a transport that cannot tell reports nothing")
{
    auto hub = start_hub();
    auto loopback = add_loopback(*hub, "plain");
    const auto info = loopback->info();
    REQUIRE(!info.peer_format.has_value());
    REQUIRE(!info.clients.has_value());
}

TEST_CASE("endpoint info: the format and client count follow the transport")
{
    auto hub = start_hub();
    auto transport = std::make_shared<reporting_transport>();
    const auto added =
        hub->add_endpoint("reporting", endpoint_kind::virtual_port, transport);
    REQUIRE(added);

    auto info = (*added)->info();
    REQUIRE(!info.peer_format.has_value());
    REQUIRE(!info.clients.has_value());

    transport->set_format(format(19200, 7, parity_mode::even,
                                 stop_bits_mode::two));
    transport->set_clients(0);
    info = (*added)->info();
    REQUIRE(info.peer_format.has_value());
    REQUIRE(*info.peer_format ==
            format(19200, 7, parity_mode::even, stop_bits_mode::two));
    // Zero is an answer: a listener with nobody connected.
    REQUIRE(info.clients.has_value());
    REQUIRE_EQ(*info.clients, std::size_t{0});

    transport->set_clients(3);
    REQUIRE_EQ(*(*added)->info().clients, std::size_t{3});

    transport->set_format(std::nullopt);
    REQUIRE(!(*added)->info().peer_format.has_value());
}

TEST_CASE("line format: labels and the framing of serial settings")
{
    REQUIRE_EQ(to_label(format(9600)), std::string("9600 8N1"));
    REQUIRE_EQ(to_label(format(19200, 7, parity_mode::even, stop_bits_mode::two)),
               std::string("19200 7E2"));

    serial_settings settings;
    settings.baud_rate = 4800;
    settings.data_bits = 7;
    settings.parity = parity_mode::odd;
    settings.flow_control = flow_control_mode::hardware;
    REQUIRE(format_of(settings) == format(4800, 7, parity_mode::odd));
    // Settings and format give one label, so the two never disagree.
    REQUIRE_EQ(to_label(settings), to_label(format_of(settings)));
}

TEST_CASE("format check: what each kind of endpoint runs at")
{
    // A program's choice wins over everything.
    REQUIRE(vss_test::require_value(running_format(info_of(
                1, endpoint_kind::virtual_port, format(9600)))) ==
            format(9600));
    // A real port and an RFC 2217 client run at their configured settings.
    REQUIRE(vss_test::require_value(running_format(info_of(
                2, endpoint_kind::physical, std::nullopt, 57600))) ==
            format(57600));
    REQUIRE(vss_test::require_value(running_format(info_of(
                3, endpoint_kind::rfc2217, std::nullopt, 2400))) ==
            format(2400));
    // Everything else has no format to compare.
    REQUIRE(!running_format(info_of(4, endpoint_kind::virtual_port)).has_value());
    REQUIRE(!running_format(info_of(5, endpoint_kind::tcp)).has_value());
    REQUIRE(!running_format(info_of(6, endpoint_kind::udp)).has_value());
    REQUIRE(!running_format(info_of(7, endpoint_kind::mock)).has_value());
}

TEST_CASE("format check: members at different speeds are reported")
{
    const std::vector<endpoint_info> endpoints{
        info_of(1, endpoint_kind::virtual_port, format(9600)),
        info_of(2, endpoint_kind::virtual_port, format(19200)),
        info_of(3, endpoint_kind::virtual_port, format(9600))};

    const auto found =
        find_format_mismatches(endpoints, {link_of(10, {1}, {2})});
    REQUIRE_EQ(found.size(), std::size_t{1});
    REQUIRE_EQ(found[0].link, link_id{10});
    REQUIRE_EQ(found[0].members.size(), std::size_t{2});
    REQUIRE_EQ(found[0].members[0].endpoint, endpoint_id{1});
    REQUIRE(found[0].members[0].format == format(9600));
    REQUIRE_EQ(found[0].members[1].endpoint, endpoint_id{2});
    REQUIRE(found[0].members[1].format == format(19200));

    // Two that agree are quiet.
    REQUIRE(find_format_mismatches(endpoints, {link_of(11, {1}, {3})}).empty());
}

TEST_CASE("format check: framing counts as well as the baud rate")
{
    const auto check = [](line_format left, line_format right)
    {
        const std::vector<endpoint_info> endpoints{
            info_of(1, endpoint_kind::virtual_port, left),
            info_of(2, endpoint_kind::virtual_port, right)};
        return find_format_mismatches(endpoints, {link_of(1, {1}, {2})}).size();
    };

    REQUIRE_EQ(check(format(9600), format(9600)), std::size_t{0});
    REQUIRE_EQ(check(format(9600, 8), format(9600, 7)), std::size_t{1});
    REQUIRE_EQ(check(format(9600, 8, parity_mode::none),
                     format(9600, 8, parity_mode::even)),
               std::size_t{1});
    REQUIRE_EQ(check(format(9600, 8, parity_mode::none, stop_bits_mode::one),
                     format(9600, 8, parity_mode::none, stop_bits_mode::two)),
               std::size_t{1});
}

TEST_CASE("format check: a member whose format is unknown is not a mismatch")
{
    const std::vector<endpoint_info> endpoints{
        info_of(1, endpoint_kind::virtual_port, format(9600)),
        info_of(2, endpoint_kind::virtual_port),
        info_of(3, endpoint_kind::tcp)};

    // Not knowing is not a difference.
    REQUIRE(find_format_mismatches(endpoints, {link_of(1, {1}, {2})}).empty());
    REQUIRE(find_format_mismatches(endpoints, {link_of(2, {1}, {3})}).empty());
    REQUIRE(find_format_mismatches(endpoints, {link_of(3, {2}, {3})}).empty());
    // A link to an endpoint that is gone is not an error here either.
    REQUIRE(find_format_mismatches(endpoints, {link_of(4, {1}, {99})}).empty());
}

TEST_CASE("format check: a real port is held to the program on its link")
{
    // The device is configured for 9600; the program opens its end at 19200.
    const std::vector<endpoint_info> endpoints{
        info_of(1, endpoint_kind::physical, std::nullopt, 9600),
        info_of(2, endpoint_kind::virtual_port, format(19200))};
    const auto found =
        find_format_mismatches(endpoints, {link_of(1, {1}, {2})});
    REQUIRE_EQ(found.size(), std::size_t{1});
    REQUIRE(found[0].members[0].format == format(9600));

    // Once the program uses the device's settings the warning goes.
    const std::vector<endpoint_info> agreed{
        info_of(1, endpoint_kind::physical, std::nullopt, 9600),
        info_of(2, endpoint_kind::virtual_port, format(9600))};
    REQUIRE(find_format_mismatches(agreed, {link_of(1, {1}, {2})}).empty());
}

TEST_CASE("format check: a bus compares all its members, and a tap counts too")
{
    const std::vector<endpoint_info> endpoints{
        info_of(1, endpoint_kind::virtual_port, format(9600)),
        info_of(2, endpoint_kind::virtual_port, format(9600)),
        info_of(3, endpoint_kind::virtual_port, format(9600)),
        info_of(4, endpoint_kind::virtual_port, format(4800))};

    // One member out of three at another speed.
    std::vector<endpoint_info> mixed = endpoints;
    mixed[2].peer_format = format(19200);
    const auto bus = find_format_mismatches(
        mixed, {link_of(1, {1, 2, 3}, {}, link_mode::bus)});
    REQUIRE_EQ(bus.size(), std::size_t{1});
    REQUIRE_EQ(bus[0].members.size(), std::size_t{3});

    // All three agree.
    REQUIRE(find_format_mismatches(
                endpoints, {link_of(1, {1, 2, 3}, {}, link_mode::bus)})
                .empty());

    // A tap that joins the members but listens at another speed.
    const auto tap = find_format_mismatches(
        endpoints, {link_of(2, {1, 2, 3}, {4}, link_mode::one_way)});
    REQUIRE_EQ(tap.size(), std::size_t{1});
    REQUIRE_EQ(tap[0].members.size(), std::size_t{4});
}

TEST_CASE("format check: only the links that differ are listed")
{
    const std::vector<endpoint_info> endpoints{
        info_of(1, endpoint_kind::virtual_port, format(9600)),
        info_of(2, endpoint_kind::virtual_port, format(9600)),
        info_of(3, endpoint_kind::virtual_port, format(1200))};
    const auto found = find_format_mismatches(
        endpoints, {link_of(1, {1}, {2}), link_of(2, {2}, {3}),
                    link_of(3, {1}, {3})});
    REQUIRE_EQ(found.size(), std::size_t{2});
    REQUIRE_EQ(found[0].link, link_id{2});
    REQUIRE_EQ(found[1].link, link_id{3});
}

TEST_CASE("line format: a side that knows only its baud rate agrees on framing")
{
    line_format baud_only = format(9600);
    baud_only.framing_known = false;
    baud_only.data_bits = 8;

    // The label is the rate alone, so nothing is claimed about the framing.
    REQUIRE_EQ(to_label(baud_only), std::string("9600"));

    // Same rate: not a difference, whatever the other side's framing.
    REQUIRE(same_format(baud_only, format(9600, 7, parity_mode::even)));
    REQUIRE(same_format(format(9600, 7, parity_mode::even), baud_only));
    // A different rate is a difference whether or not the framing is known.
    REQUIRE(!same_format(baud_only, format(19200)));
    // Two full formats compare on all four.
    REQUIRE(same_format(format(9600), format(9600)));
    REQUIRE(!same_format(format(9600, 8), format(9600, 7)));
}

TEST_CASE("format check: a member that knows only its baud rate")
{
    line_format baud_only = format(9600);
    baud_only.framing_known = false;

    const auto only = [&](line_format other)
    {
        const std::vector<endpoint_info> endpoints{
            info_of(1, endpoint_kind::virtual_port, baud_only),
            info_of(2, endpoint_kind::virtual_port, other)};
        return find_format_mismatches(endpoints, {link_of(1, {1}, {2})}).size();
    };
    REQUIRE_EQ(only(format(9600, 7, parity_mode::even)), std::size_t{0});
    REQUIRE_EQ(only(format(19200)), std::size_t{1});

    // Two full formats that differ in framing still differ when a third member
    // knows only its rate: every pair is compared, not just the first.
    const std::vector<endpoint_info> three{
        info_of(1, endpoint_kind::virtual_port, baud_only),
        info_of(2, endpoint_kind::virtual_port, format(9600, 8)),
        info_of(3, endpoint_kind::virtual_port, format(9600, 7))};
    REQUIRE_EQ(find_format_mismatches(three, {link_of(1, {1, 2, 3}, {},
                                                      link_mode::bus)})
                   .size(),
               std::size_t{1});
}
