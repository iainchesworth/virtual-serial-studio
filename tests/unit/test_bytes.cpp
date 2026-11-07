//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <vss/core/bytes.hpp>

#include "test_framework.hpp"

using namespace vss;

TEST_CASE("bytes: text roundtrip keeps embedded NUL")
{
    const std::string text("AT\x00OK", 5);
    const byte_buffer data = to_bytes(text);
    REQUIRE_EQ(data.size(), std::size_t{5});
    REQUIRE_EQ(to_string(bytes_view(data)), text);
}

TEST_CASE("bytes: hex dump separators")
{
    const byte_buffer data{0x00, 0x0f, 0xa5, 0xff};
    REQUIRE_EQ(to_hex(bytes_view(data)), std::string("00 0f a5 ff"));
    REQUIRE_EQ(to_hex(bytes_view(data), ':'), std::string("00:0f:a5:ff"));
    REQUIRE_EQ(to_hex(bytes_view(data), '\0'), std::string("000fa5ff"));
    REQUIRE_EQ(to_hex(bytes_view{}), std::string());
}

TEST_CASE("bytes: from_hex accepts separators and 0x")
{
    const auto plain = from_hex("48656c6c6f");
    REQUIRE(plain);
    REQUIRE_EQ(to_string(bytes_view(*plain)), std::string("Hello"));

    for (const char* text : {"48 65 6c", "48:65:6c", "48,65,6c", "0x48656c",
                             " 48-65_6c "})
    {
        const auto parsed = from_hex(text);
        REQUIRE(parsed);
        REQUIRE_EQ(to_string(bytes_view(*parsed)), std::string("Hel"));
    }

    REQUIRE_EQ(from_hex("").value().size(), std::size_t{0});
}

TEST_CASE("bytes: from_hex rejects malformed input")
{
    for (const char* text : {"4", "48 6", "zz", "0x", "48 6g"})
    {
        const auto parsed = from_hex(text);
        REQUIRE(!parsed);
        REQUIRE_EQ(parsed.error(), make_error_code(errc::invalid_argument));
    }
}

TEST_CASE("bytes: printable rendering escapes control bytes")
{
    const std::string text("A\r\n\tB\x01\xff");
    const std::string out = to_printable(as_bytes(text));
    REQUIRE_EQ(out, std::string("A\\r\\n\\tB.\xff"));
}

TEST_CASE("bytes: parse_byte_size understands binary multiples")
{
    REQUIRE_EQ(*parse_byte_size("0"), std::uint64_t{0});
    REQUIRE_EQ(*parse_byte_size("64"), std::uint64_t{64});
    REQUIRE_EQ(*parse_byte_size("1KB"), std::uint64_t{1024});
    REQUIRE_EQ(*parse_byte_size("1 kb"), std::uint64_t{1024});
    REQUIRE_EQ(*parse_byte_size("10MB"), std::uint64_t{10} * 1024 * 1024);
    REQUIRE_EQ(*parse_byte_size("1.5MiB"), std::uint64_t{1536} * 1024);
    REQUIRE_EQ(*parse_byte_size("2GiB"), std::uint64_t{2ull * 1024 * 1024 * 1024});
}

TEST_CASE("bytes: parse_byte_size rejects nonsense")
{
    for (const char* text : {"", "  ", "abc", "10XB", "-5", "1.2.3MB", "MB"})
    {
        const auto parsed = parse_byte_size(text);
        REQUIRE(!parsed);
    }
}

TEST_CASE("bytes: format_byte_size picks a unit")
{
    REQUIRE_EQ(format_byte_size(0), std::string("0 B"));
    REQUIRE_EQ(format_byte_size(512), std::string("512 B"));
    REQUIRE_EQ(format_byte_size(1024), std::string("1.0 KB"));
    REQUIRE_EQ(format_byte_size(1536ULL * 1024), std::string("1.5 MB"));
    REQUIRE_EQ(format_byte_size(3ull * 1024 * 1024 * 1024), std::string("3.0 GB"));
}

TEST_CASE("bytes: format_byte_size reaches terabytes")
{
    constexpr std::uint64_t terabyte = 1024ull * 1024ull * 1024ull * 1024ull;
    REQUIRE_EQ(format_byte_size(2 * terabyte), std::string("2.0 TB"));
    REQUIRE_EQ(format_byte_size(1024 * terabyte), std::string("1024.0 TB"));
}

TEST_CASE("bytes: printing an empty buffer")
{
    REQUIRE_EQ(to_printable(bytes_view{}), std::string());
    REQUIRE_EQ(to_hex(bytes_view{}, '\0'), std::string());
    REQUIRE_EQ(to_string(bytes_view{}), std::string());
}
