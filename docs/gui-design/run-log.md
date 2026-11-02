# GUI redesign run log

One row per work package. Branches are local; nothing is pushed. Work
packages merge into `gui/integration` only after the build, test and reviewer
gates pass.

| WP | Branch | Status | Build and tests | Review | Notes |
|---|---|---|---|---|---|
| WP0 | `gui/wp0-recon` | implemented, self-review complete | Debug `ctest` 5/5, Release 6/6, 10 backend tests x40, `vss_gui --selftest` ok, changelog ok, clang-format clean | self-review + human G0; reviewer sessions timed out | bundle installed; implementation plan written; backend + event bridge + 10 tests |
| WP1 | `gui/wp1-theme-widgets` | implemented, self-review complete | Debug `ctest` 6/6, 13 theme/widget tests, gallery PNGs light and dark, contrast/QSS/changelog/format clean | self-review + human G1 | theme, icons, focus ring, widget library, gallery, token tools, CI job |
| WP2 | `gui/wp2-shell` | implemented, self-review complete | Debug `ctest` 7/7, Release `ctest` 8/8, 6/6 shell tests, `vss_gui --selftest` ok, six review PNGs, contrast/QSS/changelog/format clean | self-review; human G1 | action registry, level, launcher, command palette, status bar, dock frame; old shell retained until WP9 |
| WP3 | `gui/wp3-data-panes` | not started | - | - | blocked on G1 |
| WP4 | `gui/wp4-topology` | not started | - | - | blocked on G1 |
| WP5 | `gui/wp5-basic-flows` | not started | - | - | blocked on G1 |
| WP6 | `gui/wp6-connect-share` | implemented, self-review in progress | Debug `ctest` 8/8, 30 dialog tests, 7 shell tests, `vss_gui --selftest` ok, 12 review PNGs | self-review | connect/share/network/templates dialogs, real wiring, Advanced toolbar Share |
| WP7 | `gui/wp7-advanced` | not started | - | - | blocked on G1 |
| WP8 | `gui/wp8-support-system` | not started | - | - | blocked on G1 |
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
