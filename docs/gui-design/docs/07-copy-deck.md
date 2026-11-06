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
| Device | A real serial device on this PC. Studio holds it open; programs never open it. |
| Virtual port | The port a program opens in place of the device. Studio creates it and passes the data between it and the device. |
| Setup | Everything built together: a linked pair, a shared device with its program ports, or an attached device with its virtual port. |
| Linked pair | Two virtual ports wired like a null-modem cable. |
| Link | A connection between ports: two-way, one-way, monitor, split, join, shared bus. |
| Studio only | An in-process port that other programs cannot open. |
| Fixture | A mock device for testing. |
| Sending from | The port a send goes out of. For a fixture the bytes arrive on it, as if the device had sent them, and travel along its links. |
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

## Explaining the device and its port
First-time users do not know that an attached or shared device is taken over by Studio. Say it in these words, everywhere the idea appears:
- Attach, subtitle: "Studio holds the real device open. Your programs connect to a new virtual port that Studio creates, and Studio passes the data between the two."
- Attach, result: "Studio will hold **COM3** open at 115200 baud. Your programs should not open COM3. They open the new virtual port that Studio creates, and Studio passes data between that port and COM3."
- Share, step 1 and 2: "Your programs will open the ports Studio creates, not **COM3** itself." and "You can add another program after sharing, and remove any program's port later with Remove port."
- Never write "I open this port" or any first-person wording that leaves out who opens what.

## Removing
- Setup: title "Remove this setup?", body "This closes <n> ports and <m> links: <names>. A program that has one of these ports open loses it. Nothing is saved until you save the setup.", buttons "Remove setup" and "Cancel".
- Port in a setup: title "Remove <port>?", body "<port> is part of a setup with <n> other ports: <names>. Remove only <port> and its links, or close the whole setup. Nothing is saved until you save the setup.", buttons "Cancel", "Remove only <port>", "Remove the whole setup".
- Program port of a share: the body says "<port> is one of <n> programs sharing a device, in a setup with ..." and "the others keep sharing".
- Port with no links: title "Remove <port>?", body "Remove <port> and its links? Nothing is saved until you save the setup.", buttons "Remove port" and "Cancel".
- Status bar after a setup: "Removed the setup (2 ports and 1 link)."
