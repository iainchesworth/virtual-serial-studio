//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#pragma once

#include <vss/core/bytes.hpp>
#include <vss/core/error.hpp>
#include <vss/core/hub.hpp>
#include <vss/core/types.hpp>

#include <chrono>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vss {

// -- the capture format --------------------------------------------------------
//
// A capture is a list of traffic records.  Each record says when it
// happened (milliseconds from the start of the capture), on which port, in which
// direction, and carries every byte of the chunk.
//
// Direction.  `R` is bytes a port received, that is, bytes that came in from
// whatever is attached to the port (a program, a device, a remote client).
// `W` is bytes written to the port, that is, bytes the hub sent out to that
// attachment.  These are the engine's `rx` and `tx`.  One transmission on a bus
// therefore shows up as `R` on the port of the sender and as `W` on the port of
// each receiver; a passive tap port shows it once, as `W`.  Leave the ports
// you do not want out with `capture_options::ports`.
//
// Time.  Every offset comes from `endpoint_event::when`, the steady clock
// the endpoint read at the moment the bytes moved, minus the moment the capture
// started.  All members of a hub share that one clock, including a remote one,
// whose own clock is never used.

enum class capture_format
{
    /// The `.cap` text format: `[ms] R|W 0xHH|0xHH|...`, one line per chunk.
    cap,
    /// Comma separated values with a header row.
    csv,
    /// One JSON object per line.
    jsonl
};

const char* to_string(capture_format value) noexcept;

/// "cap", "csv" or "jsonl" (also "ndjson"), case insensitive.
std::optional<capture_format> parse_capture_format(std::string_view text);

/// Picks a format from a file name's extension (`.cap`, `.csv`, `.jsonl`,
/// `.ndjson`); empty for anything else.
std::optional<capture_format> capture_format_for_path(std::string_view path);

/// One chunk of traffic.
struct capture_record
{
    /// Milliseconds from the start of the capture.
    std::int64_t offset_ms = 0;
    /// The port the chunk belongs to.  A `.cap` file has no room for it.
    std::string port;
    /// `rx` is `R`, `tx` is `W`.
    data_direction direction = data_direction::rx;
    byte_buffer data;
};

/// The letter a direction has in a capture.
char direction_letter(data_direction direction) noexcept;

struct capture_header
{
    /// The wall clock at offset zero.  Carried in the header of every format,
    /// so the absolute time of a record is the start plus its offset.
    std::chrono::system_clock::time_point started =
        std::chrono::system_clock::now();
};

/// Writes records in one format.  Not thread safe: a `capture_session` puts
/// its own lock around it.
class capture_writer
{
public:
    /// Writes the header (a `.cap` comment block, the CSV column row, or the
    /// JSON Lines description line) to `out`.
    capture_writer(std::ostream& out, capture_format format,
                   const capture_header& header);

    void write(const capture_record& record);

    /// Writes the closing line (`# Recording ended` for `.cap`) and flushes.
    /// Safe to call twice.
    void finish();

    bool good() const;
    std::uint64_t records() const noexcept { return records_; }
    std::uint64_t bytes() const noexcept { return bytes_; }

private:
    std::ostream& out_;
    capture_format format_;
    capture_header header_;
    std::uint64_t records_ = 0;
    std::uint64_t bytes_ = 0;
    bool finished_ = false;
};

/// Formats a wall clock time as `2026-06-08T12:00:00.142Z`.
std::string format_utc(std::chrono::system_clock::time_point time);

// -- reading -------------------------------------------------------------------

struct capture_file
{
    capture_format format = capture_format::cap;
    /// The wall clock at offset zero, when the file says (JSON Lines does).
    std::optional<std::chrono::system_clock::time_point> started;
    /// Sorted by offset; records with the same offset keep file order.
    std::vector<capture_record> records;
    /// Lines that were not blank, not a comment and could not be read.
    std::size_t skipped_lines = 0;
};

/// Reads a `.cap` or JSON Lines capture.  `format` defaults to what the first
/// meaningful line looks like.  CSV is written but not read:
/// `errc::unsupported`.
result<capture_file> read_capture(std::istream& in,
                                  std::optional<capture_format> format = {});

result<capture_file> read_capture_file(const std::string& path);

/// Writes `records` to `path` in `format`.  The file is created with the
/// owner's permissions where the platform has them.
result<void> write_capture_file(const std::string& path, capture_format format,
                                const capture_header& header,
                                const std::vector<capture_record>& records);

// -- recording from a hub --------------------------------------------------------

struct capture_options
{
    capture_format format = capture_format::cap;
    /// Only these ports, by name.  Empty keeps every port.
    std::vector<std::string> ports;
};

/// Subscribes to a hub and writes every data event it sees, until `stop()` or
/// destruction.  Nothing is coalesced and no preview is cut: a record holds
/// the whole chunk the endpoint moved.
///
/// The hub must outlive the session, or the session must be stopped first.
class capture_session
{
public:
    /// Opens `path` for writing.  `errc::permission_denied` when it cannot be
    /// created.
    static result<std::unique_ptr<capture_session>> start(
        hub& engine, const std::string& path, const capture_options& options);

    /// Writes to `out`, which the session keeps alive.
    static std::unique_ptr<capture_session> start(
        hub& engine, std::shared_ptr<std::ostream> out,
        const capture_options& options);

    ~capture_session();

    capture_session(const capture_session&) = delete;
    capture_session& operator=(const capture_session&) = delete;

    /// Stops listening, writes the closing line and closes the file.
    void stop();

    std::uint64_t records() const;
    std::uint64_t bytes() const;
    /// False once a write to the file has failed (the disk is full, say).
    bool good() const;

    /// The wall clock at offset zero.
    std::chrono::system_clock::time_point started() const noexcept
    {
        return header_.started;
    }

private:
    struct state;

    capture_session(hub& engine, std::shared_ptr<std::ostream> out,
                    const capture_options& options);

    hub& hub_;
    capture_header header_;
    subscriber_id subscription_ = 0;
    std::shared_ptr<state> state_;
};

} // namespace vss
