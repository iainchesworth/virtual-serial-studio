//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Includes every public header on its own to prove each one is
// self-contained.

#include <vss/core/bytes.hpp>
#include <vss/core/control_lines.hpp>
#include <vss/core/endpoint.hpp>
#include <vss/core/error.hpp>
#include <vss/core/event.hpp>
#include <vss/core/hub.hpp>
#include <vss/core/link.hpp>
#include <vss/core/serial_settings.hpp>
#include <vss/core/stats.hpp>
#include <vss/core/traffic_log.hpp>
#include <vss/core/transport.hpp>
#include <vss/core/types.hpp>

#include <vss/ports/com0com.hpp>
#include <vss/ports/loopback_transport.hpp>
#include <vss/ports/native_lines.hpp>
#include <vss/ports/serial_enumeration.hpp>
#include <vss/ports/serial_transport.hpp>
#include <vss/ports/virtual_port.hpp>

#include <vss/config/document.hpp>

#include <vss/studio/studio.hpp>

#include <vss/testkit/harness.hpp>

#include <cstdio>

int main()
{
    // Touch a few symbols so the translation unit is not optimised away.
    const auto size = vss::parse_byte_size("64KB");
    const auto label = vss::to_label(vss::serial_settings{});
    std::printf("headers ok: %s %s\n", size ? "size" : "no-size", label.c_str());
    return size && *size == 64 * 1024 ? 0 : 1;
}
