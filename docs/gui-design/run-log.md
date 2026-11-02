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
| WP5 | `gui/wp5-basic-flows` | implemented, self-review complete | Debug `ctest` 8/8, 20/20 flow tests (one real-engine loopback smoke), warnings-as-errors build, `vss_gui --selftest` ok, contrast/QSS/changelog/format clean | self-review | add/ready/pair/attach/watch; pair name and elevation deviations recorded |
| WP6 | `gui/wp6-connect-share` | not started | - | - | blocked on G1 |
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
