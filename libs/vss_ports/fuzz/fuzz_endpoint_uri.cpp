//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fuzz harness for the endpoint URI parser. A URI comes from a topology file,
// the command line or a text box, so any bytes must give either a backend kind
// or an error.
//

#include <vss/ports/serial_transport.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>

// libFuzzer looks for exactly this name.
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size)
{
    const std::string_view uri(reinterpret_cast<const char*>(data), size);
    static_cast<void>(vss::classify_uri(uri));
    return 0;
}
