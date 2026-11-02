# Design deviations and recorded decisions

The design bundle is the contract. Where the bundle is silent, or where its
wording conflicts with the repository, the choice and its reason are recorded
here. Entries name the work package that owns the decision.

## WP0

### Deviations file path

`docs/09-qa-and-definition-of-done.md` says `docs/design-deviations.md`;
`AGENTS.md`, WP0 and WP9 say `docs/gui-design/design-deviations.md`. This file
uses the bundle path. The QA reference is treated as a typo.

### GUI logic tests use a Qt-aware runner

WP1's prompt says to test with the in-tree framework. The in-tree framework in
`tests/framework` is deliberately Qt-free and has no `QCoreApplication`. The
backend has signals and a `QTimer`, and widgets need a `QApplication`, so both
test executables reuse the in-tree `TEST_CASE` and `REQUIRE` macros with a
small Qt-aware runner (`vss_gui_backend_tests`, `vss_gui_theme_tests`). The
whole-window walk stays in `vss_gui --selftest`. No new third-party test
framework is added, so the intent of the rule is met.

### No per-write callback from the engine thread

The design says failures must be visible locally. A per-write completion
callback would have to touch a `QObject` from the endpoint strand and risks a
use-after-free when the backend is destroyed with a write pending. The adapter
therefore returns an immediate error for a missing or closed port, queues the
write, and surfaces a queue-full rejection as the engine's error event through
the coalesced batch. The Send panel can show "queued" and the event; it does
not need a per-write signal.

### Integration branch

Work packages merge into `gui/integration`, not `develop`, until WP9. The
design replaces the shell and leaves placeholders between WP2 and WP8, so
merging each package to trunk would make `develop` unusable for the duration.
This is a delivery decision, not a change to the design.

### Qt version pin for builds

The machine has Qt 6.10.3 under `C:\Qt` and Qt 6.12 under
`C:\Repos\common\qt`. A `QT_ROOT` value with a trailing space makes CMake miss
one and silently select the other; the binary then imports a version tag that
the runtime does not export (`qt_version_tag_6_12` against 6.10). All builds
pin `QT_ROOT` without a trailing space and also pass
`-DCMAKE_PREFIX_PATH`.

WP0 used 6.10.3. WP1 needs the approved `Qt6::Svg` module, which the 6.10.3
installation does not have; the 6.12 installation does. GUI builds therefore
pin 6.12.0 from `C:\Repos\common\qt\6.12.0\msvc2022_64`. This is an
environment decision, not a product dependency change: `Qt6::Svg` is the
dependency the bundle already approved.

## WP1

### Offscreen gallery and the system font

The offscreen platform on Windows has no native font database, and the
project forbids bundling a font. `vss_gui_theme_tests` points
`QT_QPA_FONTDIR` at `C:/Windows/Fonts` so the review PNGs show real text
instead of missing-glyph boxes. This is test-only; the product continues to
use the system UI font through `QFontDatabase`, and the test adds no font to
the repository or the build output.

### SegmentedControl uses QRadioButton

The segmented control is a group of exclusive `QRadioButton`s with the
indicator hidden by QSS. That gives screen readers radio-group semantics and
arrow-key behaviour without inventing an accessible interface. The selected
segment is still an accent fill with `onAccent` text, as the design specifies.

### CopyButton paints itself

A 24 px `QToolButton` inherited the 10 px toolbar padding and clipped the
16 px copy glyph. `CopyButton` is now a custom-painted `QAbstractButton` with
the same accessible name, tooltip, focus ring and 24 px target.

## WP2

### The old shell stays compiled until WP9

`main.cpp` now constructs `ShellWindow` from `src/shell/`, but the flat
`main_window.cpp`, the old panels and dialogs, `theme.cpp`, and `selftest.cpp`
are still in the executable's source list and still compile. They are not
referenced by `main.cpp`. Deleting 24 files in the same commit as the first
working frame would have made the WP2 diff much harder to review and risked a
link surprise while the panes were still placeholders. WP9 owns the deletion,
the compatibility `theme.hpp` removal and the whole-tree literal scan. The
new shell does not link to or instantiate the old `MainWindow`.

