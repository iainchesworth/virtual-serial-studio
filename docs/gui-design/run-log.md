# GUI redesign run log

One row per work package. Branches are local; nothing is pushed. Work
packages merge into `gui/integration` only after the build, test and reviewer
gates pass.

| WP | Branch | Status | Build and tests | Review | Notes |
|---|---|---|---|---|---|
| WP0 | `gui/wp0-recon` | implemented, self-review complete | Debug `ctest` 5/5, Release 6/6, 10 backend tests x40, `vss_gui --selftest` ok, changelog ok, clang-format clean | self-review + human G0; reviewer sessions timed out | bundle installed; implementation plan written; backend + event bridge + 10 tests |
| WP1 | `gui/wp1-theme-widgets` | implemented, self-review complete | Debug `ctest` 6/6, 13 theme/widget tests, gallery PNGs light and dark, contrast/QSS/changelog/format clean | self-review + human G1 | theme, icons, focus ring, widget library, gallery, token tools, CI job |
| WP2 | `gui/wp2-shell` | implemented, self-review complete | Debug `ctest` 7/7, Release `ctest` 8/8, 6/6 shell tests, `vss_gui --selftest` ok, six review PNGs, contrast/QSS/changelog/format clean | self-review; human G1 | action registry, level, launcher, command palette, status bar, dock frame; old shell retained until WP9 |
| WP3 | `gui/wp3-data-panes` | implemented, self-review complete | Debug `ctest` 8/8, Release 9/9, 7/7 pane tests, simple/advanced/dark/compact PNGs, `vss_gui --selftest` ok, changelog/format clean | self-review; pending G2 | ports, inspector, send, monitor, status data, selection; legacy sources leave the executable |
| WP4 | `gui/wp4-topology` | implemented, self-review complete | Debug `ctest` 8/8 on its branch, 18 topology tests, light and dark review PNGs, changelog/contrast/QSS/format clean | self-review; pending G2 | topology view, layout, keyboard, a11y summary, links list, shell wiring; WP6/WP7 actions stay placeholders |
| WP5 | `gui/wp5-basic-flows` | not started | - | - | blocked on G1 |
| WP6 | `gui/wp6-connect-share` | not started | - | - | blocked on G1 |
| WP7 | `gui/wp7-advanced` | implemented, self-review complete | Debug `ctest` 8/8, 15 advanced tests, 16 review PNGs, changelog/format/contrast/QSS clean | self-review | owner bar, wiring and faults, mock fixtures, topology JSON, event log, benchmark, export command |
| WP8 | `gui/wp8-support` | implemented, self-review complete | Debug `ctest` 8/8, 15 support tests, 10 shell tests, `vss_gui --selftest` ok, Python `zipfile` round trip, contrast/QSS/changelog/format clean | self-review; human G2 pending | setup check, troubleshooting, bundle, settings, About, shortcuts, help index, confirm dialog; undo/redo disabled with a recorded reason |
| WP9 | `gui/wp9-qa` | not started | - | - | blocked on G2 |

## WP0 detail

