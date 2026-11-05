# 08 Engine gap analysis and the backend adapter

The designs ask for some things the engine may not expose yet. **This table is built from the project's README and docs. I could not read the source, so treat "Known" as "documented" and verify every row in WP0.** Never change `vss_core` semantics to suit the GUI without an explicit task. Prefer a thin addition in `vss_studio`, and say so in the PR.

## The adapter rule
Put one narrow interface between the GUI and the engine, for example `StudioBackend` (name it after WP0 finds the existing conventions), with a `Capabilities` struct of booleans. Screens call the adapter, never the engine directly. Each feature checks its capability and, when absent, degrades visibly (disabled with a tooltip "Not available in this build", or a simpler variant) instead of failing or faking it.

## Capability table
| Capability the UI needs | Status | Plan if missing |
|---|---|---|
| Create, list, remove virtual ports (auto, pty, loopback, windows-driver) | Known | Use directly. |
| Enumerate serial devices and com0com pairs | Known (`vss_cli devices`) | Use directly. |
| Links: connect, split, join, redirect, monitor, merge policy interleave or owner | Known | Use directly. |
| Owner arbitration: set owner, release owner | Known (`set_owner`, `release_owner` on the hub) | Verify the studio facade exposes it. |
| Cycle refusal on add link | Known (engine refuses) | Map the error to the loop pattern. "Show the loop" computes the cycle from the link graph in the GUI. |
| Events, counters, bounded traffic ring buffer | Known | Subscribe through the sink, marshal to the GUI thread, batch by refresh interval. |
| Set DTR, RTS, BREAK. Read CTS, DSR, DCD, RI | Known | Use directly. Unsupported lines return an error, show a disabled control. |
| Null-modem crossing with an on or off switch (`emulate_modem_lines`) | Known | Use for "Emulate modem lines". |
| **Per-link line wiring matrix** | Not in the engine (listed as roadmap) | Ship the matrix read-only with the Standard preset only, behind `Capabilities::lineMatrix`. Do not simulate it in the GUI. |
| Fault injection on mock fixtures | Library mock backend can inject faults, GUI exposure unknown | Check the API on the object returned by `create_mock_port`. If absent, add a thin forwarding method in `vss_studio`. |
| **Peer-open detection** ("Waiting for program", "Program connected") | Unknown, may not be detectable for PTY or com0com | Add a tri-state `PeerState` (Unknown, NotOpen, Open) to the adapter. "Open" is the normal state. Show "Waiting for program" and "Program connected" only where the adapter reports a definite NotOpen or Open (`Capabilities::peer_open`, a driver-side signal later), and say nothing about an outside program on Unknown. Never claim what you cannot know. |
| Setup check inputs (com0com present, elevation, dialout membership) | Partly known (`detect_com0com`, pair list work without elevation) | Small platform helpers in the GUI layer. |
| "Install com0com" and "Restart as administrator" | GUI actions | Never install silently. Install opens an instructions dialog with a button to open the project page (URL in one named constant, confirm with the maintainer). Restart as administrator relaunches the app elevated on Windows only. |
| Free com0com pair count for the share wizard | Partial | `com0com_installation.pairs` plus paths already claimed by `endpoint_infos` | Derive from the pair list and which ends the hub already claimed. WP6 derives it from the device list and the ports snapshot; no new engine API. |
| Create one more com0com pair from the share wizard | Partial | `vss::create_com0com_pair` exists, but `vss::studio` has no facade and the call needs elevation | WP6 adds `StudioBackend::create_com0com_pair()` in the GUI adapter (no library change), gated by `capabilities::com0com_pair_creation`. |
| Share as one operation with rollback | Known (share scenario, failed bridge rolls back) | Use directly. |
| Bridge by URI (`tcp://`, `udp://`, `rfc2217://`), client role | Known | Build the URI in the GUI, preview it, pass it through. |
| **Network bridge in the server role** (listen for other computers) | **Missing, re-verified by WP6** | Boost.VirtualSerial's `tcp_bridge_port` and `udp_bridge_port` only resolve and connect; there is no acceptor path, and `vss::studio::bridge` attaches both URIs as clients. `capabilities::network_server` is false, the dialog defaults to the client role, and the server option is disabled with "Not available in this build". Never simulate a listener. |
| JSON topology load, save, validate | Known (`vss_config`) | Use directly. |
| JSON validation errors with line and column | Unknown | Use `QJsonDocument::fromJson` offsets for syntax. For schema errors without positions, show the message and hide "Go to line". |
| Rename port | Unknown (names are link identity) | Ship Rename only if the engine supports it. Otherwise hide the menu item and F2. |
| Undo and redo | Not in the engine | GUI `QUndoStack`. Snapshot before each mutating action and restore via the studio apply path. Verify restore is exact. If it cannot be made reliable, drop the Undo toast and keep confirmations. |
| Benchmark | CLI only (`vss_cli bench`) | Extract the core into a shared library function with progress and cancel callbacks, used by both CLI and GUI. Needs maintainer approval, do it in its own PR. |
| Diagnostic bundle zip | None | No new dependency. Write a small stored (uncompressed) ZIP writer in the GUI layer, with tests. |
| Event log tab | None | GUI-side `EventLog` fed from hub events. |
| Recent files, templates | None | `QSettings` list, and JSON templates bundled as Qt resources validated by `vss_config`. |
| Node positions in topology | Not in JSON | Session only. Do not change the JSON schema. |
