# WP6: connect, share, network and templates
Model: deepseek-v4-flash (thinking on for the share wizard). Branch: `gui/wp6-connect-share`. Depends on WP2.

## Read first
`AGENTS.md`, `docs/screens/04-connect-share-network.md`, `docs/screens/05-templates-and-advanced-tools.md` (Setups and templates only), `docs/07-copy-deck.md`. Mockups: `Connect.html`, `ConnectJoin.html`, `ShareChoose.html`, `Share.html`, `ShareDone.html`, `Bridge.html`, `Templates.html`.

## Task
1. **Connect ports** with the five modes, per-mode right column, live count line, per-mode result sentences, self-link blocking with visible reasons, the loop refusal path, the two-loopback-ports two-way restriction, and the Advanced owner option for join.
2. **Share wizard** as a custom dialog with the Stepper (three steps, exact copy), name defaults, the Windows free-pair computation and the warn note with "Create another pair" and "Keep the third as Studio only", rollback on failure, and the step 3 table that updates states live.
3. **Put a device on the network** with the TCP, UDP and RFC 2217 forms, server and client roles, URI building and preview, Advanced address editing, firewall note on Windows, "Test from this PC first", validation, and the success toast.
4. **Setups and templates** dialog: four bundled templates (JSON resources validated by `vss_config`), Recent tab from `QSettings`, Browse, Save as template, and opening a template as an unsaved setup.
5. Wire menu, toolbar and launcher entries. Share and bridge move under Topology and Ports (the Scenarios menu is gone).
6. Tests with `FakeStudioBackend`: every mode's validation and sentence, share with 1, 2 and 3 programs and with limited free pairs, URI building for all three transports, template validation (every bundled template loads and validates), keyboard flow and accessible names.

## Constraints
Do not change the JSON schema. Build the URI in the GUI, pass it through unchanged. No literal colours.

## Acceptance
All strings verbatim. A failed share leaves the studio exactly as it was. Every bundled template validates in a unit test.
