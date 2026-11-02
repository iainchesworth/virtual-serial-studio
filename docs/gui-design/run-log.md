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
| WP5 | `gui/wp5-basic-flows` | implemented, self-review complete | Debug `ctest` 8/8, 20/20 flow tests (one real-engine loopback smoke), warnings-as-errors build, `vss_gui --selftest` ok, contrast/QSS/changelog/format clean | self-review | add/ready/pair/attach/watch; pair name and elevation deviations recorded |
| WP6 | `gui/wp6-connect-share` | implemented, self-review complete | Debug `ctest` 8/8, 30 dialog tests, 7 shell tests, `vss_gui --selftest` ok, 12 review PNGs | self-review | connect/share/network/templates dialogs, real wiring, Advanced toolbar Share |
| WP7 | `gui/wp7-advanced` | implemented, self-review complete | Debug `ctest` 8/8, 15 advanced tests, 16 review PNGs, changelog/format/contrast/QSS clean | self-review | owner bar, wiring and faults, mock fixtures, topology JSON, event log, benchmark, export command |
| WP8 | `gui/wp8-support` | implemented, self-review complete | Debug `ctest` 8/8, 15 support tests, 10 shell tests, `vss_gui --selftest` ok, Python `zipfile` round trip, contrast/QSS/changelog/format clean | self-review; human G2 pending | setup check, troubleshooting, bundle, settings, About, shortcuts, help index, confirm dialog; undo/redo disabled with a recorded reason |
| WP9 | `gui/wp9-qa` | implemented, self-review complete | Debug `ctest` 14/14 including `gui_selftest_200`, 24-state dialog accessibility walk, 52 review PNGs, Release 15/15, contrast/QSS/changelog/format clean | self-review; pending G3 | accessibility selftest, 2x scale, high contrast/reduced motion, PNG set, docs, legacy sources removed; residual capability gaps recorded |

## Integration review (G2)

- `gui/integration` now carries WP3, WP4, WP5, WP6, WP7 and WP8 as
  no-fast-forward merges. The only later integration commit connects the
  Inspector's troubleshooting and line-matrix links, the Port ready setup and
  connect actions, the launcher's Share job and the Add port/Pair setup links
  to the tools those packages delivered.
- Debug: `ctest --preset debug-gui` 13/13 (unit, system, headers,
  `gui_selftest`, `gui_backend`, `gui_theme`, `gui_shell`, `gui_panes`,
  `gui_topology`, `gui_advanced`, `gui_support`, `gui_dialogs`, `gui_flows`);
  `vss_gui --selftest` ok.
- Release: `ctest --test-dir build/release-gui` 14/14 including `perf_smoke`;
  `vss_gui --selftest` ok. Contrast `FAILURES: 0`; QSS renders both themes;
  changelog check ok; clang-format clean on the changed integration sources.
- Review PNGs (never committed), all with `QT_QPA_FONTDIR=C:/Windows/Fonts`:
  `build/debug-gui/gui-shots/integration-{light,dark}.png`,
  `integration-advanced-{light,dark}.png` and
  `integration-compact-light.png`, plus the WP5-WP8 dialog sets.
- Known gaps taken into G2: Log and Topology JSON are separate Advanced docks
  rather than Monitor tabs (the in-panel placeholder tabs are hidden);
  real-device templates stay disabled until a template device picker is
  designed, although WP5 Attach now exists; network server, peer-open, line
  matrix, fault injection and benchmark extraction remain capability-gated;
  real com0com/elevation and real serial hardware were exercised only through
  the fake backend; setup-check chip click-through and the 200 percent
  accessibility walk belong to WP9.

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

## WP6 detail

- Added `apps/vss_gui/src/dialogs/`: `ConnectPortsDialog` (five modes, per-mode
  right column, live count line, per-mode result sentences, self-link and
  two-loopback refusal, engine cycle refusal, Advanced owner policy),
  `ShareWizard` (three custom steps, device list, device settings, free-pair
  plan, warn note with Create another pair and Keep the third as Studio only,
  `share_serial` rollback, live Ready table, Copy, Open monitor),
  `NetworkEndpointDialog` (TCP, UDP and RFC 2217, client and capability-gated
  server roles, listen/address fields, URI and command preview, Advanced
  address override, known bind-cause copy) and `TemplatesDialog` (four bundled
  templates, search, Recent from QSettings, Browse, Save current setup as
  template, Use template as an unsaved setup).
