# User experience

Virtual Serial Studio is a tool for people who need a serial port to exist,
right now, and who should not have to understand the library's topology model
to get one. This page records the human-factors decisions behind the window and
the gaps still to close.

## Principles

1. **Recognition over recall.** Every operation is a menu item, a toolbar
   button or a dialog with the available ports listed by name. Nothing requires
   typing a URI unless the user chooses the advanced path.
2. **Progressive disclosure.** Simple mode shows the four everyday jobs —
   create a port, attach a real one, connect two ports, watch traffic. Expert
   tools (fixtures, raw URIs, topology editing) live behind Advanced mode,
   which is one checkbox away and remembered.
3. **Plain language.** The UI talks about *ports*, *listeners*, *publishers*
   and *monitors*; it says "two-way", "one-way", "split" and "join" rather than
   exposing `link_mode` values.
4. **Nothing surprising.** Every action states its effect in the status bar,
   every dialog validates before it closes, and failures are reported with the
   library's own message.
5. **Reversible where possible.** Ports and links can be removed from the
   table or the topology view; removing a port removes its links explicitly
   rather than leaving dangling routes.
6. **Keyboard first, mouse equal.** Connect is `Ctrl+L`, settings `Ctrl+,`,
   remove is `Delete`, file operations use the platform shortcuts.
7. **Accessible by default.** System colours and fonts, scalable layouts,
   labels on every control, and no information carried by colour alone (state
   is also text).

## Simple mode vs advanced mode

| Area | Simple | Advanced |
|---|---|---|
| Add virtual port | yes | yes |
| Attach a real serial device | yes | yes |
| Connect ports (two-way, redirect, monitor, split, join) | yes | yes |
| Remove ports and links | yes | yes |
| Monitor, send, inspector | yes | yes |
| Mock fixture device | hidden | yes |
| Null-modem demo | hidden | yes |
| Share over a network URI, join by URI, bridge | hidden | yes |
| Topology fit / raw link editing | hidden | yes |
| Settings (refresh, theme, default baud) | yes | yes |

Simple is the default. The choice is stored in `QSettings` and can be toggled
from **View → Advanced mode** or from Settings.

## A day in the tool

1. **"I need a COM port for this program."**
   *Ports → Add virtual port…*, pick a name and *Auto*; the table shows the
   advertised path (for example `COM7` or `/dev/pts/3`). That is the path the
   other program opens.
2. **"I need two ports wired together."**
   *Ports → Null-modem loopback* (advanced) creates a pair and links them; or
   add two ports and use *Connect ports… → Two-way*.
3. **"Several programs should see the same device."**
   *Connect ports… → Split one port to many*, select the source and tick the
   targets. Each listener receives a copy.
4. **"Several sources should feed one program."**
   *Connect ports… → Join many ports into one*, tick the publishers and pick
   the target.
5. **"I need to watch what a program sends without disturbing it."**
   *Connect ports… → Monitor* creates a one-way copy that never drives modem
   lines; the monitor pane shows the traffic.
6. **"My program is on another machine."**
   Advanced mode: *Scenarios → Share a serial port…* (TCP listeners) or
   *Bridge to network…* (`tcp://`, `udp://`, `rfc2217://`). A guided
   host/port form is the next step.

## The About window

Support questions start with "which build?". The About window therefore has
four tabs:

- **About** — version, purpose, copyright and the GPLv3 notice.
- **Environment** — commit, branch, build date and type, compiler, Qt runtime
  and build version, OS, architecture and kernel.
- **Licences** — Qt, Boost, Boost.VirtualSerial, Tracy and the optional
  com0com driver, with a pointer to `THIRD_PARTY_NOTICES.md`.
- **Fonts** — the application bundles no fonts; it names the system UI font in
  use and explains that its licence comes from the operating system.

The environment tab is intended to be copied into a bug report.

## What is still missing

- Guided forms for **share/bridge**: host, port, protocol and listener count
  instead of a free-form URI.
- A **pair manager** for creating batches of virtual ports ("add 8 pairs"),
  with names and advertised paths in one table.
- A **line-control matrix** editor for DTR/DSR/RTS/CTS crossing per link.
- **Bundles**: naming a group of links and applying settings to all of them.
- **Switch** semantics as a first-class action (owner arbitration between two
  publishers, with a visible "current owner").
- **Undo/redo** for topology edits.
- A first-run **onboarding** page: create a port, open the monitor, done.

Each of these is a self-contained addition to this shell; the studio API
already exposes the operations they need.
