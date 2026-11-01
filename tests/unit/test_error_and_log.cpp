//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <vss/core/error.hpp>
#include <vss/core/traffic_log.hpp>

#include "test_framework.hpp"

using namespace vss;

TEST_CASE("error: category and messages")
{
    REQUIRE_EQ(std::string(error_category().name()), std::string("vss"));

    REQUIRE_EQ(make_error_code(errc::success).message(), std::string("success"));
    REQUIRE_EQ(make_error_code(errc::not_found).message(), std::string("not found"));
    REQUIRE_EQ(make_error_code(errc::already_exists).message(),
               std::string("already exists"));
    REQUIRE_EQ(make_error_code(errc::invalid_argument).message(),
               std::string("invalid argument"));
    REQUIRE_EQ(make_error_code(errc::invalid_uri).message(),
               std::string("invalid URI"));
    REQUIRE_EQ(make_error_code(errc::unsupported).message(),
               std::string("unsupported by this transport"));
    REQUIRE_EQ(make_error_code(errc::not_open).message(),
               std::string("endpoint is not open"));
    REQUIRE_EQ(make_error_code(errc::already_open).message(),
               std::string("endpoint is already open"));
    REQUIRE_EQ(make_error_code(errc::cycle_detected).message(),
               std::string("link would create a cycle"));
    REQUIRE_EQ(make_error_code(errc::queue_full).message(),
               std::string("write queue is full"));
    REQUIRE_EQ(make_error_code(errc::timeout).message(),
               std::string("operation timed out"));
    REQUIRE_EQ(make_error_code(errc::canceled).message(),
               std::string("operation canceled"));
    REQUIRE_EQ(make_error_code(errc::io_error).message(), std::string("I/O error"));
    REQUIRE_EQ(make_error_code(errc::permission_denied).message(),
               std::string("permission denied"));
    REQUIRE_EQ(make_error_code(errc::config_error).message(),
               std::string("configuration error"));
    REQUIRE_EQ(make_error_code(errc::device_busy).message(),
               std::string("device is busy"));
    REQUIRE_EQ(make_error_code(errc::port_exhausted).message(),
               std::string("no ports of this kind are available"));
    REQUIRE_EQ(make_error_code(errc::internal).message(),
               std::string("internal error"));
    REQUIRE_EQ(make_error_code(static_cast<errc>(999)).message(),
               std::string("unknown vss error"));
}

TEST_CASE("error: implicit conversion and comparison")
{
    const error_code code = errc::timeout;
    REQUIRE_EQ(code.value(), static_cast<int>(errc::timeout));
    REQUIRE_EQ(code, make_error_code(errc::timeout));
    REQUIRE(code != make_error_code(errc::canceled));
}

TEST_CASE("error: result carries values and errors")
{
    const result<int> good = 42;
    REQUIRE(good);
    REQUIRE_EQ(*good, 42);

    const result<int> bad = make_error_code(errc::not_found);
    REQUIRE(!bad);
    REQUIRE_EQ(bad.error(), make_error_code(errc::not_found));

    const result<void> empty;
    REQUIRE(empty);

    const result<void> failed = make_error_code(errc::unsupported);
    REQUIRE(!failed);
}

TEST_CASE("traffic log: ordering, capacity and limits")
{
    traffic_log log(3);
    REQUIRE_EQ(log.capacity(), std::size_t{3});
    REQUIRE_EQ(log.size(), std::size_t{0});

    for (int index = 0; index < 5; ++index)
    {
        traffic_record record;
        record.name = std::to_string(index);
        record.endpoint = static_cast<endpoint_id>(index);
        log.push(std::move(record));
    }

    REQUIRE_EQ(log.size(), std::size_t{3});

    const auto snapshot = log.snapshot();
    REQUIRE_EQ(snapshot.size(), std::size_t{3});
    REQUIRE_EQ(snapshot.front().name, std::string("2"));
    REQUIRE_EQ(snapshot.back().name, std::string("4"));

    const auto limited = log.snapshot(2);
    REQUIRE_EQ(limited.size(), std::size_t{2});
    REQUIRE_EQ(limited.front().name, std::string("3"));
    REQUIRE_EQ(limited.back().name, std::string("4"));

    log.set_capacity(1);
    REQUIRE_EQ(log.capacity(), std::size_t{1});
    REQUIRE_EQ(log.size(), std::size_t{1});

    log.clear();
    REQUIRE_EQ(log.size(), std::size_t{0});
}

TEST_CASE("traffic log: zero capacity is normalised")
{
    traffic_log log(0);
    REQUIRE_EQ(log.capacity(), std::size_t{1});

    traffic_record record;
    log.push(record);
    REQUIRE_EQ(log.size(), std::size_t{1});

    log.set_capacity(0);
    REQUIRE_EQ(log.capacity(), std::size_t{1});
}
