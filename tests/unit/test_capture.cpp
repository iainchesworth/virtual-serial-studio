//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "support/test_support.hpp"

#include "scratch_directory.hpp"

#include <vss/studio/capture.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

using namespace vss;
using namespace vss::testkit;
using namespace vss_test_support;
namespace fs = std::filesystem;

namespace {

using namespace std::chrono_literals;

/// 2026-06-08T12:00:00Z, the start time of the fixture.
std::chrono::system_clock::time_point fixture_start()
{
    // 20,612 days after 1970-01-01, plus noon.
    return std::chrono::system_clock::time_point(
        std::chrono::seconds(std::int64_t{20'612} * 86'400 + 12 * 3600));
}

capture_header fixture_header()
{
    capture_header header;
    header.started = fixture_start();
    header.local_time = false;
    return header;
}

capture_record make_record(std::int64_t offset, data_direction direction,
                           const std::string& port, byte_buffer data)
{
    capture_record record;
    record.offset_ms = offset;
    record.direction = direction;
    record.port = port;
    record.data = std::move(data);
    return record;
}

std::string read_text(const fs::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    std::ostringstream text;
    text << stream.rdbuf();
    return text.str();
}

std::vector<capture_record> fixture_records()
{
    return {
        make_record(142, data_direction::rx, "",
                    {0x10, 0x02, 0x50, 0x14, 0x76, 0x10, 0x03}),
        make_record(310, data_direction::tx, "",
                    {0x10, 0x02, 0x50, 0x11, 0x28, 0x9b, 0x10, 0x03}),
        make_record(512, data_direction::rx, "",
                    {0x10, 0x02, 0x00, 0x16, 0x20, 0x00, 0x48, 0x10, 0x03}),
        make_record(1'234'567, data_direction::tx, "", {0x00, 0xff}),
    };
}

/// Waits for a session to have written `count` records.
bool wait_for_records(const capture_session& session, std::uint64_t count)
{
    return wait_until([&] { return session.records() >= count; }, 2000ms);
}

} // namespace

// -- the file format ---------------------------------------------------------------

TEST_CASE("capture: a .cap file matches Automate's format byte for byte")
{
    // tests/data/automate_sample.cap is the example from Automate's record and
    // replay notes (docs/RECORD_REPLAY.md, "Capture file format") with the
    // header time filled in: ten-column right-aligned millisecond offsets,
    // one direction letter, lower-case 0x## bytes joined by '|', LF endings.
    std::ostringstream out;
    {
        capture_writer writer(out, capture_format::cap, fixture_header());
        for (const auto& record : fixture_records())
            writer.write(record);
        writer.finish();
        writer.finish(); // the closing line is written once
    }

    const auto expected =
        read_text(fs::path(VSS_TEST_DATA_DIR) / "automate_sample.cap");
    REQUIRE(!expected.empty());
    REQUIRE_EQ(out.str(), expected);
}

TEST_CASE("capture: the header time is the local time unless asked for UTC")
{
    std::ostringstream utc;
    capture_writer writer(utc, capture_format::cap, fixture_header());
    REQUIRE(utc.str().rfind("# Serial recording started at: Mon Jun  8 "
                            "12:00:00 2026\n", 0) == 0);

    capture_header local;
    local.started = fixture_start();
    std::ostringstream text;
    capture_writer local_writer(text, capture_format::cap, local);
    // Whatever the zone, it is the same layout as ctime().
    const auto line = text.str().substr(0, text.str().find('\n'));
    REQUIRE(line.rfind("# Serial recording started at: ", 0) == 0);
    REQUIRE_EQ(line.size(), std::string("# Serial recording started at: Mon "
                                        "Jun  8 12:00:00 2026").size());
}

TEST_CASE("capture: CSV carries the port, direction, all bytes and the time")
{
    std::ostringstream out;
    {
        capture_writer writer(out, capture_format::csv, fixture_header());
        writer.write(make_record(142, data_direction::rx, "master",
                                 {0x10, 0x02, 0x7f}));
        writer.write(make_record(310, data_direction::tx, "tap, \"1\"",
                                 {0x00}));
        writer.finish();
    }

    REQUIRE_EQ(out.str(),
               "time_utc,offset_ms,port,direction,bytes,hex\n"
               "2026-06-08T12:00:00.142Z,142,master,R,3,10 02 7f\n"
               "2026-06-08T12:00:00.310Z,310,\"tap, \"\"1\"\"\",W,1,00\n");
}

TEST_CASE("capture: JSON Lines carries a start line and one object per chunk")
{
    std::ostringstream out;
    {
        capture_writer writer(out, capture_format::jsonl, fixture_header());
        writer.write(make_record(142, data_direction::rx, "master",
                                 {0x10, 0x02, 0x7f}));
        writer.write(make_record(1500, data_direction::tx, "tap", {0xde, 0xad}));
        writer.finish();
    }

    std::istringstream lines(out.str());
    std::string first;
    std::getline(lines, first);
    REQUIRE(first.find("\"started_utc\":\"2026-06-08T12:00:00.000Z\"") !=
            std::string::npos);

    std::string second;
    std::getline(lines, second);
    REQUIRE_EQ(second,
               "{\"offset_ms\":142,\"time_utc\":\"2026-06-08T12:00:00.142Z\","
               "\"port\":\"master\",\"direction\":\"R\",\"bytes\":3,"
               "\"hex\":\"10 02 7f\"}");

    std::string third;
    std::getline(lines, third);
    REQUIRE(third.find("\"time_utc\":\"2026-06-08T12:00:01.500Z\"") !=
            std::string::npos);
    REQUIRE(third.find("\"direction\":\"W\"") != std::string::npos);
}

TEST_CASE("capture: dates and formats are written and named correctly")
{
    REQUIRE_EQ(format_utc(std::chrono::system_clock::time_point{}),
               std::string("1970-01-01T00:00:00.000Z"));
    // A leap day, and the start of a century that is not a leap year.
    REQUIRE_EQ(format_utc(std::chrono::system_clock::time_point(
                   std::chrono::seconds(1'709'210'096) + 7ms)),
               std::string("2024-02-29T12:34:56.007Z"));
    REQUIRE_EQ(format_utc(std::chrono::system_clock::time_point(
                   std::chrono::seconds(4'107'542'400))),
               std::string("2100-03-01T00:00:00.000Z"));

    REQUIRE(parse_capture_format("CAP") == capture_format::cap);
    REQUIRE(parse_capture_format("csv") == capture_format::csv);
    REQUIRE(parse_capture_format("ndjson") == capture_format::jsonl);
    REQUIRE(!parse_capture_format("text"));
    REQUIRE(capture_format_for_path("run.CAP") == capture_format::cap);
    REQUIRE(capture_format_for_path("run.jsonl") == capture_format::jsonl);
    REQUIRE(!capture_format_for_path("run.txt"));
}

// -- reading --------------------------------------------------------------------

TEST_CASE("capture: reading the fixture gives back the records")
{
    const auto read =
        read_capture_file((fs::path(VSS_TEST_DATA_DIR) / "automate_sample.cap").string());
    REQUIRE(read);
    REQUIRE(read->format == capture_format::cap);
    REQUIRE_EQ(read->skipped_lines, std::size_t{0});

    const auto expected = fixture_records();
    REQUIRE_EQ(read->records.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        REQUIRE_EQ(read->records[i].offset_ms, expected[i].offset_ms);
        REQUIRE(read->records[i].direction == expected[i].direction);
        REQUIRE(read->records[i].data == expected[i].data);
    }
}

TEST_CASE("capture: the reader follows Automate's rules for odd lines")
{
    std::istringstream in(
        "# a comment\r\n"
        "\r\n"
        "[         0] # Opened device: /dev/ttyUSB0\r\n"
        "[       100] W 0x10|0x02\r\n"
        "[0000000090] R 0x41|0x42\r\n"      // older zero padded stamp, out of order
        "0x61|0x62\r\n"                      // legacy: bytes only
        "[       120] R 0x43|oops|0x44\r\n"  // a bad token costs the token only
        "[       130] X 0x01\r\n"            // unknown direction
        "[ no stamp ] R 0x01\r\n"            // not a number
        "[       140] R\r\n");               // no bytes

    const auto read = read_capture(in);
    REQUIRE(read);
    // The bad token, the unknown direction and the bad stamp are counted.
    REQUIRE_EQ(read->skipped_lines, std::size_t{3});
    REQUIRE_EQ(read->records.size(), std::size_t{4});

    // Sorted by offset; the legacy line takes the largest offset seen before it.
    REQUIRE_EQ(read->records[0].offset_ms, 90);
    REQUIRE(read->records[0].data == byte_buffer({0x41, 0x42}));
    REQUIRE_EQ(read->records[1].offset_ms, 100);
    REQUIRE(read->records[1].direction == data_direction::tx);
    REQUIRE_EQ(read->records[2].offset_ms, 100);
    REQUIRE(read->records[2].direction == data_direction::rx);
    REQUIRE(read->records[2].data == byte_buffer({0x61, 0x62}));
    REQUIRE_EQ(read->records[3].offset_ms, 120);
    REQUIRE(read->records[3].data == byte_buffer({0x43, 0x44}));
}

TEST_CASE("capture: JSON Lines reads back what it wrote")
{
    std::ostringstream out;
    {
        capture_writer writer(out, capture_format::jsonl, fixture_header());
        writer.write(make_record(5, data_direction::rx, "a", {1, 2, 3}));
        writer.write(make_record(9, data_direction::tx, "b \"quoted\"", {4}));
        writer.finish();
    }
    out << "this is not json\n";

    std::istringstream in(out.str());
    const auto read = read_capture(in);
    REQUIRE(read);
    REQUIRE(read->format == capture_format::jsonl);
    REQUIRE(read->started.has_value());
    REQUIRE(*read->started == fixture_start());
    REQUIRE_EQ(read->skipped_lines, std::size_t{1});
    REQUIRE_EQ(read->records.size(), std::size_t{2});
    REQUIRE_EQ(read->records[0].port, std::string("a"));
    REQUIRE(read->records[0].data == byte_buffer({1, 2, 3}));
    REQUIRE_EQ(read->records[1].port, std::string("b \"quoted\""));
    REQUIRE(read->records[1].direction == data_direction::tx);
}

TEST_CASE("capture: CSV is written but not read, and a missing file is not found")
{
    std::istringstream csv("time_utc,offset_ms,port,direction,bytes,hex\n");
    const auto read = read_capture(csv);
    REQUIRE(!read);
    REQUIRE(read.error() == make_error_code(errc::unsupported));

    scratch_directory scratch;
    const auto missing = read_capture_file((scratch.path() / "none.cap").string());
    REQUIRE(!missing);
    REQUIRE(missing.error() == make_error_code(errc::not_found));
    const auto directory = read_capture_file(scratch.path().string());
    REQUIRE(!directory);
}

TEST_CASE("capture: a capture file written for each format is complete")
{
    scratch_directory scratch;
    const auto records = fixture_records();

    for (const auto format : {capture_format::cap, capture_format::csv,
                              capture_format::jsonl})
    {
        const auto path =
            (scratch.path() / (std::string("file.") + to_string(format))).string();
        REQUIRE(write_capture_file(path, format, fixture_header(), records));
        const auto text = read_text(path);
        REQUIRE(!text.empty());
        REQUIRE_EQ(text.back(), '\n');
        if (format != capture_format::csv)
        {
            const auto read = read_capture_file(path);
            REQUIRE(read);
            REQUIRE_EQ(read->records.size(), records.size());
        }
    }

    REQUIRE(!write_capture_file((scratch.path() / "no" / "dir.cap").string(),
                                capture_format::cap, fixture_header(), records));
}

// -- recording from a hub ------------------------------------------------------------

TEST_CASE("capture: a session writes every byte of both directions")
{
    auto hub = start_hub();
    auto master = add_mock(*hub, "master");
    auto device = add_mock(*hub, "device");
    REQUIRE(hub->connect(master.endpoint->id(), device.endpoint->id()));

    auto out = std::make_shared<std::ostringstream>();
    capture_options options;
    options.format = capture_format::jsonl;
    auto session = capture_session::start(*hub, out, options);

    // More than the monitor's preview, and every byte value.
    const std::string payload = std::string(300, 'x') + "end";
    master.inject_read(payload);

    // The master port received it, and the device port was written it.
    REQUIRE(wait_for_records(*session, 2));
    session->stop();

    std::istringstream in(out->str());
    const auto read = read_capture(in);
    REQUIRE(read);
    REQUIRE_EQ(read->records.size(), std::size_t{2});
    for (const auto& record : read->records)
    {
        REQUIRE_EQ(record.data.size(), payload.size());
        if (record.port == "master")
            REQUIRE(record.direction == data_direction::rx);
        else
        {
            REQUIRE_EQ(record.port, std::string("device"));
            REQUIRE(record.direction == data_direction::tx);
        }
    }
}

TEST_CASE("capture: the port filter leaves the duplicate rows out")
{
    auto hub = start_hub();
    auto master = add_mock(*hub, "master");
    auto device = add_mock(*hub, "device");
    auto tap = add_mock(*hub, "tap");
    REQUIRE(hub->connect(master.endpoint->id(), device.endpoint->id()));
    REQUIRE(hub->monitor(master.endpoint->id(), tap.endpoint->id()));

    auto everything = std::make_shared<std::ostringstream>();
    auto only_tap = std::make_shared<std::ostringstream>();
    capture_options all;
    all.format = capture_format::jsonl;
    capture_options filtered = all;
    filtered.ports = {"tap"};
    auto all_session = capture_session::start(*hub, everything, all);
    auto tap_session = capture_session::start(*hub, only_tap, filtered);

    master.inject_read("poll");

    // master R, device W and tap W: one transmission, three rows.
    REQUIRE(wait_for_records(*all_session, 3));
    REQUIRE(wait_for_records(*tap_session, 1));
    all_session->stop();
    tap_session->stop();

    REQUIRE_EQ(all_session->records(), std::uint64_t{3});
    REQUIRE_EQ(tap_session->records(), std::uint64_t{1});

    std::istringstream in(only_tap->str());
    const auto read = read_capture(in);
    REQUIRE(read);
    REQUIRE_EQ(read->records.size(), std::size_t{1});
    REQUIRE_EQ(read->records[0].port, std::string("tap"));
    REQUIRE(read->records[0].direction == data_direction::tx);
    REQUIRE(read->records[0].data == byte_buffer({'p', 'o', 'l', 'l'}));
}

TEST_CASE("capture: offsets come from the event time on one clock")
{
    auto hub = start_hub();
    auto first = add_mock(*hub, "first");
    auto second = add_mock(*hub, "second");

    // The clock the test reads is the clock the events carry.
    event_recorder events;
    hub->subscribe({}, [events](const endpoint_event& event) {
        events.record(event);
    });

    auto out = std::make_shared<std::ostringstream>();
    capture_options options;
    options.format = capture_format::jsonl;
    auto session = capture_session::start(*hub, out, options);
    const auto origin = clock_type::now();

    first.inject_read("a");
    REQUIRE(wait_for_records(*session, 1));
    std::this_thread::sleep_for(150ms);
    second.inject_read("b");
    REQUIRE(wait_for_records(*session, 2));
    session->stop();

    std::istringstream in(out->str());
    const auto read = read_capture(in);
    REQUIRE(read);
    REQUIRE_EQ(read->records.size(), std::size_t{2});

    // The second chunk is 150 ms after the first, from either port.
    const auto gap = read->records[1].offset_ms - read->records[0].offset_ms;
    REQUIRE(gap >= 145);
    REQUIRE(gap <= 400);

    // And each offset is the event's own time, not the time the writer ran.
    for (const auto& event : events.snapshot())
    {
        if (event.kind != event_kind::data)
            continue;
        const auto expected =
            std::chrono::duration_cast<std::chrono::milliseconds>(event.when - origin)
                .count();
        for (const auto& record : read->records)
        {
            if (record.port == event.name)
                REQUIRE_NEAR(record.offset_ms, expected, 50);
        }
    }
}

TEST_CASE("capture: a session writes to a file and closes it on stop")
{
    scratch_directory scratch;
    const auto path = (scratch.path() / "bus.cap").string();

    auto hub = start_hub();
    auto device = add_mock(*hub, "device");

    auto session = capture_session::start(*hub, path, capture_options{});
    REQUIRE(session);
    device.inject_read("hi");
    REQUIRE(wait_for_records(**session, 1));
    REQUIRE((*session)->good());
    (*session)->stop();

    const auto text = read_text(path);
    REQUIRE(text.rfind("# Serial recording started at: ", 0) == 0);
    REQUIRE(text.find("] R 0x68|0x69\n") != std::string::npos);
    REQUIRE(text.size() >= 18);
    REQUIRE_EQ(text.substr(text.size() - 18), std::string("# Recording ended\n"));

    // A path that cannot be created is an error, not a silent no-op.
    const auto bad = capture_session::start(
        *hub, (scratch.path() / "missing" / "x.cap").string(), capture_options{});
    REQUIRE(!bad);
}

TEST_CASE("capture: a stopped session records nothing more")
{
    auto hub = start_hub();
    auto device = add_mock(*hub, "device");
    auto out = std::make_shared<std::ostringstream>();
    auto session = capture_session::start(*hub, out, capture_options{});

    device.inject_read("one");
    REQUIRE(wait_for_records(*session, 1));
    session->stop();
    session->stop(); // twice is fine

    device.inject_read("two");
    REQUIRE(wait_until([&] { return device.endpoint->stats().bytes_rx == 6; }, 2000ms));
    std::this_thread::sleep_for(50ms);
    REQUIRE_EQ(session->records(), std::uint64_t{1});
    REQUIRE(out->str().find("0x74|0x77|0x6f") == std::string::npos);
}
