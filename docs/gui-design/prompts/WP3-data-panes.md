# WP3: ports, inspector, send, monitor and status data
Model: deepseek-v4-pro. Branch: `gui/wp3-data-panes`. Depends on WP2.

## Read first
`AGENTS.md`, `docs/screens/02-workspace.md`, `docs/04-components.md` (PortDelegate, StatusChip, DirectionBadge), `docs/06-accessibility.md`, `docs/08-engine-gaps.md` (peer-open row). Mockups: `Workspace.html`, `Advanced.html`, `Compact.html`.

## Task
1. **PortsModel** (`QAbstractTableModel`) fed by the adapter, with roles for name, role text, path, state, rates and dropped bytes. **PortDelegate** paints the three-line row with StatusChip and CopyButton hit areas (24 px minimum). Advanced mode switches the same dock to the table with RX, TX and Dropped columns and a filter field. Keyboard: Up, Down, Enter, Delete (confirm only for now), F2 only if the capability exists, Ctrl+Shift+C.
2. **State mapping** to the vocabulary in `docs/04-components.md`. "Waiting for program" requires `PeerState::NotOpen`. With `Unknown` show "Open" and omit the waiting sentence.
3. **Inspector** per spec (Simple and Advanced variants): state, path, traffic, help line with the "Why is nothing arriving?" link (opens the dialog once WP8 lands, until then a placeholder action), control lines as chips, Advanced switches for DTR and RTS, Break, and the "Open line matrix and faults" link (placeholder until WP7).
4. **Send** panel per spec including hex validation, Add CRLF, Advanced Repeat and saved sends.
5. **Monitor** per spec: `MonitorModel` on a bounded ring (named constant, default 10,000 rows), batched inserts per refresh interval on the GUI thread, ASCII and Hex rendering, escapes for `\r` and `\n`, DROP rows with their muted text, Pause (Space), Clear, Follow, Advanced filters (port, RX, TX, Dropped), search, Export, and the Log tab fed by an `EventLog`. The Topology JSON tab is a WP7 placeholder.
6. **Status bar fields**: ports, links, RX and TX rates, hub state, dropped warning chip.
7. **Selection sync** between Ports, Monitor and (later) Topology through one `SelectionModel`-style service.
8. Tests: model updates under bursts, ring cap, coalescing (no more than one GUI update per interval), hex and ASCII formatting, filter combinations, accessible names for rows, no per-row widgets, the 24 px target check.

## Constraints
Never block the GUI thread on the engine. No per-row widgets in views. Only `Theme` colours and WP1 widgets. Compact layout rules in `03-information-architecture.md`.

## Acceptance
A 10,000-row burst keeps the UI responsive (include a timing assertion with generous bounds). Selection stays in sync. The empty monitor shows the exact empty-state text.
