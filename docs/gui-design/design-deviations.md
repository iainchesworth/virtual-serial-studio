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
and `VSS_SCREENSHOT_THEME=light|dark` selects the theme. They exist only on
the documentation `--screenshot` path; normal startup ignores them and they do
not write `QSettings`. The default screenshots use two loopback ports, one
link, one mock port and a short write so the frame shows real data shapes.

## WP6

### The network bridge has no server role

The design's default is "Wait for other computers to connect (server)". WP6
re-checked Boost.VirtualSerial: `tcp_bridge_port` and `udp_bridge_port` only
resolve and connect, there is no acceptor or bind path, and
`vss::studio::bridge` attaches both URIs as clients. The server role cannot be
built without an engine change, which this package is not allowed to make.

`capabilities::network_server` is added and is false in the real adapter. The
network dialog defaults to the client role, disables the server option with
the tooltip "Not available in this build", and shows a visible warning that
this build can connect to a server but cannot wait for one. The firewall note
is therefore not shown in this build; it appears when a backend reports the
capability. URI preview, validation and the client bridge are real. The
bundled "Device on the network" template uses a loopback client target
(`tcp://127.0.0.1:4001`) for the same reason and still validates.

### Create another pair is a GUI adapter method

The share wizard's "Create another pair" needs `vss::create_com0com_pair`,
which the studio facade does not expose. WP6 adds one narrow method,
`StudioBackend::create_com0com_pair()`, implemented by the real adapter with
the elevation prompt and by the fake backend for tests. It is gated by
`capabilities::com0com_pair_creation` (false off Windows) and documented for
WP5 to reuse. No library behaviour changed.

### Real-device templates wait for the Attach flow

`share-three-programs` and `device-on-the-network` open a real serial device.
The design says the device is chosen through Attach, which belongs to WP5 and
does not exist on this branch. WP6 does not guess a device: those template
cards say "Needs a real device", the "Use template" button is disabled while
one is selected, and an info note says to use Ports, Attach a real device
first. The templates still load and validate in the unit tests, and the
dialog returns their documents once WP5 supplies the device flow.

### Share step 3 shows Open until the engine can see a program

`Capabilities::peer_open` is false, so a real backend reports
`peer_state::unknown`. The Ready table then shows the "Open" chip and the
result note omits the sentence about Waiting labels changing, following
`08-engine-gaps.md`. A backend that reports a definite `not_open` or `open`
shows "Waiting for program" or "Program connected"; the fake backend and the
tests cover both mappings. The engine work to detect an outside program is
unchanged.

### Add another program re-runs the one-step share

The engine has no add-listener operation: `studio::share_serial` creates the
source, every listener and the split link as one rollback unit. From step 3,
"Add another program" therefore goes back to step 2, and the next "Share
device" removes the previous share and creates the complete new one. A failed
replacement reports "The previous share was removed. Check the device, then
try again." A first share failure keeps the "The studio is unchanged"
sentence. The alternative, silently adding a listener outside the engine's
rollback unit, would not leave a failed share recoverable.

### Error pattern is inline in these dialogs

The shared error pattern in `screens/06` is a dialog. Connect, Share and the
network endpoint render the same danger title, cause and "Copy details" as an
inline panel so the wizard keeps its step and the entered values. A refused
link keeps the Connect dialog open exactly as the screen says. The "Show the
loop" button is not present because highlighting a cycle is WP4 topology
work; the loop sentence names the pair instead.

### Toolbar-only Advanced Share

The IA adds Share to the Advanced toolbar while the Topology menu keeps it in
Simple. `topology.shareToolbar` is a second, Advanced-only action placed on the
toolbar. `action_spec` gains a `palette` flag and this action sets it false,
so the command palette does not list the same command twice. No existing menu
action changed level or position.

### Copy added where the deck is silent

The following sentences are new because the specs do not name them: the
self-link reason ("A port cannot link to itself. Choose a different port."),
the connect result toasts ("Connected A and B.", "Redirected A to B.",
"Monitoring A on B.", "Split A to B and C.", "Joined A and B into C."), the
client-role result sentence ("Studio connects to X on port N and talks to
Y..."), the capability note, the validation sentence for a missing or invalid
address and the hint for a port outside 1 to 65535, "Turn on Advanced mode"
in the join More options row, "Use a device instead" in the share wizard, the
"No serial device is available" note, and the Templates note that a setup
needs Attach. All use the shared vocabulary and sentence case.

### Free-pair prediction and device settings

The share wizard predicts a port for each program before the engine runs:
each unclaimed com0com pair contributes the end the studio advertises (the
second end; the engine tries that order), and programs beyond the free pairs
are predicted as `loopback:listener-N`. The Ready step always uses the real
ports returned by `share_serial`. On platforms without com0com the planned
cell reads "PTY". Device settings are carried in the share URI's query string,
so no engine signature changed.

### Recent setups keep the WP2 keys and add per-file metadata

The launcher still reads `ui/recentFiles` and `ui/recentSummary`. WP6 adds
`ui/recentEntries/<sha1>/summary` and `.../lastOpened` for the Recent table,
recorded by `record_recent()`. The file list stays the ordering source, capped
at ten, and nothing is stored outside `QSettings`.

### No Qt Network module for the listen address

The design allows "a specific address" on the Listen on combo. Enumerating
local interfaces would add `Qt6::Network`, which is not an approved dependency
for this GUI. The combo offers all interfaces, this PC only, and a specific
address field instead; Advanced address editing accepts the full URI. This
covers the same values without a new module.

### Small widget extensions

`PickCard` gained `set_trailing()` for the A-to-B mode glyphs and now keeps its
content at the top with a fixed vertical policy, so template cards no longer
stretch. `NoteBanner` and `ResultSentence` gained a rich-text overload that
carries a plain accessible sentence, which keeps bold port names out of the
screen-reader text. `ActionRegistry` gained the `palette` placement flag
described above. The new QSS rules are `QLabel[secondary="true"]`,
`QFrame[dashed="true"]`, `QFrame[spin="true"]` and `QFrame#dialogFooter`; all
use existing tokens.