- Added the pure helpers `network_uri` and `share_plan`, the `recent_files`
  QSettings helper and four validated template resources in `templates.qrc`.
- Backend additions in the GUI adapter only: `capabilities::network_server`
  (false), `capabilities::com0com_pair_creation` (false off Windows) and
  `StudioBackend::create_com0com_pair()` implemented by both adapters. The
  fake gained `set_free_pairs`, `fail_share_listener` and a Windows-style
  share path for the limited-pair tests. No library or JSON schema changes.
- Shell wiring: `topology.connect`, `topology.share`, `ports.network` and
  `file.templates` now open the real dialogs; `run_job` routes `connect`,
  `share`, `network` and `templates`; the new Advanced toolbar-only
  `topology.shareToolbar` action opens the wizard without changing the Simple
  Topology action. Failed share and bridge attempts run with the shell's
  loading guard so transient rollback signals do not mark the setup dirty.
  Connect success shows a toast, bridge success shows "Bridge is running on
  port N." with Open monitor, and templates open with the unsaved `*`.
- Extended `PickCard` (trailing glyph, fixed vertical policy), `NoteBanner`
  and `ResultSentence` (rich text with a plain accessible sentence),
  `ActionRegistry` (`palette` placement flag), and the QSS template
  (`secondary`, `dashed`, `spin` and dialog footer rules).
- Added `vss_gui_dialogs_tests` (30 cases) registered as `gui_dialogs` in
  `tests/CMakeLists.txt`: every connect mode, all result sentences, self-link,
  two-loopback and cycle refusals, owner policy, share plan/free pairs and
  warning copy, share with 1, 2 and 3 programs, limited pairs, mid-share
  rollback, definite peer states, network source, settings URI, URI building
  and validation for all transports, bind failure, address override,
  templates validation/round-trip, Attach-required templates, open/save/recent,
  search, accessible names, 24 px buttons, Escape/Enter flow and review PNGs.
- Review PNGs (never committed): `build/debug-gui/gui-shots/connect-*.png`,
  `share-step1-*.png`, `share-step2-*.png`, `share-step3-*.png`,
  `network-*.png`, `templates-*.png`, both themes.
- Checks: Debug `ctest` 8/8, 30/30 dialog tests, 7/7 shell tests,
  `vss_gui --selftest` ok, changelog ok, clang-format clean, contrast
  `FAILURES: 0`, QSS renders both themes.
- Deviations and incomplete items: network server role is not in the engine
  and is disabled with a visible reason; the share wizard's Create another
  pair uses a new GUI adapter method; real-device templates require the WP5
  Attach flow and are disabled with an explicit note; peer-open is unknown in
  the real backend so step 3 shows "Open" and omits the waiting sentence;
  Add another program re-runs the one-step share; the error pattern is inline;
  Show the loop is WP4 work. All are recorded in `design-deviations.md`, with
  the network and pair-creation rows added to `08-engine-gaps.md`.

## WP5 detail

- Added `apps/vss_gui/src/flows/`: the common dialog chrome (title, subtitle,
  scrollable body, sunken footer, 760 px width and screen-height cap), Add a
  port, Port ready, Connect two programs with the shared linked-pair form,
  Attach a real device and Watch traffic. Each dialog calls `StudioBackend`
  only and never claims a port or link that the adapter did not create.
- Add a port: the three PickCards plus the Advanced network/mock placeholders,
  dynamic primary text, name with silent unique suffixing, How many validation
  ("Choose a number from 1 to 32."), the three Kind choices, and singular/plural
  result sentences. It creates 1 to 32 real ports atomically, rolls back on the
  first failure and opens Port ready for the first one.
- Port ready: copyable path with "Copy name" and the exact "<path> copied."
  toast, the Windows/Linux/macOS title variants, the no-driver Studio-only
  warning with Open setup check, the live Waiting/Program connected/Open rule
  from `PeerState` and `Capabilities::peer_open`, the remembered Show details
  disclosure with the JSON and CLI code blocks, and the three footer actions.
- Connect two programs: diagram, pair name, real free-COM suggestions, result
  sentence, truthful Windows note, driver/insufficient-pair warning, Advanced
  line wiring (only Standard selectable), and rollback plus `ErrorDialog` for
  permission or link failures.
- Attach: enumeration on open, Refresh, available/in-use/virtual rows with the
  reason as visible text, empty state, manual path, collapsed Simple settings
  with a one-line summary, the full framing set, capture, address preview and
  the Advanced address override. A successful attach opens the device once,
  creates an automatic virtual port and splits the device to it; failures show
  "Could not open <path>" with Try again, Choose another and Copy details.
