# 00 Product brief

## What is being built
Virtual Serial Studio (VSS) is a Qt 6 desktop application over the `vss::studio` engine (C++23, Boost.Asio, built with CMake). It opens one serial port and gives it to many programs, merges publishers, redirects traffic, bridges a device to TCP, UDP or RFC 2217, drives modem control lines, records traffic and saves topologies as JSON. A headless `vss_cli` offers the same operations.

This work redesigns and completes the GUI in `apps/vss_gui` for three levels of user, using the specs in this bundle. The engine stays as it is unless a task says otherwise (see `08-engine-gaps.md`).

## Platforms
- Windows first for the beginner journey. Virtual COM ports come from the optional com0com driver today. Creating a new com0com pair needs administrator rights. A first-party driver is planned but not available.
- Linux and macOS use pseudo terminals (`/dev/pts/N`). Layout and behaviour are identical, only port names and setup-check wording differ.

## People and levels
| Persona | Level | Needs |
|---|---|---|
| Operator | Basic | "My program needs a COM port and this PC has none." A port that exists and its name. Never needs the word topology. |
| Integrator | Intermediate | "Several programs must share one device." Repeats the same rig. Moves between guided forms and addresses. |
| Diagnostician | Advanced | "Why is nothing arriving, and which build is this?" Counters, lines, traffic, raw addresses, faults, reproducible files. |

The application has two modes, **Simple** (default) and **Advanced**. Basic and Intermediate both live in Simple. Advanced adds controls inside the same window. Switching never changes anything already built.

## Stories and where they are specified
| Story | Job in the user's words | Spec |
|---|---|---|
| A1 | "My program needs a COM port and I have none." | screens/01-launcher-and-basic-flows.md |
| A2 | "Is this machine set up properly?" | screens/06-support-and-system.md |
| A3 | "Make two programs talk to each other." | screens/01 |
| A4 | "Test my program with no hardware." | screens/05-templates-and-advanced-tools.md |
| B1 | "Let three programs use my one device." | screens/04-connect-share-network.md |
| B2 | "Feed one program from several sources." | screens/04 |
| B3 | "Only one program should talk at a time." | screens/05 |
| B4 | "Put this device on the network." | screens/04 |
| B5 | "Save this rig and bring it back." | screens/05 and 06 |
| C1 | "What is actually going over the wire?" | screens/02-workspace.md |
| C2 | "Send this exact sequence." | screens/02 |
| C3 | "Does my software react to DTR and CTS?" | screens/05 |
| C4 | "Make the device misbehave." | screens/05 |
| C5 | "Why is nothing arriving?" | screens/06 |
| C6 | "How fast is it, and does it lose data?" | screens/05 |
| C7 | "Give support everything." | screens/06 |
| D1 | "Run this without the window." | screens/05 |

## Sources of truth, in priority order
1. The Markdown in `docs/` (behaviour, copy, rules, acceptance).
2. `tokens/tokens.json` and `tokens/style.qss.in` (colour, type, size).
3. `reference/mockups/*.html`: the visual mockups as HTML and CSS with exact pixel sizes. Open them in a browser, or read the markup. `reference/vss2.css` holds the shared styles. They are a reference for measurements and structure. Never ship them and never port the CSS literally.
4. `icons/*.svg`.

Where a mockup and a doc disagree, the doc wins and the disagreement goes in `docs/design-deviations.md` in the repo.

## Out of scope
New transports, installers, the Windows kernel driver, localisation beyond making strings translatable, and engine rewrites not listed in `08-engine-gaps.md`.
