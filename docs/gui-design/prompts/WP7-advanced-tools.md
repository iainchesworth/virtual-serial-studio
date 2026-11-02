# WP7: Advanced tools
Model: deepseek-v4-pro. Branch: `gui/wp7-advanced`. Depends on WP2 (and benefits from WP3 and WP4).

## Read first
`AGENTS.md`, `docs/screens/05-templates-and-advanced-tools.md` (all except Templates and Command palette), `docs/08-engine-gaps.md`, `docs/06-accessibility.md`. Mockups: `Advanced.html`, `Faults.html`, `AddFixture.html`, `JsonView.html`, `Benchmark.html`.

## Task
1. **Owner control**: the topology link bar (policy combo, Release owner, Give to), the Owner and Dropped node chips, dropped counters in Ports and status bar, DROP monitor rows. Uses the owner arbitration capability.
2. **Line wiring and faults dialog** (Inspector link): the matrix per spec, read-only with only the Standard preset unless `Capabilities::lineMatrix` is true, never simulated in the GUI. Fault tiles and history if the fixture exposes injection. If it does not, add the thinnest forwarding method in `vss_studio` (not `vss_core`), with tests, and call it out in the PR. Faults only on fixtures.
3. **Add a mock fixture** dialog.
4. **Topology JSON tab**: editor with line-number gutter, live validation, Format, Revert, Apply, copy as command, export, checks list, and line-aware errors where available (syntax errors from `QJsonDocument` offsets, schema errors without positions hide "Go to line").
5. **Benchmark** dialog. Extract the CLI benchmark core into a shared library function with progress and cancel callbacks only if the maintainer approved in `implementation-plan.md`. Otherwise stub behind a capability and stop at the UI. Block the run when the two ports are linked.
6. Tests: capability gating, owner flow with the fake backend, JSON validation cases (syntax error, unknown mode, duplicate names, loop), benchmark cancel, accessible names, matrix checkboxes labelled "DTR to DSR" and so on.

## Constraints
Do not modify engine semantics. Do not simulate engine features the engine lacks. Mark every degraded control with the tooltip "Not available in this build".

## Acceptance
With a fake backend lacking each capability, the corresponding control is disabled with the tooltip and nothing crashes. JSON validation never applies an invalid document.
