//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fuzz harness for the saved topology parser. Any bytes may arrive from a
// file a person opened, so the parser must reject bad input with an error and
// never crash. What it accepts must validate without a crash, and the text
// written for it must read back and write again unchanged.
//

#include <vss/config/document.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>

// libFuzzer looks for exactly this name.
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size)
{
    const std::string_view text(reinterpret_cast<const char*>(data), size);

    const auto parsed = vss::config::from_json(text);
    if (!parsed)
    {
        return 0;
    }

    static_cast<void>(vss::config::validate(*parsed));
    for (const auto& port : parsed->ports)
    {
        static_cast<void>(vss::config::to_endpoint_kind(port.kind));
    }

    const auto written = vss::config::to_json(*parsed, false);
    if (!written)
    {
        std::abort();
    }
    // The writer leaves out fields that do not apply to a port's kind, so the
    // document read back can differ from the one parsed. The text it writes
    // must not change from then on.
    const auto again = vss::config::from_json(*written);
    if (!again)
    {
        std::abort();
    }
    const auto rewritten = vss::config::to_json(*again, false);
    if (!rewritten || *rewritten != *written)
    {
        std::abort();
    }
    return 0;
}