### Placeholder handlers show a message

Actions and launcher cards whose dialog or pane belongs to WP3 to WP8 are
registered, have their final menu/palette/toolbar placement and level, and
show a status-bar sentence that names the work package boundary. They do not
simulate a result and do not disable the rest of the frame. The handlers are
the only temporary part; WP3 to WP8 replace them in place.

### Unsaved-changes prompt uses QMessageBox

`ConfirmDialog` has two buttons. Discarding a setup needs Save, Don't save and
Cancel, so WP2 uses a `QMessageBox` with the spec's title, body and button
labels and honours `ui/askOnDiscard`. WP8 replaces it with the shared dialog
and the Settings toggle. The decision protects user data in the interim.

### Compact layout at 960x640

The design says that at the 960x640 minimum the right column becomes one
tabbed dock and the toolbar drops to icons. The implementation switches at
width < 1100 px or height < 700 px, so a 960-wide window is already compact,
the toolbar is icon-only, Inspector and Send are tabbed, and Monitor starts at
156 px. Qt's toolbar extension handles overflow; there is no hand-built More
menu. The shell requests the token sizes for the ports, inspector and monitor
docks. With the current one-line placeholder panes, QMainWindow's size hints
settle the split nearer the minimum; WP3 and WP4 set the final pane content and
the reviewed 360/340/196 split. The minimum sizes are already the token values.

### Menu and icon colours after a theme switch

The bundle's QSS template leaves `QMenuBar` and `QMenu` text colour to the
palette. On Windows after a runtime switch to dark that rendered the menu text
dark-on-dark. The product template now binds those three selectors to the
`ink` token. Action icons had the same problem because `QAction` stores the
rendered `QIcon`; `ActionRegistry` flushes `IconProvider` and rebuilds every
registered icon on `Theme::themeChanged`. No token value changed.

### Shell library name

The plan proposed one `vss_gui_ui` library. WP2 used `vss_gui_shell`, which
links the widget, backend and theme libraries and is what the shell tests
link with `FakeStudioBackend`. The old executable retains its flat sources.
The dependency direction matches the plan; only the target name and the
deletion timing differ.

### Toolbar Share is deferred to WP6

Share stays in the Topology menu in Simple, matching the IA. The design's
Advanced toolbar adds Share, but WP2 has no share dialog; WP6 owns that flow
and can add the toolbar-only advanced action (or change the toolbar policy)
without making the menu action Advanced. Mock and Fit are present in Advanced
so the mode difference is already visible.

### Review-only screenshot switches

`VSS_SCREENSHOT_EMPTY=1` renders the launcher instead of the sample topology,
`VSS_SCREENSHOT_THEME=light|dark` selects the theme, `VSS_SCREENSHOT_ADVANCED=1`
selects the mode and `VSS_SCREENSHOT_COMPACT=1` renders 960 by 640. They exist
only on the documentation `--screenshot` path; normal startup ignores them. The
advanced switch writes `ui/advanced` only on this review path so the image is
repeatable regardless of the user's saved mode. The default screenshots use two
loopback ports, one link, one mock port and a short write so the frame shows
real data shapes.

## WP3

### Legacy flat sources leave the executable now

WP2 kept the old `main_window`, panels, dialogs and legacy `theme.cpp` in the
`vss_gui` executable so the first shell build could land without a 24-file
deletion. WP3's new panes reuse the old class names (`PortModel`,
`MonitorModel`, `InspectorPanel`, `SendPanel`), so compiling both sets produced
duplicate symbols. The executable now builds only `main.cpp`; the legacy files
remain on disk but are not compiled. WP9 owns deleting them and broadening the
no-hex scan to the tree. This is an earlier build-list cleanup than the WP2
plan, forced by the name collision.

