# GUI redesign run log

One row per work package. Branches are local; nothing is pushed. Work
packages merge into `gui/integration` only after the build, test and reviewer
gates pass.

| WP | Branch | Status | Build and tests | Review | Notes |
|---|---|---|---|---|---|
| WP0 | `gui/wp0-recon` | implemented, review pending | `ctest` 5/5, `vss_gui --selftest` ok, changelog ok, clang-format clean | pending | bundle installed; implementation plan written; backend + event bridge + 9 tests |
| WP1 | `gui/wp1-theme-widgets` | not started | - | - | blocked on G0 |
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
  runner): 9 tests covering the fake, unique names, link shapes, owner
  validation, send, control lines, snapshot/apply, coalescing, queue cap,
  unsubscribe and the shutdown race, plus one real-adapter loopback smoke
  test.
- Checks: `ctest --test-dir build/debug-gui` 5/5; `vss_gui --selftest` ok;
  `python tools/changelog.py --check` ok; clang-format clean.
- Environment: Qt 6.10.3, MSVC 19.51, Boost.VirtualSerial sibling checkout.
  The `QT_ROOT` trailing-space trap is recorded in `design-deviations.md`.
