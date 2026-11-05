# 07 Copy deck and voice

Screen-specific strings are in `docs/screens/`. Take them exactly. This page holds the rules and the shared vocabulary.

## Voice
- Plain, short, active. Say what happened, why, and what to do. Sentence case everywhere. No exclamation marks, no jokes, no blame.
- Use Australian English spelling: colour, licence (noun), organise, behaviour.
- No em dashes. Use commas, colons or full stops.
- Name things the user sees: port, program, device, pair, link. Avoid endpoint, URI, link_mode, hub, transport in primary text. They may appear in Details areas that are clearly labelled.
- Port names, paths and addresses are shown in the fixed font and are bold inside result sentences.
- Buttons are verbs or short phrases: Create port, Attach device, Share device, Start bridge, Try again. Never OK or Yes. Cancel always means leave everything unchanged.

## Shared vocabulary
| Term | Meaning |
|---|---|
| Port | Anything a program can open or Studio can route: virtual, real, network, fixture. |
| Program | An application that opens a port. |
| Device | A real serial device on this PC. |
| Linked pair | Two virtual ports wired like a null-modem cable. |
| Link | A connection between ports: two-way, one-way, monitor, split, join. |
| Studio only | An in-process port that other programs cannot open. |
| Fixture | A mock device for testing. |
| Advanced mode | The same window with more tools. Not a separate edition. |

## State words
Open, Studio only, Closed, Failed, Fixture, Setup ready, Needs attention, Passed, Not checked yet. Direction badges: RX, TX, DROP. Switch labels include the state: "DTR on", "RTS off".

## Error template
Title: "Could not <verb> <thing>" (for example "Could not open COM3"). Body: one sentence of likely cause, one sentence of what to do. Buttons: the fix, an alternative, "Copy details". Engine text lives only behind Copy details.

## Implementation rules
- Wrap every string in `tr()` with a per-screen context. No concatenating translated fragments: use placeholders (`%1`) and `%n` plurals. Bold names inside sentences are applied with rich text, but the accessible text must still read as a plain sentence.
- Keep each screen's strings in one place (a `strings` header or a single translation unit per screen) so a copy review is a single diff.
- Dates and numbers use the user's locale. Byte sizes read "1.2 kB", "10.0 MB".
- Placeholders like `<version>` and `<date>` in the specs are values filled at runtime, never literal text.
