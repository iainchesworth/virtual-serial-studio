//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#ifndef VSS_GUI_BACKEND_CAPABILITIES_HPP
#define VSS_GUI_BACKEND_CAPABILITIES_HPP

namespace vss::gui
{

/// What this build of the engine can actually do.
///
/// A screen asks this once and degrades visibly when a capability is absent:
/// a disabled control with the tooltip "Not available in this build", or the
/// simpler variant the design specifies.  It never simulates the missing
/// behaviour.  The values here are the ones WP0 verified against the code;
/// see `docs/gui-design/implementation-plan.md`.
struct capabilities
{
    /// Create virtual ports of every kind the platform supports.
    bool create_ports = true;
    /// Enumerate serial devices and com0com pairs.
    bool devices = true;
    /// Duplex, split, join, redirect and monitor links with options.
    bool links = true;
    /// Set and release the owner of an owner-policy link.
    bool owner = true;
    /// Drive DTR, RTS and BREAK and read CTS, DSR, DCD and RI.
    bool control_lines = true;
    /// Create a scriptable mock fixture.
    bool fixtures = true;
    /// Inject faults into a mock fixture.  False: the adapter cannot reach
    /// the fixture through `vss::studio` today.
    bool faults = false;
    /// Per-line wiring matrix.  False: the engine only has a boolean
    /// null-modem crossing.
    bool line_matrix = false;
    /// Observe whether another program opened the advertised path.  False:
    /// `endpoint_info::open` is the studio's own transport state.
    bool peer_open = false;
    /// Rename a port safely.  False: the hub can rename but the studio's
    /// records and saved JSON would go out of sync.
    bool rename_port = false;
    /// Undo/redo restored from an exact snapshot.
    bool undo = false;
    /// Run the benchmark from the GUI.
    bool benchmark = false;
    /// Create the diagnostic bundle.  The ZIP writer lives in the GUI layer,
    /// so this is present whenever the GUI is built.
    bool diagnostics = true;
    /// Report JSON validation errors with a line number.
    bool json_positions = false;
    /// The setup check can gather driver and device facts.
    bool setup_check = true;
    /// The share scenario and its rollback.
    bool share = true;
    /// TCP, UDP and RFC 2217 endpoints built from a URI.
    bool bridge = true;
    /// Wait for other computers to connect (a listening TCP/UDP endpoint).
    /// False: the backend cannot open `tcp-listen://` or `udp-listen://`
    /// endpoints. The engine can (`vss_ports`' listen transport), so only a
    /// backend without one leaves this off. RFC 2217 has no server role.
    bool network_server = false;
    /// Create another com0com null-modem pair (Windows, needs elevation).
    /// The share wizard's warn note calls the adapter method this gates.
    bool com0com_pair_creation = true;
};

} // namespace vss::gui

#endif // VSS_GUI_BACKEND_CAPABILITIES_HPP
