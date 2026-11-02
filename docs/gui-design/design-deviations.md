# Design deviations and recorded decisions

The design bundle is the contract. Where the bundle is silent, or where its
wording conflicts with the repository, the choice and its reason are recorded
here. Entries name the work package that owns the decision.

## WP0

### Deviations file path

`docs/09-qa-and-definition-of-done.md` says `docs/design-deviations.md`;
`AGENTS.md`, WP0 and WP9 say `docs/gui-design/design-deviations.md`. This file
uses the bundle path. The QA reference is treated as a typo.

### GUI logic tests use a Qt-aware runner

WP1's prompt says to test with the in-tree framework. The in-tree framework in
`tests/framework` is deliberately Qt-free and has no `QCoreApplication`. The
backend has signals and a `QTimer`, so its tests need an application object.
WP0 added `vss_gui_backend_tests`, which reuses the in-tree `TEST_CASE` and
`REQUIRE` macros and provides a small `QCoreApplication` runner. Widget-level
checks stay in `vss_gui --selftest`. No new third-party test framework is
added, so the intent of the rule is met.

### No per-write callback from the engine thread

The design says failures must be visible locally. A per-write completion
callback would have to touch a `QObject` from the endpoint strand and risks a
use-after-free when the backend is destroyed with a write pending. The adapter
therefore returns an immediate error for a missing or closed port, queues the
write, and surfaces a queue-full rejection as the engine's error event through
the coalesced batch. The Send panel can show "queued" and the event; it does
not need a per-write signal.

### Integration branch

Work packages merge into `gui/integration`, not `develop`, until WP9. The
design replaces the shell and leaves placeholders between WP2 and WP8, so
merging each package to trunk would make `develop` unusable for the duration.
This is a delivery decision, not a change to the design.

### Qt version pin for builds

The machine has Qt 6.10.3 under `C:\Qt` and Qt 6.12 under
`C:\Repos\common\qt`. A `QT_ROOT` value with a trailing space makes CMake miss
6.10.3 and silently select 6.12; the binary then imports
`qt_version_tag_6_12` and fails to start against a 6.10 runtime. All WP0
builds pin Qt 6.10.3 with both `QT_ROOT` (no trailing space) and
`-DCMAKE_PREFIX_PATH`, and the same instruction is carried into the later
work packages. This is an environment trap, not a product decision.
