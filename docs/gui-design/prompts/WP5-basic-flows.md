# WP5: basic flows
Model: deepseek-v4-flash. Branch: `gui/wp5-basic-flows`. Depends on WP2.

## Read first
`AGENTS.md`, `docs/screens/01-launcher-and-basic-flows.md` (all except Launcher), `docs/07-copy-deck.md`, `docs/04-components.md`. Mockups: `AddPort.html`, `GivePort.html`, `Pair.html`, `Attach.html`, `Watch.html`.

## Task
Build, with WP1 widgets and `Theme`, these dialogs exactly as specified (strings, defaults, validation, result sentences, variants):
1. **Add a port** with PickCards, inline settings per selection, Advanced expansion of More types, dynamic primary button text.
2. **Port ready** with the copyable path, live waiting or connected note (respect the `PeerState` rule), the details disclosure and its default rules, and the Linux, macOS and no-driver variants.
3. **Connect two programs** (pair) including diagram row, suggested free COM names, the Windows admin note, the disabled state without a driver, and elevation failure handling through `ErrorDialog`.
4. **Attach a real device** with enumeration on open, Refresh, unavailable rows with visible reasons, settings section collapse rules, address preview and Advanced edit.
5. **Watch traffic** creating the loopback endpoint and the monitor link through the adapter, then focusing the Monitor on that port.
Wire the launcher cards and the toolbar and menu actions to open them.
6. Tests: validation messages and unique-name suffixing, result sentences for singular and plural, variant selection by platform and capabilities (use `FakeStudioBackend`), keyboard flow and focus order, accessible names, theme switch.

## Constraints
Exact copy. `tr()` with one strings unit per dialog. Initial focus on the first field. Enter is primary, Escape cancels. Do not touch the engine.

## Acceptance
Every string in the spec appears verbatim. No dialog clears input on validation failure. Dialogs pass the 24 px target and accessible-name walk.
