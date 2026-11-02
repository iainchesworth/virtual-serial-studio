# 03 Information architecture

## Window frame (identical in Simple and Advanced)
Menu bar 28, toolbar 48, body, status bar 30. Body (8px padding, 8px gaps): top row of three docks, then the Monitor dock below.
- Ports dock left: 360 wide, minimum 240.
- Topology dock centre: flexible.
- Right column 340 wide, minimum 260: Inspector above Send (Send is 162 high). At the 960 by 640 minimum window the right column becomes one tabbed dock (Inspector, Send) 260 wide, the toolbar becomes icon only with a More overflow, and the Monitor is 156 high.
- Monitor dock bottom: 196 high, minimum 160.
- Default window 1280 by 800, minimum 960 by 640. Dock sizes persist through `saveState`.

## Launcher (empty state)
Shown in place of the dock grid while the studio has no ports, and from View, Show launcher. Contents: readiness strip, heading "What do you want to do?", four job cards, a Saved setups row with the most recent file, and an Advanced mode hint. Once any port exists the launcher disappears and the dock grid appears. Menu bar, toolbar and status bar are always visible.

## Menu structure (same for both levels)
Items marked **A** are visible only in Advanced mode and hidden in Simple. **new** means it does not exist in the current GUI. Ctrl is Cmd on macOS.
| Menu | Items |
|---|---|
| File | New (Ctrl+N), Open (Ctrl+O), Open recent *(new)*, Setups and templates *(new)*, Save (Ctrl+S), Save as (Ctrl+Shift+S), Export as command **A** *(new)*, Diagnostic bundle *(new)*, Quit (Ctrl+Q) |
| Edit | Undo (Ctrl+Z) *(new)*, Redo (Ctrl+Y, Cmd+Shift+Z) *(new)*, Rename port (F2, Return on macOS) *(new)*, Remove selected (Delete) |
| Ports | Add port (Ctrl+Shift+N) *(new)*, Virtual port, Linked pair *(new)*, Attach real device, Network endpoint **A** *(new)*, Mock fixture **A**, Copy port name (Ctrl+Shift+C) *(new)*, Remove port |
| Topology | Connect ports (Ctrl+L), Share a device *(new)*, Watch traffic *(new)*, Remove link, Link wiring **A** *(new)*, Owner **A** *(new)*, Fit (Ctrl+0) **A** |
| Tools | Search actions (Ctrl+K) *(new)*, Setup check *(new)*, Benchmark **A** *(new)*, Refresh now (F5), Settings (Ctrl+,) |
| View | Advanced mode (Ctrl+Shift+A) *(new shortcut)*, Monitor, Log **A**, Topology JSON **A**, Theme, Density *(new)*, Reset layout *(new)*, Show launcher *(new)* |
| Help | Help contents (F1) *(new)*, Why is nothing arriving? *(new)*, Keyboard shortcuts *(new)*, Copy environment, About |

The old Scenarios menu is retired. Share and bridge live under Topology and Ports, and the null-modem demo becomes a template. Menu items hidden in Simple still appear in the command palette with a "Needs Advanced mode" prompt.

## Toolbar
Simple: New, Open, Save, separator, Add port, Attach device, Connect, Remove, spacer, Search actions field (Ctrl+K). Advanced adds after Remove: separator, Mock device, Share, Fit. Buttons show icon and text. Remove and Connect are disabled when they cannot act.

## Status bar
Left: Ports N, Links N, RX rate, TX rate, Hub state. Advanced also shows a warning chip when bytes were dropped. Right: setup readiness chip (Setup ready, or Setup needs attention linking to the setup check) and the Simple or Advanced segmented switch. Mode is global, never per document.

## Mode switching
Stored in settings and applied immediately. Advanced mode additions by area:
| Area | Simple | Advanced adds |
|---|---|---|
| Toolbar | as above | Mock device, Share, Fit |
| Ports dock | name, role, state, path with copy | RX, TX, Dropped columns, filter |
| Topology | boxes and lines | link bar (policy, owner, Release, Give to), Fit |
| Inspector | state, path, traffic, control lines read-only | switches for DTR and RTS, Break, link to line wiring and faults |
| Send | text or hex, Add CRLF | saved sends, Repeat |
| Monitor | Pause, Clear, Hex, Follow | filter by port and direction, search, ASCII or Hex segmented, Export, Log tab, Topology JSON tab |
| Dialogs | guided with defaults | editable address and command, More options sections |

## Persistence (QSettings keys, suggested)
`ui/advanced` bool, `ui/theme` (system, light, dark), `ui/density` (comfortable, compact), `ui/refreshMs` int, `ui/defaultBaud` int, `ui/askOnDiscard` bool, `ui/windowState`, `ui/geometry`, `ui/recentFiles` list. The title bar shows `*` while there are unsaved changes.
