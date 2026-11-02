# Orchestration guide (for you, the human)

## Order and parallelism
```
WP0 recon and adapter ──► WP1 theme and widgets ──► WP2 shell, actions, palette, launcher
                                                        │
        ┌───────────────┬───────────────┬──────────────┼───────────────┬───────────────┐
        ▼               ▼               ▼              ▼               ▼               ▼
   WP3 data panes   WP4 topology   WP5 basic flows  WP6 connect,    WP7 advanced    WP8 support
   (ports, inspector (graphics      (add port,       share, bridge,  tools           and system
    send, monitor)    view)          ready, pair,     templates                       (setup check,
                                     attach, watch)                                    troubleshoot,
                                                                                       bundle, settings,
                                                                                       about, shortcuts, undo)
        └───────────────┴───────────────┴──────────────┴───────────────┴───────────────┘
                                                        ▼
                                              WP9 accessibility and QA pass
```
WP0, WP1 and WP2 are strictly sequential. WP3 to WP8 can run in parallel once WP2 is merged, because each owns separate directories. WP9 runs last.

## Rules that keep parallel agents from colliding
- One git branch and one `git worktree` per agent: `git worktree add ../vss-wp5 -b gui/wp5-basic-flows develop`.
- WP0 defines the module layout and the CMake pattern (one `CMakeLists.txt` fragment per area). Later agents add only their own fragment and a single line to include it.
- Shared registries (`ActionRegistry`, `strings`, resources) take append-only edits. Merge in package order and rebase before merging.
- Merge each WP only after the reviewer prompt passes (see `REVIEW-checklist-prompt.md`) and CI is green.
- Removal flows start with a confirmation only. WP8 adds the Undo service and wires Undo into removal.

## Model assignment (DeepSeek V4)
| Package | Model | Why |
|---|---|---|
| WP0, WP2, WP4, WP9, reviews | `deepseek-v4-pro`, thinking on | Architecture, cross-cutting rules, tricky graphics and accessibility. |
| WP1 | `deepseek-v4-pro`, thinking on | Everything else depends on it. |
| WP3, WP7 | `deepseek-v4-pro` | Threading and engine-adjacent code. |
| WP5, WP6, WP8 | `deepseek-v4-flash` (thinking on for the share wizard and the zip writer) | Spec-driven, repetitive dialogs. |
See `agents/deepseek/README.md` for configuration.

## Session discipline
- One work package per session. Start a fresh session for each. Do not carry old context.
- Give each session `AGENTS.md`, the preamble, its own prompt, and let it read only the docs the prompt names. Do not paste the whole bundle.
- After each session: run the build and tests yourself, then run the reviewer prompt in a fresh session with the diff.

## Gates before WP3 to WP8 may start
1. WP2 merged and CI green.
2. `docs/gui-design/implementation-plan.md` exists and its capability table has been read by a human.
3. The widget gallery opens in light and dark.
