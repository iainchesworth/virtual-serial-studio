# AGENTS.md: rules for coding agents working on the Virtual Serial Studio GUI

Copy this file to the repository root as `AGENTS.md` (and as `CLAUDE.md` if your harness reads that name). It is loaded at the start of every session.

## Your job
Implement the redesigned Qt 6 GUI in `apps/vss_gui` from the specs in `docs/gui-design/` (this bundle, copied into the repo by WP0). You are one of several agents, each owning one work package (WP). Stay inside your package.

## You cannot see images
Do not ask for screenshots. Everything you need is text: the specs in `docs/gui-design/docs/`, the tokens in `docs/gui-design/tokens/`, and the HTML mockups in `docs/gui-design/reference/mockups/` (read the markup for exact pixel sizes and structure, never copy the CSS into the product). For visual checks, write offscreen `grab()` PNGs for human review.

## Repository facts (verify each in WP0 and record corrections in `docs/gui-design/implementation-plan.md`)
- Trunk is `develop`. `main` is the release branch. Work on a branch `gui/wp<N>-<slug>` created from the latest `develop`. Never push to `develop` or `main`.
- Stack: C++23, Boost.Asio, CMake 3.21+ with presets, Qt 6 (GUI only). Build with `cmake --preset debug-gui` or `release-gui` (these fail if Qt is missing). Run tests with `ctest --preset debug-gui`. GUI self-test: `QT_QPA_PLATFORM=offscreen vss_gui --selftest`.
- Boost.VirtualSerial is a private dependency, so the repo only builds where that checkout is available. If configuration fails, **stop and report**. Do not stub the dependency or remove it.
- Every first-party source file carries `SPDX-License-Identifier: GPL-3.0-or-later`. Qt is linked dynamically. No other new third-party dependency without an explicit instruction. `Qt6::Svg` is approved.
- Each change adds one fragment in `changelog.d/`; check with `python tools/changelog.py --check`. Read `CONTRIBUTING.md` and `.githooks` before committing and follow the commit-message rules. Respect `.clang-format` and `.editorconfig`.
- Warnings are errors in CI (`VSS_WARNINGS_AS_ERRORS`). CI builds with GCC, Clang, AppleClang and MSVC. Write portable code.
- The project has an in-tree test framework (`tests/`, `vss_unit_tests`). Use it, do not add another.
- `vss::result<T>` is `boost::system::result`. The engine event sink is a `std::function` behind the `vss::EventSink` concept.
- Threading: each endpoint runs on its own strand and events are dispatched on the hub's dispatcher strand. The GUI thread must never be called from, or block on, those threads. Marshal events to the GUI thread with queued connections and coalesce them. `unsubscribe` cannot recall a delivery that is already running, so sinks must capture state that outlives the hub (a `shared_ptr`), never a stack local.
- No fonts are bundled and none may be added.

## Hard rules
1. Read the relevant spec and the existing code before writing. Prefer extending existing patterns over inventing new ones.
2. Use design tokens only. No literal colours, font families or font sizes outside the `Theme` module. Colours and fonts come from `Theme`. Icons come from `IconProvider`.
3. Copy strings exactly from the specs and wrap them in `tr()`.
4. Every widget you add: accessible name and role, keyboard reachable, visible focus ring, both themes, 24 px minimum targets, works at 200 percent text.
5. Do not change `libs/vss_core` behaviour or the JSON topology schema. If a capability is missing, follow `docs/08-engine-gaps.md`: add a capability flag and degrade visibly.
6. Do not copy code, layouts or wording from other products (for example VSPE or Virtual Serial Port Driver).
7. No network access from the GUI, no telemetry, no analytics.
8. Never disable tests, warnings, hooks or lints to get green. Never edit CI to hide a failure.
9. No secrets in the repository or in logs.
10. Do not commit build output, generated QSS, or screenshots.

## Method
1. State a short plan in your first message: files you will touch, tests you will add, risks.
2. Work in small commits (one concern each) with the checks passing at each commit.
3. When the spec and the code disagree, follow the spec, and write the disagreement (and why) in `docs/gui-design/design-deviations.md`.
4. When the spec is silent, choose the option most consistent with `docs/01-principles-and-standards.md`, and record the decision in the same file.
5. Stop and report only if: the build cannot be configured, the task needs engine behaviour changes not authorised in your prompt, or the spec asks for something unsafe (for example silently discarding a user's setup).
6. Finish with a summary: what was built, files changed, tests run with results, deviations, open questions.

## Commands you will use
```
cmake --preset debug-gui
cmake --build --preset debug-gui
ctest --preset debug-gui --output-on-failure
QT_QPA_PLATFORM=offscreen ./build/<dir>/bin/vss_gui --selftest
python tools/changelog.py --check
python docs/gui-design/tools/check_contrast.py
python docs/gui-design/tools/gen_qss.py --out /tmp
```
(Adjust build directories to what the presets actually create.)
