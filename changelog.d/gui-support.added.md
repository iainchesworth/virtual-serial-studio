Added the redesigned support and system surfaces to the GUI:

- Setup check with the shared facts provider used by the launcher strip and the
  status bar chip. Driver/com0com, elevation, pseudo-terminal and dialout rows
  report a real platform probe where one exists, and "Not checked yet" where it
  does not. "Install com0com" opens instructions and never installs anything;
  "Restart as administrator" relaunches elevated on Windows only.
- "Why is nothing arriving?" with five checks driven by endpoint state,
  counters and the tri-state peer value. It never claims what the engine cannot
  see: checks 4 and 5 say "Not checked yet" and explain.
- A dependency-free stored ZIP writer and the diagnostic bundle: setup, event
  log, environment and counters on by default, traffic contents off unless
  ticked, and credentials redacted from every text file. The archive opens in a
  standard ZIP tool (checked with Python's `zipfile`).
- Settings with General, Appearance and Performance sections. Advanced mode,
  theme, density, refresh interval, default baud rate and the unsaved-setup
  prompt apply immediately and persist in QSettings.
- About with the GPLv3 notice, version, build, compiler, Qt, Boost, operating
  system and architecture, plus Licences and Fonts tabs and a copyable
  environment block.
- Keyboard shortcuts generated from the `ActionRegistry`, a help index for the
  support screens, the shared three-choice `ConfirmDialog` for unsaved changes
  (honouring `ui/askOnDiscard`), and an event log retained for the bundle.
- Undo and Redo stay visible but capability-gated disabled: a re-applied
  snapshot changes endpoint identifiers and advertised paths, so exact restore
  cannot be proven.

Added `vss_gui_support` with 15 logic and dialog tests, registered as
`gui_support`, covering setup-report degradation, the troubleshooting states,
ZIP validity and the Python round trip, bundle contents and the traffic opt-in,
redaction, settings persistence and live effect, confirm-dialog safety, the
registry-generated shortcut list and accessible names.
