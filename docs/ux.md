# User experience design

This page records who the window is for, how the two levels differ, and the
human-factors reasoning behind the layout. The task-by-task instructions live
in [user-guide.md](user-guide.md). The screen-by-screen specifications are in
[the screen specifications](https://github.com/iainchesworthlabs/virtual-serial-studio/blob/develop/docs/gui-design/docs/00-product-brief.md).

## Who it is for

Three classes of user share the same engine but not the same vocabulary.

### The operator

"I have a program that needs a serial port and the machine has none."

- Knows the port name their program expects, not link modes or URIs.
- Cares that the port exists, is stable, and can be opened.
- Works at the **Simple** level, ideally without ever seeing the word
  *topology*.

### The integrator

"I need several programs to share one device, or to feed one program from
several sources."

- Understands split, join and redirection conceptually, but not the library's
  `link_mode` enum.
- Often runs against a test rig and repeats the same setup.
- Uses **Simple** for the routine work and **Advanced** when wiring network
  endpoints or owner policies.

### The diagnostician

"Why is nothing arriving, and which build is this?"

- Reads counters, control lines and the traffic log.
- Wants raw addresses, fixtures, the topology JSON and reproducible commands.
- Uses **Advanced** plus `vss_cli --verbose`, the diagnostic bundle and the
  Tracy profiling build.

The levels are therefore not "basic" and "pro" feature sets; they are two
vocabularies over the same operations. Every port and link created at the
Simple level is a full-strength studio object, and switching never changes what
has been built.

## Principles

1. **Start from the job, not the mechanism.** The empty window is a launcher
   with four jobs: give my program a port, connect two programs, share one
   device, watch traffic. Each ends by saying what to open in the other
   program.
2. **Recognition over recall.** Operations are menu items, toolbar buttons or
   dialogs that list the actual ports by name. No address typing is needed for
   the everyday jobs, and the command palette (`Ctrl+K`) finds any action by
   its words.
3. **Progressive disclosure.** Simple shows the everyday jobs. Advanced adds
   panels, columns and options to the same window, and a dashed "More options"
   row in a dialog says what is there and how to turn it on.
4. **Plain language first.** "Two-way", "one-way", "split", "join", "watch",
   "linked pair". Internal names stay in the code.
5. **Say what will happen.** Every dialog prints a sentence describing the
   result, with the real names in bold, before the button is pressed.
6. **Prevent rather than report.** Dialogs refuse self-links, loops and too few
   targets before anything is created, and the add-port dialog appends unique
   suffixes silently.
7. **Fail visibly and locally.** Errors name the operation and carry the
   engine's message behind "Copy details"; they do not close the window or
   discard the setup. A wizard that fails part-way removes what it created.
8. **Degrade, never pretend.** When the engine cannot do something, the control
   is shown disabled with "Not available in this build", or the simpler variant
   is shown. The list is in
   [08-engine-gaps.md](https://github.com/iainchesworthlabs/virtual-serial-studio/blob/develop/docs/gui-design/docs/08-engine-gaps.md).
9. **The keyboard is a first-class path.** Every action has a menu item, and
   the shortcut list is generated from the same action registry as the menus.
10. **Accessible by default.** Names and roles on every control, a visible focus
    ring, 24 pixel targets, both themes and 200 percent text, checked by a
    test that walks every dialog.
11. **No surprises on exit.** Unsaved work is protected by a prompt that can be
    turned off for scripted sessions.

## The two levels

| Surface | Simple | Advanced |
|---|---|---|
| Menus | Same menus; some items hidden | Adds Network endpoint, Mock fixture, Export as command, Link wiring, Owner, Fit, Benchmark, Log, Topology JSON |
| Toolbar | New, Open, Save, Add port, Attach, Connect, Remove, search | Adds Mock device, Share, Fit |
| Ports dock | Three-line rows: name and state, role, path with copy | A table with RX, TX, Dropped and a filter |
| Inspector | State, path, traffic, control lines read only | DTR and RTS switches, Break |
| Send | Text or hex, Add CRLF | Repeat, saved sends |
| Monitor | Pause, Clear, Hex, Follow | Port and direction filters, search, ASCII or Hex, Export, Log and Topology JSON tabs |
| Dialogs | Guided, address read only, settings folded away | Address editable, settings open, More options |
| Status bar | Counts, rates, hub state, readiness chip, Simple switch | Adds a chip when bytes were dropped |

The Ports, Topology, Inspector, Send and Monitor panes stay visible at both
levels: they are how the user sees that the port is real and moving data.
Advanced removes the training wheels, not the feedback.

Switching is one checkable menu item (**View → Advanced mode**, `Ctrl+Shift+A`),
the segmented switch in the status bar, a switch in Settings, or the link in
the launcher. The setting is global and persisted. The status bar always shows
the current level so a screenshot is self-describing.

### Screen map

```
Menu: File Edit Ports Topology Tools View Help
Toolbar: New Open Save | Add port Attach Connect Remove |  [Search actions]
┌───────────────┐  ┌──────────────────────┐  ┌──────────────────┐
│ Ports         │  │ Topology             │  │ Inspector        │
│               │  │                      │  ├──────────────────┤
│               │  │                      │  │ Send             │
└───────────────┘  └──────────────────────┘  └──────────────────┘
Monitor (Advanced: + Log, Topology JSON)
Status: Ports Links RX TX Hub          Setup ready   [Simple|Advanced]
```

With no ports, the launcher takes the place of the three docks. At the minimum
window of 960 by 640 the right column becomes one tabbed dock, the toolbar
shows icons only, and the monitor is shorter.

## Task flows

### Give my program a port (operator)

Launcher card, or **Ports → Add port**, then **Create port**. The **Port ready**
dialog shows the name to open in large type, with **Copy name**. The disclosure
below it holds the saved-setup entry and the equivalent `vss_cli` command.

Design notes: the name field is pre-filled and the suffix is automatic; the
dialog explains the three kinds in one sentence each; the result sentence
changes with the count; the dialog is honest when there is no driver ("port-1
is ready inside Studio").

### Connect two programs (operator)

**Connect two programs** makes a linked pair like a null-modem cable. The
dialog shows both ports as large names under "Program A opens" and "Program B
opens", and says which control lines are wired.

### Wire existing ports (integrator)

**Topology → Connect ports** chooses a relationship in plain words (two-way,
one-way, monitor, split, join), lists only ports that exist, and updates its
summary sentence live. It validates before closing; a failure leaves the
dialog's data intact.

### Share a device (integrator)

**Share a device** is a three-step wizard: choose the device, say how many
programs and see which port each will open, then a summary of what to open in
each program. When there are too few driver pairs it says so and offers to
create another pair or keep the extras inside Studio. It is one rollback unit:
a failure removes what it created.

### Watch traffic (diagnostician)

**Watch traffic** adds a read-only copy of a port as a Studio-only port, so the
watcher can be removed when finished. It never drives modem lines.

### Diagnose (diagnostician)

Select a port: the Inspector shows state, counters and control lines. **Help →
Why is nothing arriving?** walks the steps from device to program. **Tools →
Setup check** reports what the PC needs. **File → Diagnostic bundle** gathers
the setup, log and environment, with traffic contents off unless chosen.
**About → Environment** gives the build.

## Human factors decisions

- **The launcher is a page, not a wizard.** It shows only while there is
  nothing else to show, names the four jobs in the user's words and says what
  each produces. It disappears when it stops being true and returns on request.
- **Readiness is always visible.** A strip in the launcher and a chip in the
  status bar say whether other programs can open the ports Studio creates,
  because "the port exists but nothing can see it" is the commonest failure.
- **"Open" is the honest state.** Studio cannot see whether another program has
  opened a port, so a healthy port reads *Open* and the troubleshooting list
  says "Not checked yet" for the last steps. It does not guess.
- **The log is Advanced.** It is invaluable when diagnosing and noise for
  everyone else; the monitor carries the everyday feedback.
- **The topology stays visible in Simple.** Hiding it would turn a wiring tool
  into a list; the boxes are the mental model. A list of links is available
  from **View** for screen readers and for people who prefer one.
- **Level is global, not per document.** A user is an operator or a
  diagnostician, not both from file to file.
- **Refresh is adjustable.** 250 ms feels live; busy machines can slow it down
  without changing what the studio does.
- **Theme and density follow the user.** The theme follows the desktop by
  default and can be forced, because lab machines and screenshots often need a
  fixed light or dark surface. Density is comfortable or compact.
- **No fonts are shipped.** The window inherits the platform UI font, so text
  metrics, scripts and accessibility settings match the rest of the desktop.
- **Nine languages, right to left included.** Hebrew, Yiddish and Arabic mirror
  the whole layout. See [i18n.md](i18n.md).

## Accessibility

- Every control has an accessible name and role; the status bar repeats the
  effect of the focused action.
- Colour is never the only signal: states are printed as text in the table and
  inspector, and the topology labels its links.
- Shortcuts are standard where the platform has a standard, and the Help menu
  lists them all.
- Dialogs are keyboard-navigable, validate on accept, and keep the user's input
  when validation fails.
- `vss_gui --selftest` walks the main window and every dialog in both themes,
  and a second CTest entry repeats the walk at 200 percent scale. It fails on
  an unnamed focusable control, a target under 24 by 24, a control that cannot
  take keyboard focus, or clipped text.
- `VSS_HIGH_CONTRAST=1` drops the custom stylesheet and uses the system
  palette; `VSS_REDUCE_MOTION=1` makes every transition zero-duration.

## Open design questions

- Should Simple collapse the topology to a list on very small screens?
- Should the Connect dialog remember the last relationship per user?
- Which engine capabilities are worth building next? The disabled ones are
  undo and redo, the per-line wiring matrix, observing whether a
  program opened a port and JSON error
  positions. Each is a row in
  [08-engine-gaps.md](https://github.com/iainchesworthlabs/virtual-serial-studio/blob/develop/docs/gui-design/docs/08-engine-gaps.md).

## Roadmap

1. Undo and redo for topology edits, once the engine can restore endpoint
   identities exactly.
2. The per-line wiring matrix for mock fixtures. Fault injection is done.
3. Showing whether another program has opened a port on Windows and macOS.
   Linux shows the *Waiting* and *Program connected* states for a pseudo
   terminal already.
4. Reconnect for a port that failed, with the note and button the design
   specifies.
5. Real-device templates that open the attach dialog.
6. An RFC 2217 server role, so the network endpoint can offer it as it already
   does for TCP and UDP.