- Installed the bundle at `docs/gui-design/` (98 files; `agents/`, the bundle
  ZIP and the two generated QSS previews excluded, the last per the bundle
  README's "do not commit" note).
- Wrote `docs/gui-design/implementation-plan.md`: current GUI inventory, every
  engine symbol the specs need, the capability table re-verified against the
  source, the module layout, the CMake pattern, the self-test plan and the
  open questions.
- Added `apps/vss_gui/src/backend/`: `StudioBackend`, `EngineStudioBackend`,
  `FakeStudioBackend`, `EventBridge`, capabilities, peer state and view types.
- Added `vss_gui_backend_tests` (in-tree framework, Qt `QCoreApplication`
  runner): 10 tests covering the fake, unique names, link shapes, owner
  validation, send, control lines, snapshot/apply, coalescing, queue cap,
  unsubscribe, restart and the shutdown race, plus one real-adapter loopback
  smoke test.
- Checks: Debug `ctest` 5/5 and Release `ctest` 6/6; the GUI tests also pass
  from a shell without Qt on `PATH` (the CTest property supplies it);
  `vss_gui --selftest` ok; the backend suite passed 40 consecutive runs;
  `python tools/changelog.py --check` ok; clang-format clean;
  `check_contrast.py` reports `FAILURES: 0`; `gen_qss.py` renders both themes.
- Environment: Qt 6.10.3 for WP0, MSVC 19.51, Boost.VirtualSerial sibling
  checkout. The `QT_ROOT` trailing-space trap is recorded in
  `design-deviations.md`. WP1 raises the GUI pin to Qt 6.12.0 because 6.10.3
  does not include the approved `Qt6::Svg` module.
- Reviewer tooling: three fresh reviewer sessions were started with the
  bundle's `REVIEW-checklist-prompt.md` and the diff; none returned a verdict
  within a workable time and all were stopped. The gate therefore uses the
  author's self-review against the same checklist plus the human G0 review.
  Re-run `REVIEW-checklist-prompt.md` in a fresh Pro session before the final
  merge to `develop`; the diff is `73d09bb..gui/wp0-recon`.

## WP1 detail

- Added `apps/vss_gui/src/theme/`: `Theme` (tokens, type scale, metrics,
  spacing, palette, QSS rendering, light/dark/system, density, reduced
  motion), `IconProvider` (26 SVGs, `currentColor`, cache flush), and
  `FocusRingStyle` (2 px keyboard-only ring over Fusion). The tokens, QSS
  template and icons are copied into a Qt resource; no font is bundled.
- Added `apps/vss_gui/src/widgets/`: StatusChip, DirectionBadge, Tag, Badge,
  StateDot, KeyCap, NoteBanner, ResultSentence, CodeBlock, CopyButton,
  SegmentedControl, PickCard, Switch, Stepper, Disclosure, FormRow, Toast and
  ToastManager, ConfirmDialog, ErrorDialog, and the WidgetGallery. The
  gallery opens with `VSS_WIDGET_GALLERY=1`; the old shell is untouched.
- Added `vss_gui_theme_tests`: 13 tests for token resolution, every QSS
  placeholder in both themes, the WCAG contrast pairs, the no-hex-literals
  scan, icon cache flush, fonts/metrics/spacing/density, reduced motion, the
  gallery in both themes, accessible names, 24 px targets and the widget
  behaviours.
- Review PNGs: `build/debug-gui/gui-shots/gallery-light.png` and
  `gallery-dark.png` (offscreen `grab()`, system font directory, never
  committed).
- Added `tools/check_contrast.py`, `tools/gen_qss.py` and a `design` CI job.
- Checks: `ctest --test-dir build/debug-gui` 6/6 (unit, system, headers,
  `gui_selftest`, `gui_backend`, `gui_theme`); 13/13 theme/widget tests;
  clang-format clean; `changelog.py --check`; contrast `FAILURES: 0`; QSS
  renders both themes.
- Qt 6.12.0 is required for `Qt6::Svg`; the offscreen gallery points at
  `C:/Windows/Fonts` in the test only so text is not missing glyphs.
- Deviations: the segmented control is a QRadioButton group; CopyButton is
  custom-painted to keep the 24 px icon target; both are recorded in
  `design-deviations.md`.

## WP2 detail

- Added `apps/vss_gui/src/shell/`: `ActionRegistry` (one `QAction` per
  command, level visibility, search keywords, shortcut uniqueness, enabled
  predicates and theme-aware icons), `LevelService` (`ui/advanced`),
  `SelectionService`, `Launcher` + `LauncherCard`, `CommandPalette`,
  `StatusBar`, `ShellWindow` and the small `format` helpers.
- `ShellWindow` is the application frame: menu bar, 48 px toolbar with the
  search field, the launcher/workspace stack, Ports/Inspector/Send/Monitor
  docks with placeholder panes, and the status bar. It opens, saves and
  creates the existing JSON document format, tracks dirty state in the title,
  persists geometry and dock state, handles the unsaved prompt, exposes the
  Simple/Advanced switch in the status bar and the View menu, and applies the
  theme/density choices. The old `MainWindow` is no longer constructed;
  its files remain until WP9.
- `CommandPalette` groups results into Available now and Needs Advanced mode,
  renders the mode explanation and empty-group sentence, selects the first
  action, supports keyboard navigation and turns Advanced mode on before
  running a hidden action.
- Extended WP1's `SegmentedControl` with a 24 px compact form for the status
  bar and added a healthy status-chip kind. The QSS template gained explicit
  menu text colours; `ActionRegistry` rebuilds icons on a theme change.
- Added `vss_gui_shell_tests`: six tests for action levels/search/shortcut
  uniqueness, palette grouping and activation, launcher-to-workspace
  transitions and dock visibility, saved layout, level persistence,
  accessible toolbar names, the no-duplicate-shortcut walk and the headless
  self-test.
- Review PNGs (never committed): `build/debug-gui/gui-shots/shell-light.png`,
  `shell-dark.png`, `launcher-light.png`, `launcher-dark.png`,
  `palette-light.png`, `palette-dark.png`.
- Checks: Debug `ctest` 7/7 (new `gui_shell`), 6/6 shell tests,
  `vss_gui --selftest` ok, changelog ok, clang-format clean; Release build and
  ctest recorded in the gate report.
- Deviations: `vss_gui_shell` instead of `vss_gui_ui`; old flat sources stay
  until WP9; placeholders show a status-bar boundary message; unsaved prompt
  uses a three-button QMessageBox until WP8; compact starts below 1100x700;
  Share toolbar entry deferred to WP6; Review-only screenshot env switches.
  All are recorded in `design-deviations.md` and section 11 of
  `implementation-plan.md`.

## WP3 detail

- Added `apps/vss_gui/src/panes/`: `PortModel` + `PortDelegate` +
  `PortsPanel`, `InspectorPanel`, `SendPanel`, `MonitorModel` +
  `DirectionDelegate` + `MonitorPanel`, the shared `SelectionService`, and the
  panes formatter. Simple rows are three-line painted rows with a real 24 px
  copy hit area; Advanced is the RX/TX/Dropped table with filtering. No
  per-row widgets.
- Ports, Inspector, Send and Monitor replace the WP2 placeholder dock pages.
  Selection flows through one service: a Ports row, a Monitor row and the
  Inspector/Send tabs follow the same endpoint. The Inspector reads real
  control lines, drives DTR/RTS and Break where the port supports them, and
  shows the exact waiting sentence only when the adapter reports NotOpen.
- Send validates hex live, adds CRLF by default, supports Advanced Repeat and
  saved sends in QSettings, and reports "queued" because the adapter has no
  per-write callback.
- Monitor consumes the coalesced `event_batch`, caps its ring at 10,000 rows,
  renders ASCII escapes and hex, filters by port/direction/search, pauses
  without stopping traffic, follows, exports visible rows and adds real DROP
  rows from link dropped-counter deltas. Log is a bounded read-only text view;
  Topology JSON remains a labelled WP7 tab.
- ShellWindow gained a structural document signature so traffic counters no
  longer mark the document dirty, real copy/remove handlers and confirmation,
  dock title counts/selected name, default 360/340/196 sizes, compact tabbing
  and panel mode updates. The legacy flat sources left the executable because
  the new pane class names collide with the old ones.
- Tests: `vss_gui_panes_tests` 7/7 (state/role/filter, 10k burst timing and cap,
  ASCII/hex/filters/pause/drop, Send hex/CRLF/error, Ports selection sync,
  Monitor empty state/no row widgets, Inspector waiting/control chips).
- Review PNGs: `wp3-light.png`, `wp3-advanced.png`, `wp3-dark.png`,
  `wp3-compact.png` under `build/debug-gui/gui-shots/` (never committed).
- Checks: Debug `ctest` 8/8 (`gui_panes` added), Release `ctest` 9/9,
  `vss_gui --selftest` ok, changelog/format/contrast/QSS clean.

## WP4 detail

- Added `apps/vss_gui/src/topology/`: `TopologyView` (QGraphicsView and scene),
  `NodeItem`, `LinkItem`, `compute_topology_layout` with cycle detection,
  `LinksList` and one copy module (`topology_text`). The view is fed a
  snapshot and never calls the studio; it reports intent with signals.
- Nodes are 168 by 56 (168 by 64 for a real device or a join target, 120 by 48
  in compact) with the design's radius, fills, dashed unlinked edge and the
  accent plus halo selection; the Owner, Dropped and "not linked" badges are
  painted from the snapshot. Links are 2 px `ink3` with round caps, 3 px
  `accent` when selected, filled heads for one-way and monitor, dashed monitor
  links, and Split/Join/two-way/owner labels kept 8 px clear of lines and
  nodes.
- Deterministic layout: one column per rank, sources left and targets right,
  every column vertically centred, 48 px minimum vertical gaps and a 96 px
  horizontal gap with the split/join bar in the middle. Unlinked ports get a
  final column so they cannot pull a source off the centre line.
- Keyboard: Tab through nodes then links, Enter inspects, Delete asks the
  shell to remove, arrows nudge 8 px (Shift 32), Shift+F10 and the menu key
  open Inspect/Copy name/Connect from here/Remove, Ctrl+plus/minus and
  Ctrl+wheel zoom, Ctrl+0 fits, Escape clears. Node offsets are session only.
- Accessibility: view name "Topology", generated description that updates on
  change, 24 px targets, focus ring, and the optional "Show links as a list"
  dock exposing every link with names and states through `SelectionService`.
- Loop refusal: `show_loop_note()` computes the cycle, highlights it in `bad`
  and shows the exact danger note with "Show the loop"; WP6's Connect dialog
  owns the modal refusal and can call it.
- Shell wiring: the central placeholder is replaced by the view, `edit.remove`
  and `topology.removeLink` now confirm and call the backend, `topology.fit`
  fits, Copy name uses the clipboard and a status sentence, and the links dock
  is a View Simple action. Connect/Share/Watch name WP6 and Link wiring/Owner
  name WP7.
- Added `vss_gui_topology_tests` (`gui_topology`): 18 tests for the empty
  state, node sizes and summary text, every link kind's layout and 48 px gaps,
  mouse and keyboard selection, hit testing, context menu, fit/zoom limits,
  compact sizes, 200 percent text sizing, the links list, theme repaint, loop
  detection and highlight, session-only nudges, badges, and the shell's
  selection and placeholder wiring.
- Review PNGs (never committed): `build/debug-gui/gui-shots/topology-light.png`
  and `topology-dark.png`.
- Checks: Debug `ctest` 8/8 (new `gui_topology`), `vss_gui --selftest` ok,
  changelog ok, clang-format clean, contrast and QSS tools unchanged.
- Deviations: nested namespace `vss::gui::topology` because the legacy
  `TopologyView` still links until WP9; Rename hidden (`rename_port` false)
  and Remove without Undo (`undo` false); keyboard nudge only, no dragging;
  node sizes grow at larger text; compact below 500 px; unlinked ports in a
  final column; monitor links keep an arrow head; loop note lives in the view
  until WP6; no per-item QAccessibleInterface (summary plus Links list); links
  dock placement; auto-fit until the user zooms; single selection. All are
  recorded in `design-deviations.md`.

## WP7 detail

- Added `apps/vss_gui/src/advanced/`: `OwnerBar` (policy, owner chip, Release
  owner and Give to through `set_owner`/`release_owner`), `LineMatrixPanel` and
  `WiringDialog` (Standard null-modem matrix, read-only line state card,
  control-line switches and Break, gated fault tiles and history),
  `AddFixtureDialog` (real `create_mock_port` plus an optional two-way link,
  with rollback), `json_validation` (syntax offsets, explicit schema errors,
  loops and a fix suggestion), `TopologyJsonEditor` (line gutter, live
  validation, Format/Revert/Apply, command block, export), `CommandPanel`,
  `ExportCommandDialog`, `EventLogPanel`, `BenchmarkDialog`, the shared
  `chrome` footer/header helpers and `line_presets`.
- Backend seam: `line_wiring` in `view_types.hpp`; `link_wiring()` and
  `set_link_wiring()` on `StudioBackend` default to unsupported, the real
  adapter keeps `Capabilities::line_matrix` false, and the fake stores one for
  the editable-path test. No `libs/vss_core` or JSON schema change.
- `ShellWindow` now opens the real dialogs for Mock fixture, Link wiring,
  Owner, Benchmark and Export as command; Log and Topology JSON are advanced
  docks tabified with Monitor; the owner bar sits under the topology
  placeholder and follows the selected link/port; the status bar reads
  "N dropped, not owner" when the drops came from an owner-policy link; the
  event log records engine errors/state changes and GUI actions.
- Added `vss_gui_advanced_tests` (offline `QApplication`, in-tree framework),
  registered as CTest `gui_advanced` in `tests/CMakeLists.txt`. 15 tests:
  owner give/release and capability gating, fixture creation and gating, both
  matrix modes, the wiring dialog header, syntax/mode/duplicate/loop
  validation, the editor's apply gate and fix action, event-log filtering,
  benchmark capability and link refusal, export command naming, accessible
  names and 24 px targets across every advanced control, the shell's advanced
  dock/owner-bar mode behaviour, and the review PNG pass.
- Review PNGs (never committed), light and dark for each:
  `build/debug-gui/gui-shots/` `wiring-*`, `add-fixture-*`, `benchmark-*`,
  `topology-json-*`, `export-command-*`, `owner-bar-*`, `event-log-*`,
  `shell-advanced-*`.
- Checks: `cmake --preset debug-gui`, `cmake --build --preset debug-gui`,
  `ctest --preset debug-gui` 8/8; the advanced suite passed 3 consecutive
  runs; `vss_gui --selftest` ok; `python tools/changelog.py --check` ok;
  clang-format clean; `check_contrast.py` and `gen_qss.py` unchanged and
  passing (no token edits).
- Deviations: faults stay gated with no `vss_studio` forwarding method;
  wiring, the policy combo and "Emulate modem lines" are read-only; owner
  node chips, the Ports Dropped column and DROP monitor rows belong to
  WP3/WP4, and the hub publishes no drop event; Log and Topology JSON are
  docks until WP3 owns the tab set; benchmark is UI-only with no CLI
  extraction; the AddFixture sentence is truthful about the unavailable
  capabilities; the JSON GUI adds line-aware schema checks on top of the
  engine's positionless errors. All are recorded in `design-deviations.md`.

## WP8 detail

- Added `apps/vss_gui/src/support/` (`vss_gui_support`, namespace
  `vss::gui::support`): `build_info`/`environment_report`, `redact_secrets`,
  a bounded `EventLog`, `ui_settings` load/save, the stored ZIP writer, the
  bundle model, the setup facts provider/report, the troubleshooting model,
  and the Setup check, Troubleshoot, Diagnostic bundle, Settings, About,
  Keyboard shortcuts and Help contents dialogs, plus `CheckRowWidget`.
- Setup check reads `StudioBackend::facts()` and a GUI-layer platform probe
  (Windows token elevation, POSIX `geteuid`, `/dev/ptmx`, Linux `dialout`
  membership). Rows are real or "Not checked yet"; com0com instructions open
  the official page, never install; "Restart as administrator" relaunches
  elevated on Windows and asks the shell's unsaved-changes question first.
  The launcher strip and status bar chip now use the same
  `query_setup_report()` provider.
- "Why is nothing arriving?" builds five rows from endpoint state, counters,
  link shape and the tri-state peer value; checks 4 and 5 stay "Not checked
  yet" with an explanation when the engine cannot see a program. The Try this
  card, Copy path, Check again and bundle link are wired.
- Diagnostic bundle: the adapter gained `StudioBackend::traffic(limit)` over
  the engine's bounded ring (both backends), so Traffic contents is a real,
  default-off option. The stored ZIP writer has no dependency; every text file
  is redacted; the archive is written with `QSaveFile`. Capability-gated on
  `capabilities::diagnostics` (now true for this build).
- Settings: General/Appearance/Performance, live apply and QSettings
  persistence for Advanced mode, theme, density, refresh interval (wired to
  the backend's event bridge), default baud rate and `ui/askOnDiscard`.
  `ShellWindow` applies `ui/refreshMs` at startup and connects
  `advanced_changed` to `LevelService`.
- About: four tabs, GPLv3 notice, version/build/compiler/Qt/Boost/OS/
  architecture, copyable environment, licences and the no-bundled-fonts
  statement. Environment text is shared with `environment.txt`.
- Keyboard shortcuts: generated from `ActionRegistry` through
  `shortcut_entries()`; one table, filter, KeyCaps and the Ctrl/Cmd columns.
  Help contents indexes the four support surfaces.
- `ConfirmDialog` gained a multi-choice variant with an explicit cancel
  choice; the ShellWindow unsaved prompt is now the shared three-choice dialog
  (Don't save / Cancel / Save) and honours `ui/askOnDiscard`.
- Undo/Redo: the engine cannot restore endpoint ids and advertised paths, so
  both stay visible, disabled and capability-gated, with the reason in
  `design-deviations.md`; no `QUndoStack` was added.
- Tests: new `vss_gui_support_tests` (registered as CTest `gui_support`) with
  15 cases: setup-report degradation and the dialog, every troubleshooting
  state, ZIP CRC/headers plus a Python `zipfile` round trip, bundle contents
  and the traffic opt-in, redaction, settings persistence/live effect and
  reset, confirm-dialog safety and Escape, About copy, shortcuts from a
  registry, event log and accessible names/24 px targets for every dialog.
  `vss_gui_shell_tests` gained undo gating, registry-to-shortcuts, the
  `ui/askOnDiscard` close path and shared readiness checks (10 total).
- Checks: Debug `ctest` 8/8 (unit, system, headers, `gui_selftest`,
  `gui_backend`, `gui_theme`, `gui_shell`, `gui_support`), clang-format 22 on
  every changed C++ file, `changelog.py --check`. No `libs/vss_core`, JSON
  schema or third-party dependency changed.
- A live theme switch from inside Settings also re-tokenises the dialog footer,
  the Settings nav, the About tile and the troubleshooting separators; the
  support test checks the Settings nav follows the dark accent tokens.
- Deviations: support namespace; Undo/Redo disabled; one shortcuts table;
  Help contents index; setup-check and readiness copy for states the mockup
  did not specify; About adds a Boost row and summarised licences. All are in
  `design-deviations.md` (WP8).
