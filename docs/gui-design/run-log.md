# GUI redesign run log

One row per work package. Branches are local; nothing is pushed. Work
packages merge into `gui/integration` only after the build, test and reviewer
gates pass.

| WP | Branch | Status | Build and tests | Review | Notes |
|---|---|---|---|---|---|
| WP0 | `gui/wp0-recon` | implemented, self-review complete | Debug `ctest` 5/5, Release 6/6, 10 backend tests x40, `vss_gui --selftest` ok, changelog ok, clang-format clean | self-review + human G0; reviewer sessions timed out | bundle installed; implementation plan written; backend + event bridge + 10 tests |
| WP1 | `gui/wp1-theme-widgets` | implemented, self-review complete | Debug `ctest` 6/6, 13 theme/widget tests, gallery PNGs light and dark, contrast/QSS/changelog/format clean | self-review + human G1 | theme, icons, focus ring, widget library, gallery, token tools, CI job |
| WP2 | `gui/wp2-shell` | not started | - | - | blocked on WP1 |
| WP3 | `gui/wp3-data-panes` | not started | - | - | blocked on G1 |
| WP4 | `gui/wp4-topology` | not started | - | - | blocked on G1 |
| WP5 | `gui/wp5-basic-flows` | not started | - | - | blocked on G1 |
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