### SelectionService lives in panes

The one selected port/link moved from `src/shell/selection_service.hpp` to
`src/panes/selection_service.hpp`. Ports and Monitor need its metaobject, and a
header-only Q_OBJECT in the shell library would have forced the pane tests to
link the whole shell. The shell links panes, so both the frame and the data
panes share the one service without a circular target.

### Default dock minimums are the reviewed defaults during WP3

The frame sets Ports to 360, Inspector to 340 and Monitor to 196 at normal
width, with the compact minima 240/260/156. Qt's placeholder size hints kept
settling near the minima, so WP3 uses the reviewed defaults as the current
minimums to make the real panes and screenshots match the spec. This is a
temporary layout constraint; WP9 can relax it once the panes own their content
sizes and the splitter is re-checked at 200 percent text.

### Monitor tabs and the Log view

The Monitor dock title already names the dock, so Simple hides the one-tab tab
bar instead of drawing "Monitor" twice. Advanced restores the bar and adds Log
and Topology JSON. Log is a read-only `QPlainTextEdit` capped at 10,000 blocks,
fed from the coalesced event batch. It is not a model/view pair because it has
no per-row actions; this keeps it outside the no-per-row-widget rule and
preserves the text selection users expect in a log.

### DROP rows aggregate the real link counter

The engine increments `link_stats.dropped` but does not publish a per-drop
event with a payload or direction. Monitor therefore adds a DROP row when a
link's dropped counter grows, labelled "not the owner, not forwarded", with the
source/target name and the delta. It never invents a payload. The row is
filtered like any dropped direction.

### Advanced Send and Monitor heights

The spec gives Send 162 and Monitor 196. WP3 keeps those defaults in Simple.
Advanced Send adds the saved-sends row and Repeat, and Advanced Monitor adds the
tab bar and filters, so both grow by their content height (about 230) rather
than clipping controls. Compact still uses the spec's 156 Monitor. WP9 owns the
final compact/200 percent pass.

### Peer-open honesty

`EngineStudioBackend` reports `peer_state::unknown` because the engine cannot
observe an external opener. Ports therefore show "Open" and Inspector omits the
"No program has opened ..." sentence. "Waiting for program" and "Program
connected" are exercised by the fake in tests and will appear if a later engine
capability reports the state. No waiting text is shown for Unknown.

### No F2 rename

`capabilities::rename_port` is false, so the Ports keyboard map implements Up,
Down, Enter, Delete and Ctrl+Shift+C but not F2. Rename stays hidden rather than
corrupting the studio's records and saved JSON. The existing placeholder
`edit.rename` is not registered in WP3.

## WP4

### The topology module is `vss::gui::topology`

The legacy flat `apps/vss_gui/topology_view.cpp` was still compiled when WP4
branched, and it already defines `vss::gui::TopologyView`; linking the new
module under that name produced duplicate symbols. WP3 later removed the legacy
file from the executable, but the new module keeps the nested namespace
(`TopologyView`, `NodeItem`, `LinkItem`, `LinksList`, layout helpers) and
`vss::gui::topology_text` for its copy helpers. No product behaviour depends on
the namespace; WP9 can flatten it when it deletes the remaining legacy files.

### Rename is hidden, not faked

`Capabilities::rename_port` is false, and `08-engine-gaps.md` says to hide
Rename until the studio can rename safely. The node context menu therefore
omits Rename and there is no F2. If a future backend sets the capability the
menu item appears and emits `rename_requested`; the shell answers "Not
available in this build." rather than doing nothing.

### Remove has confirmation but no Undo

`Capabilities::undo` is false, so Remove link and Remove port confirm the
destructive action and say nothing is saved until the setup is saved. No Undo
toast is offered because it could not be made reliable. This matches the
WP3/WP4 instruction to confirm only for now.

