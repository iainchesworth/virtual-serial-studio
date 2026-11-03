# WP2: main window shell, actions, command palette and launcher
Model: deepseek-v4-pro (thinking on). Branch: `gui/wp2-shell`. Depends on WP1.

## Read first
`AGENTS.md`, `docs/03-information-architecture.md`, `docs/screens/01-launcher-and-basic-flows.md` (Launcher section only), `docs/screens/05-templates-and-advanced-tools.md` (Command palette section only), `docs/06-accessibility.md`. Mockups: `Main.html`, `Workspace.html`, `Compact.html`, `Palette.html`.

## Task
1. **ActionRegistry.** Every menu, toolbar and palette action registers once with: id, text, keywords, icon, shortcut (standard keys, Ctrl is Cmd on macOS), required level (Simple or Advanced), enabled predicate, and a handler. Menus and the toolbar are built from it. Menu items needing Advanced are hidden in Simple but remain findable in the palette.
2. **Main window frame.** Menu bar, toolbar (icon plus text, 48 high, icon-only at the 960 by 640 minimum with a More overflow), status bar, dock grid with the sizes and minimums in `tokens.json`, `saveState` and `restoreState`, Reset layout. The dock contents are empty placeholders that later packages fill. Compact behaviour: the right column becomes one tabbed dock.
3. **Level service** (Simple and Advanced): persisted in `QSettings` (`ui/advanced`), switchable from View, Settings (later), the status bar segmented switch and Ctrl+Shift+A, signals to every pane, no layout reset on switch.
4. **Launcher** per spec: readiness strip driven by a setup-facts provider from the adapter (stub the facts if WP8 has not landed, behind an interface), four LauncherCards, saved setups row from recents, Advanced hint link. Shown while the studio is empty and from View, Show launcher.
5. **Command palette** per spec, built on the ActionRegistry, with "Available now" and "Needs Advanced mode" groups, and Enter on a hidden action turning Advanced on and running it.
6. **Title bar and dirty state**: `*` while unsaved. Closing, File New and File Open use the unsaved prompt from `ConfirmDialog`, honouring `ui/askOnDiscard`.
7. **Status bar** per spec with live fields wired to adapter signals (rates and counts can read zero until WP3), readiness chip, level switch.
8. Tests: action registration and level visibility, palette filtering and grouping, launcher shown and hidden by studio state, layout persistence round trip, selftest additions, accessible names for every menu item and toolbar button, shortcut uniqueness.

## Constraints
Do not build the ports table, inspector, monitor or topology. Use WP1 widgets and `Theme` only. No literal colours.

## Acceptance
The window launches into the launcher when empty. Every action has a menu item and a unique shortcut. Ctrl+K lists hidden Advanced actions. Mode switching is instant and loses nothing. Keyboard tab order follows `docs/06-accessibility.md`.
