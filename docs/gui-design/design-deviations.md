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

## WP7

### Fault injection stays gated; no `vss_studio` forwarding method

The engine's mock transport exposes `serial_transport::mock()`, but the
endpoint keeps its transport private and `vss_studio` has no accessor. WP7 was
instructed to keep `Capabilities::faults` false and not to change
`libs/vss_core`, so the six fault tiles, their buttons and the history table
are built and disabled with "Not available in this build". No fault state is
simulated in the GUI. Adding the thin forwarding method remains an approved
follow-up outside this package; the tiles are already laid out for it.

### Per-link wiring is a read-only Standard preset

`StudioBackend` gained `link_wiring()` and `set_link_wiring()` with
`unsupported`/`nullopt` defaults so the dialog has one adapter seam, but the
real backend keeps `Capabilities::line_matrix` false. The matrix therefore
shows the Standard null-modem preset and is read-only. `FakeStudioBackend`
stores a wiring for tests so the editable path is exercised without faking a
real engine feature. The preset table lives in `advanced/line_presets.cpp` and
uses the engine's `null_modem_peer()` mapping (DTR→DSR, RTS→CTS, DTR→DCD) for
Standard. The bundle does not define "Loopback"; it is two wires, DTR→DSR and
RTS→CTS, and neither it nor Custom can be chosen in this build.

The bundle's Faults mockup shows the matrix on the left of "Inject a fault";
the wording in `screens/05` says rows DTR and RTS and columns DSR, CTS, DCD,
RI, which is what the implementation draws. The caption and the generated
sentence use the linked port's real name.

"Emulate modem lines" and "If both write at once" are read-only for the same
reason: `link_options` is copied when the hub creates a link, and no engine
call changes it afterwards. Both controls reflect the live link and carry the
degraded tooltip rather than pretending to be changeable.

### The owner link bar is hosted under the topology placeholder

WP4 owns the topology diagram and the node chips, and WP3 owns the Ports table
and the Monitor. WP7 hosts `OwnerBar` directly under the central topology
placeholder, shows it only in Advanced, and chooses its link in this order:
the selected link, the selected port as a join target, then the first
owner-policy join. That fallback keeps the control usable before WP3 supplies
a Ports selection and before WP4 supplies a link selection. When WP4 lands it
can move the same widget into the Topology dock unchanged.

The `Owner` and `Dropped` node chips, the Ports `Dropped` column and DROP rows
in the Monitor are not shipped here: they belong to WP3/WP4 and the last one
also needs a drop event the hub does not publish. Their owner/state data is
available through `link_view::owner` and `link_stats::dropped`. The one part
WP7 owns, the status bar sentence, now reads "N dropped, not owner" when
every dropped byte came from an owner-policy link.

### Monitor drops have no engine event

`hub::route_data` increments `link_stats.dropped` for a non-owner publisher
and continues; it does not publish an event. The Log therefore shows state
changes and errors, not DROP traffic rows, and the Monitor cannot draw them
without an engine change. Adding such an event would change `libs/vss_core`
and is out of scope. This is the main visible gap against `screens/05`.

### Log and Topology JSON are docks, not Monitor tabs

WP3 owns the Monitor bottom tab set. Until it lands, WP7 adds `logDock` and
`jsonDock`, tabifies them with `monitorDock`, and hides them in Simple mode.
Both are advanced-only actions, so the menu, palette and mode behaviour match
the design. `TopologyJsonEditor` and `EventLogPanel` are standalone widgets;
WP3 can move them into the final tab widget without a rewrite.

### Topology JSON validation is layered

Syntax errors come from `QJsonDocument::fromJson` and carry its offset, so
they always have a line. Schema errors are found by explicit GUI checks that
name the field (unknown port kind, unknown virtual kind, missing address,
duplicate names, unknown link mode with a `Did you mean` suggestion, bad
arity, missing port references and loops); the helpers locate the offending
token in the text so "Go to line" appears where it can. The check order
matches the hub: a loop is tested against routes created so far, and a duplex
link's reverse route is added with it. `vss::config::from_json()` has the
final word and `Apply` is disabled unless it accepts the document, so an
invalid document is never applied. A schema error that only the engine can
name hides "Go to line". `Capabilities::json_positions` stays false because
the engine still has no positions; the GUI-side checks are best effort and
the deviation is that they exist at all.

`Export file` in the JSON editor writes the editor text verbatim when it
parses; `Apply` always uses the engine's parsed document. `Format` uses the
engine's canonical serialisation when the document is valid and Qt's
indented form when only the JSON syntax is valid.

### Benchmark ships the dialog only

`implementation-plan.md` records the maintainer default as "no extraction";
the WP7 prompt allows the UI and requires the linked-port block. The dialog
has `Capabilities::benchmark` false, so Run is disabled with "Not available in
this build" and no metric or result is invented. The linked-port refusal is
real: it is computed from the live links, shown in a warning note and used as
the Run tooltip, so a future core can keep the same gate. `Copy result` stays
disabled because there is no result. The primary button reads "Run" rather than
the mockup's "Run again", because the mockup shows a completed run and this
dialog has never run. `vss_cli bench` was not modified.

### Add a mock fixture grows a truthful sentence

The fixture and its two-way link are real `create_mock_port` and
`connect_ports` calls. The result sentence uses the design wording when both
`faults` and `line_matrix` exist; in this build it says "After it exists you
can open it from the Inspector." instead of promising fault injection and
wiring changes the build cannot do. A failed link rolls the new fixture back so
the setup is not left half-created.

### Export as command has no mockup

There is no dedicated mockup for File, Export as command. It reuses the JSON
view's "Run it without the window" block: the real file name when the setup
has been saved, a suggested `<name>.json` otherwise, "Copy as command" and
"Export file". Export writes the current snapshot through
`vss::config::save_file` and updates the command afterwards.

### Event log timestamps are GUI arrival times

Hub events carry `steady_clock` time points, which have no wall-clock origin.
The Log stamps a row when the coalesced batch reaches the GUI thread, using
`QTime::currentTime()` with millisecond precision. That is the only honest
timestamp available without an engine change.

### Advanced dialogs and theme changes

The shared footer, `ink2` labels and the matrix caption rebuild their palette
or style when `Theme::themeChanged` fires, so a dialog opened before a theme
switch stays readable. This matters because the review PNGs construct each
dialog in one theme and grab it in both.

### Testable degraded state

`line_wiring` and the adapter methods live in `backend/view_types.hpp` and
`studio_backend.hpp`, not in `vss_core`. No topology schema field was added,
no node position was written to the document, and no new dependency was
introduced.