### Nodes are nudged with the keyboard, not dragged

The design forbids requiring a drag (WCAG 2.5.7) and the acceptance asks for
keyboard-only operation. Arrow keys move the focused node by 8 px, Shift by
32 px, and the offset is kept per port for the session. Pointer dragging is
not implemented, so a click can never move a node by accident. Offsets are
never written to the JSON snapshot.

### Node sizes are minima, not fixed boxes

168 by 56, 168 by 64 for a real device or the target of a join, and 120 by 48
in compact mode are the design sizes. `node_size_for()` grows the box when the
name, detail line and badges need more room, so 200 percent text cannot clip
(WCAG 1.4.4). At the default system font the boxes are the design sizes; the
test asserts that and the doubled-font case.

### Compact switches at 500 px of topology width

The design says compact sizes apply at the 960 by 640 minimum window. The
actual central width there is about 430 px after the Ports and right column
docks, so the view switches to the 120 by 48 nodes below 500 px of its own
width. This keeps the default 1280 by 800 window on the comfortable sizes.

### Unlinked ports get their own column

The design asks for a deterministic automatic layout and gives example
positions but not a rule for unlinked ports. Putting them in the same left
column as a split source pushed that source off the vertical centre, so an
unlinked port now sits in a final column after the graph. Split and join
sources and their target group stay centred on each other.

### Monitor links keep their arrow head

`03-topology-view.md` says monitor links are dashed and that one-way and
redirect links end in a filled arrow head. A monitor link is one-way in the
engine, so it is drawn dashed with an arrow head; split and two-way links have
no heads.

### Loop feedback lives in the topology

`06-support-and-system.md` asks for a modal refusal with Close and "Show the
loop". That dialog belongs to WP6's Connect ports flow. WP4 provides the
topology half: `show_loop_note()` finds the cycle, highlights its links in
`bad`, and shows the exact danger note in a `NoteBanner` with a "Show the loop"
action that frames the cycle. The Connect dialog will call it; Escape
dismisses the note in the meantime.

### Per-item QAccessibleInterface is not implemented

`03-topology-view.md` says to give each node a `QAccessibleInterface` "if
practical". QGraphicsItem is not a QObject and a custom interface would
duplicate the accessible model that WP3's Ports dock and WP4's Links list
already provide. The view instead sets the name "Topology", generates the
specified description ("4 ports, 1 link: split from device to terminal,
test-tool and logger") and updates it with a DescriptionChanged event; the
Links list exposes every link with its names and state to the keyboard and to
screen readers.

### Links list placement

"Show links as a list" is a View menu item in Simple mode. The list is a dock
tabbed with the Monitor and hidden by default, because the Ports dock is WP3
and the design does not name a home for it. Turning the action on shows the
dock; choosing a row selects the link through `SelectionService`.

### The topology auto-fits until the user zooms

Fit is an Advanced command (Ctrl+0), but the first view and every resize fit
the content until the user zooms or pans, so a fresh workspace never opens
with the graph off-screen. `fit_content()` restores that automatic behaviour.

### Single selection between ports and links

Selecting a node clears the selected link and vice versa, matching the
"Click a box ... Click a line ..." copy and the old at-most-one contract.
`SelectionService` still carries both ids; the topology keeps one non-zero.

### Owner and Dropped badges are read-only here

WP7 owns the owner control (the Advanced link bar) and the owner flow. WP4
paints the Owner, Dropped and "not linked" badges from the snapshot so the
nodes match the Advanced mockup, and its Owner action is a placeholder naming
WP7. Nothing in the topology changes ownership.

### Placeholders name their package

Connect ports, Share a device and Watch traffic say they arrive with WP6
(connect and share). Link wiring and Owner say WP7 (Advanced tools). Fit,
Remove link, Copy name and the Links list are implemented here. Each
placeholder is a status-bar sentence, never a fabricated result.
