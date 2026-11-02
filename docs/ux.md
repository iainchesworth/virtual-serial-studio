# User experience design

This page records who the window is for, how the two modes differ, and the
human-factors reasoning behind the layout. The task-by-task instructions live
in [user-guide.md](user-guide.md).

## Who it is for

Three classes of user share the same engine but not the same vocabulary.

### The operator

"I have a program that needs a serial port and the machine has none."

- Knows the port name their program expects, not link modes or URIs.
- Cares that the port exists, is stable, and can be opened.
- Works in **Simple** mode, ideally without ever seeing the word *topology*.

### The integrator

"I need several programs to share one device, or to feed one program from
several sources."

- Understands split, join and redirection conceptually, but not the library's
  `link_mode` enum.
- Often runs against a test rig and repeats the same setup.
- Uses **Simple** mode for the routine work and **Advanced** mode when wiring
  network transports.

### The diagnostician

"Why is nothing arriving, and which build is this?"

- Reads counters, control lines and the traffic log.
- Wants raw URIs, fixtures, the topology graph and reproducible JSON.
- Uses **Advanced** mode plus `vss_cli --verbose` and the Tracy profiling
  build.

The modes are therefore not "basic" and "pro" feature sets; they are two
vocabularies over the same operations. Every port and link created in simple
mode is a full-strength studio object.

## Principles

1. **Recognition over recall.** Operations are menu items, toolbar buttons or
   dialogs listing the actual ports by name. No URI typing is needed for the
   everyday jobs.
2. **Progressive disclosure.** Simple mode shows the four everyday jobs; expert
   tools appear in one step and the choice is remembered.
3. **Plain language first.** "Two-way", "one-way", "split", "join", "monitor",
   "publisher", "listener". Internal names stay in the code.
4. **Say what will happen.** Every action has a status-bar explanation; the
   Connect dialog prints a sentence describing the resulting wiring.
5. **Prevent rather than report.** The Connect dialog refuses self-links and
   too-few targets before anything is created; the add-port dialog appends
   unique suffixes automatically.
6. **Fail visibly and locally.** Errors name the operation and carry the
   engine's message; they do not close the window or discard the topology.
7. **The keyboard is a first-class path.** `Ctrl+L`, `Ctrl+,`, `F5`, `Ctrl+0`,
   `Delete` and the platform file shortcuts.
8. **Accessible by default.** System colours and fonts, labels on controls,
   state conveyed by text as well as colour, scalable docks and dialogs.
9. **No surprises on exit.** Unsaved work is protected by a prompt that can be
   turned off for scripted sessions.

## The two modes

| Surface | Simple | Advanced |
|---|---|---|
| Toolbar | File actions, Add port, Attach, Connect, Remove | Adds mock, demo, share, fit |
| Ports menu | Add, attach, connect, remove | Adds mock device |
| Scenarios menu | — | Share, join, bridge, null-modem demo |
| Topology menu | Remove link | Adds fit |
| Traffic log dock | Hidden | Shown |
| First-run hint | Shown when the studio is empty | Hidden |
| Status bar | "Simple mode" | "Advanced mode" |

The central topology view, the inspector, send panel and monitor stay visible in
both modes: they are how the user sees that the port is real and moving data.
Advanced mode removes the training wheels, not the feedback.

Switching is one checkable menu item (**View → Advanced mode**) and a checkbox
in Settings. The setting is persisted; the status bar always states the current
mode so a screenshot is self-describing.

### Screen map: simple

```
Ports: add | attach | connect | remove        Inspector: details + lines
┌───────────────┐  ┌──────────────────────┐  ┌──────────────────┐
│ hint (if any) │  │ topology             │  │ Send             │
│ ports table   │  │                      │  │                  │
└───────────────┘  └──────────────────────┘  └──────────────────┘
Monitor (bottom)                         status: Simple mode
```

### Screen map: advanced

The same frame plus the traffic log tab, the Scenarios menu, fixtures and
topology tools. Nothing moves; controls appear.

## Task flows

### Create a port (operator)

Ports → Add virtual port → name, kind, count → the table shows the path → the
hint disappears once a port exists.

Design notes: the name field is pre-filled and the suffix is automatic; the
dialog explains the three kinds in one sentence each; the advertised path is
the column that matters and is stretched to use the space.

### Wire two ports (operator/integrator)

Ports → Connect ports → choose the relationship in plain words → the summary
sentence updates live → Connect.

Design notes: the dialog lists only endpoints that exist, reuses names from the
table, and validates before closing. Failure leaves the dialog's data intact
and reports the engine error.

### Share a device (integrator)

Simple mode: Add port, Connect → Split. Advanced mode adds the URI-driven
share/join/bridge for transports the studio cannot guess.

### Diagnose (diagnostician)

Select a port → Inspector shows state, counters, rates and control lines →
Monitor shows rows → Log (advanced) shows state changes → About → Environment
gives the build.

## Human factors decisions

- **The hint is a sentence, not a modal.** It appears only when the studio is
  empty and simple mode is active, and it disappears as soon as it is no longer
  true. No "welcome wizard" to dismiss.
- **The log is advanced-only.** It is invaluable when diagnosing and noise for
  everyone else; the monitor carries the everyday feedback.
- **The topology stays visible in simple mode.** Hiding it would make links
  invisible and turn a wiring tool into a list; the boxes are the mental model.
- **Mode is global, not per-document.** A user is an operator or a
  diagnostician, not both from file to file.
- **Refresh is adjustable.** 250 ms feels live; busy machines can slow it down
  without changing what the studio does.
- **Theme follows the desktop by default** and can be forced, because lab
  machines and screenshots often need a fixed light or dark surface.
- **No fonts are shipped.** The window inherits the platform UI font, so text
  metrics, scripts and accessibility settings match the rest of the desktop;
  the About window says so explicitly.

## Accessibility

- Every control has a label or tooltip; the status bar repeats the effect of
  the focused action.
- Colour is never the only signal: state is printed as text in the table and
  inspector, and the topology labels its links.
- Shortcuts are standard where the platform has a standard, and single
  modifiers where it does not.
- Dialogs are keyboard-navigable, validate on accept, and keep the user's
  input when validation fails.

## Open design questions

- Should simple mode hide the topology entirely for very small screens, or
  collapse it to a list?
- Should the Connect dialog remember the last mode per user?
- Should advanced mode be discoverable from the first-run hint ("Need more?
  Turn on Advanced mode")?
- How should a "switch" (owner arbitration) be represented — a link mode, or a
  pair of links with an owner indicator?

## Roadmap

1. Guided share/bridge forms (protocol, host, port, listeners) with a URI
   preview, so advanced mode does not require URI recall.
2. Pair manager: create named batches of virtual ports in a table.
3. Line-control matrix editor per link (DTR/DSR/RTS/CTS crossing).
4. Named bundles and baud emulation/throttling.
5. First-class switch with a visible current owner.
6. Undo/redo for topology edits.
7. First-run onboarding as an empty-state panel, replacing the single hint.