- Watch: port picker preselected from the current selection, live
  `<port>-monitor` name, the read-only explanation and Studio-only card,
  loopback watcher creation, `monitor` link and rollback if either fails.
- Wiring: `ports.add`, `ports.virtual`, `ports.pair`, `ports.attach` and
  `topology.watch` and the launcher's Add/pair/watch cards now open the real
  dialogs. The Port ready actions for Connect ports and Setup check remain the
  WP6/WP8 status placeholders, and Watch selects the source and focuses the
  Monitor dock (the filter waits for WP3's monitor model).
- Widget/backend additions: `device_view::in_use` (engine leaves false),
  `ResultSentence` rich-plus-plain text, `ErrorDialog::add_action`, and the
  `vss_gui_flows` library. No `vss_core` or JSON schema change.
- Added `vss_gui_flows_tests` (20 tests): validation and unique suffixes,
  create/cancel/no-op, singular/plural sentences, ready variants and copy,
  peer capability and live update, pair success/no-driver/elevation failure,
  attach enumeration/refresh/empty/manual/address/error recovery, watch
  success/error/capability, accessible names and 24 px targets across all five
  dialogs, Enter/Escape, both themes and ShellWindow/launcher wiring. One test
  uses the real `EngineStudioBackend` to create and remove a loopback port.
- Review PNGs (never committed): `build/debug-gui/gui-shots/`
  `{add-port,add-pair,pair,attach,watch,ready}-{light,dark}.png`.
- Checks: Debug `ctest` 8/8; `VSS_WARNINGS_AS_ERRORS=ON` build clean;
  `python tools/changelog.py --check`; clang-format clean; contrast
  `FAILURES: 0`; `gen_qss.py` writes both theme previews.
- Deviations: pair names read-only because the adapter cannot choose an
  advertised driver name; no new com0com pair or elevation, so the admin note
  is replaced with a truthful info note; the pair needs two free com0com
  pairs; Attach composes physical plus virtual plus split; in-use detection is
  adapter/fake supplied; Watch filter waits for WP3; pair success uses the
  status bar instead of a second ready screen. All are in
  `design-deviations.md` under WP5.

## WP9 detail

- Added `apps/vss_gui/src/shell/accessibility.cpp`: the selftest now constructs
  the main window and 24 dialog states (22 classes; the Share wizard at each
  step) against `FakeStudioBackend`, shows each in
  light and dark, and checks every visible enabled focusable control for an
  accessible name, a 24 by 24 target, keyboard focus and clipped plain text.
  Composite scroll and tab containers are skipped so their children are not
  double-counted.
- Added `gui_selftest_200` in `tests/CMakeLists.txt` with
  `QT_SCALE_FACTOR=2;QT_FONT_DPI=192`; the same walk runs at 200 percent.
  Windows CTest runs set `QT_QPA_FONTDIR=C:/Windows/Fonts` so text metrics are
  real; without a font database the clipping rule is skipped rather than
  guessed.
- Fixed the target rule at the source: `QToolButton`, `QCheckBox` and
  `QRadioButton` now have a 24 px minimum width in the token stylesheet, so
  icon-only stepper arrows and card indicators meet the minimum.
- `Theme::high_contrast()` and `VSS_HIGH_CONTRAST=1` drop the custom stylesheet
  for the platform palette; the selftest asserts the stylesheet is empty and
  that `VSS_REDUCE_MOTION=1` makes transitions zero-duration.
- `VSS_QA_SHOTS=<dir>` writes 52 offscreen images: main window light, dark,
  compact light and compact dark, plus every dialog in both themes. They stay
  under `build/gui-shots/` and are not committed.
- Removed the 23 legacy flat GUI files (`main_window`, the old models/panels,
  dialogs and `theme`); `vss_gui` now has only `main.cpp` at the executable
  level and links the redesigned libraries.
- Updated `docs/user-guide.md` and `docs/ux.md`: removed the guided
  share/bridge, line-matrix, owner-switch and Undo items from "Not yet", and
  added the honest remaining limits (network server, template device picker,
  hardware probes).
- Checks: Debug `ctest` 14/14, Release 15/15, both `gui_selftest` and
  `gui_selftest_200` pass, contrast `FAILURES: 0`, QSS renders both themes,
  changelog and clang-format clean. Residual gaps are recorded in
  `design-deviations.md` under WP9.
