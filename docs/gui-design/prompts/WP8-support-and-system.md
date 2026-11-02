# WP8: support and system
Model: deepseek-v4-flash (thinking on for the zip writer and the undo stack). Branch: `gui/wp8-support-system`. Depends on WP2.

## Read first
`AGENTS.md`, `docs/screens/06-support-and-system.md`, `docs/08-engine-gaps.md`, `docs/07-copy-deck.md`, `docs/06-accessibility.md`. Mockups: `SetupCheck.html`, `Troubleshoot.html`, `Bundle.html`, `Feedback.html`, `Settings.html`, `About.html`, `Shortcuts.html`.

## Task
1. **Setup check** dialog and the facts provider used by the launcher strip and the status bar chip, with Windows and Linux or macOS variants. Install com0com opens an instructions dialog (never installs silently). Restart as administrator relaunches elevated on Windows only.
2. **Why is nothing arriving** checklist driven by endpoint state, counters and events, with honest "Not checked yet" rows when the engine cannot know (see `PeerState`).
3. **Diagnostic bundle** with a small dependency-free stored (uncompressed) ZIP writer in the GUI layer, unit tested (CRC32, headers, round trip with Python's `zipfile` in a test or a documented manual check). Traffic contents default off.
4. **Feedback patterns**: wire `ErrorDialog`, `ConfirmDialog`, toasts and the loop, failed port and remove patterns per spec into the real operations.
5. **UndoService** (`QUndoStack`), snapshot-based restore verified exact. Wire Undo and Redo into removal and the toast "Undo". If restore cannot be made exact, leave Undo out, keep confirmations, and record why in `design-deviations.md`.
6. **Settings** dialog (General, Appearance, Performance), changes applied immediately through the services from WP1 and WP2, refresh interval wired to the event bridge.
7. **About** (four tabs, Environment copy) and **Keyboard shortcuts** generated from the `ActionRegistry`.
8. Tests: facts provider with fake platform inputs, checklist logic for each failing check, ZIP validity, undo exactness on remove, settings persistence and live effect, shortcut list matches the registry, accessible names.

## Constraints
No network access. No silent installs or privilege changes. Never include traffic in a bundle unless the user ticked it.

## Acceptance
Every string verbatim. The bundle opens in a standard ZIP tool. Undo restores ports, links and names exactly, or is absent with a recorded reason.
