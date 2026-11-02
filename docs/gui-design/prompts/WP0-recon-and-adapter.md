# WP0: recon and backend adapter
Model: deepseek-v4-pro (thinking on). Branch: `gui/wp0-recon`.

## Read first
`AGENTS.md`, `docs/00-product-brief.md`, `docs/03-information-architecture.md`, `docs/08-engine-gaps.md`.

## Task
1. **Install the bundle.** Copy this bundle into the repo at `docs/gui-design/` (docs, tokens, icons, tools, prompts, reference). Keep `agents/` out of the repo. Do not alter the contents.
2. **Inventory the current GUI.** Read everything under `apps/vss_gui` and the public API of `libs/vss_studio`, `libs/vss_core`, `libs/vss_config` and `libs/vss_ports`. Write `docs/gui-design/implementation-plan.md` containing: (a) a table of existing GUI classes and files with one line each; (b) the real engine API names for every operation the specs need; (c) the capability table from `docs/08-engine-gaps.md`, with every row re-verified against the code and marked Present, Partial or Missing, with the file and symbol as evidence; (d) the module layout you propose for the new GUI (directories for `theme`, `widgets`, `shell`, `panes`, `topology`, `dialogs`, `support`, `backend`), the CMake pattern (one fragment per area), and the namespace and naming conventions already used in the repo; (e) how the existing `--selftest` works and how to extend it; (f) risks and questions for the maintainer.
3. **Create the adapter.** Add a narrow `StudioBackend` interface (match repo naming conventions), a `Capabilities` struct, a real implementation over `vss::studio`, and a `FakeStudioBackend` for tests. Operations: create and remove ports, list devices, attach, links (all modes and policies), owner set and release, send, snapshot and apply, events as Qt signals delivered on the GUI thread and coalesced by a refresh interval, control lines, fixtures (if present), setup facts. Return `PeerState` as a tri-state. No UI work.
4. **Thread bridge.** Implement the engine-to-GUI event bridge with a `shared_ptr` state object that outlives the hub, queued delivery, coalescing by interval, and a clean shutdown. Test it for the shutdown order described in `docs/architecture.md` of the repo.

## Constraints
No visual changes. Do not change `vss_core`. Do not add dependencies. If the build cannot be configured, stop and report.

## Acceptance
Plan document complete and specific (symbols, not guesses). Adapter and fake compile on the CI compilers. Unit tests for the fake backend and the event bridge, including a shutdown race test. Existing tests and self-test unchanged and green. Changelog fragment added.
